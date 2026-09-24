// SPDX-License-Identifier: AGPL-3.0-only
#include "netfetch.h"
#include "ceiling.h"
#include "hosts.h"

#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

// Pinned origin (docs/signed-ceiling.md, bootstrap rule): public TLS to openpak.org, never
// a URL taken from the profile or a guest DNS override.
#define CEILING_URL "https://openpak.org/api/v1/network/ceiling"
#define PROFILE_URL "https://openpak.org/api/v1/network/profile?platform=switch"
#define NETWORK_LOG "/switch/openpak/network.log"

typedef struct { char *data; size_t len, max; bool overflow; } body;

static size_t collect(char *p, size_t size, size_t n, void *u) {
    body *b = u;
    size_t add = size * n;
    if (b->len + add > b->max) { b->overflow = true; return 0; }
    char *grown = realloc(b->data, b->len + add + 1);
    if (!grown) return 0;
    b->data = grown;
    memcpy(b->data + b->len, p, add);
    b->len += add;
    b->data[b->len] = '\0';
    return add;
}

// One GET, 2 s to connect and 3 s overall, no retry: an offline console opens the tool
// without waiting. Returns the body on 200, else NULL with err filled.
static char *get(const char *url, size_t max, size_t *len, char *err, int errlen) {
    body b = {NULL, 0, max, false};
    CURL *c = curl_easy_init();
    if (!c) { snprintf(err, errlen, "could not start a request"); return NULL; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "openpak-nro/" OPENPAK_VERSION);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || status != 200 || !b.data) {
        if (b.overflow) snprintf(err, errlen, "%s: answer larger than %zu bytes", url, max);
        else if (rc != CURLE_OK) snprintf(err, errlen, "%s: %s", url, curl_easy_strerror(rc));
        else snprintf(err, errlen, "%s: HTTP %ld", url, status);
        free(b.data);
        return NULL;
    }
    *len = b.len;
    return b.data;
}

static void logline(FILE *log, const char *fmt, ...) {
    if (!log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log, fmt, ap);
    va_end(ap);
    fputc('\n', log);
}

void openpak_network_refresh(char *note, int notelen) {
    note[0] = '\0';
    openpak_policy_set_fresh(false);
    char path[320], err[256];
    snprintf(path, sizeof(path), "%s/switch/openpak", openpak_root);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s%s", openpak_root, NETWORK_LOG);
    FILE *log = fopen(path, "wb");            // this launch's reasons, once each

    // Not connected: say so at once rather than waiting out two timeouts.
    bool online = false;
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        NifmInternetConnectionType type;
        u32 strength = 0;
        NifmInternetConnectionStatus st;
        online = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &st)) &&
                 st == NifmInternetConnectionStatus_Connected;
        nifmExit();
    }
    if (!online || R_FAILED(socketInitializeDefault())) {
        snprintf(note, notelen, "Offline: using the saved OpenPak network rules.");
        logline(log, "no network: nothing fetched");
        if (log) fclose(log);
        openpak_policy_reload();
        return;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // The ceiling first: the profile is filtered through whichever ceiling is in force.
    size_t len = 0;
    char *env = get(CEILING_URL, 64 * 1024, &len, err, sizeof(err));
    if (env) {
        openpak_ceiling c;
        if (openpak_ceiling_accept(env, len, &c, err, sizeof(err)))
            logline(log, "ceiling version %lld accepted (%d switch families)", c.version, c.count);
        else
            logline(log, "%s; keeping the saved ceiling", err);
        free(env);
    } else {
        logline(log, "ceiling not fetched (%s); keeping the saved ceiling", err);
    }
    openpak_policy_reload();
    const openpak_ceiling *ceiling = openpak_ceiling_current();
    if (!ceiling->version) logline(log, "no verified ceiling: the compiled families filter the profile");

    // The profile is saved only if it parses and leaves something to install; a broken
    // answer never replaces a working saved copy.
    char *profile = get(PROFILE_URL, 256 * 1024, &len, err, sizeof(err));
    if (profile) {
        char dropped[256] = "";
        openpak_policy *p = openpak_profile_policy(profile, len, ceiling, OPENPAK_SOURCE_PROFILE,
                                                   dropped, sizeof(dropped), err, sizeof(err));
        if (!p) {
            logline(log, "profile rejected: %s", err);
        } else {
            snprintf(path, sizeof(path), "%s%s", openpak_root, OPENPAK_PROFILE_CACHE);
            if (openpak_write_file_atomic(path, profile, len, err, sizeof(err))) {
                openpak_policy_set_fresh(true);
                logline(log, "profile saved (%d host rules)", p->count);
            } else {
                logline(log, "profile not saved: %s", err);
            }
            openpak_policy_free(p);
        }
        free(profile);
    } else {
        logline(log, "profile not fetched (%s); keeping the saved copy", err);
        snprintf(note, notelen, "Could not reach openpak.org: using the saved OpenPak network rules.");
    }

    curl_global_cleanup();
    socketExit();

    openpak_policy_reload();
    if (openpak_policy_dropped()[0])
        logline(log, "left out (outside the signed ceiling, or not expressible in a hosts file): %s",
                openpak_policy_dropped());
    if (openpak_policy_problem()[0]) logline(log, "%s", openpak_policy_problem());
    if (log) fclose(log);
}
