// SPDX-License-Identifier: AGPL-3.0-only
#include "update.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef OPENPAK_HOST_TEST
#include "netfetch.h"
#include <curl/curl.h>
#include <errno.h>
#include <json-c/json.h>
#include <switch.h>
#endif

// Three numbers, whatever follows them ignored.
static bool parts(const char *v, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!v) return false;
    if (*v == 'v') ++v;
    if (*v < '0' || *v > '9') return false;
    for (int i = 0; i < 3; ++i) {
        char *end = NULL;
        out[i] = (int)strtol(v, &end, 10);
        v = end;
        if (*v != '.' || v[1] < '0' || v[1] > '9') break;
        ++v;
    }
    return true;
}

bool openpak_update_newer(const char *tag, const char *mine) {
    int a[3], b[3];
    if (!parts(tag, a) || !parts(mine, b)) return false;
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

bool openpak_update_asset_ok(const char *url, const char *name) {
    if (!url || !name || !name[0]) return false;
    size_t prefix = strlen(OPENPAK_ASSET_PREFIX);
    if (strncmp(url, OPENPAK_ASSET_PREFIX, prefix) != 0) return false;
    const char *tag = url + prefix, *slash = strchr(tag, '/');
    if (!slash || slash == tag) return false;                 // the release tag
    return strchr(slash + 1, '/') == NULL && strcmp(slash + 1, name) == 0;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

// An NRO's header carries its own length at 0x18, and elf2nro appends the icon, the NACP and the
// romfs behind an "ASET" header at that offset, the last of them ending at the end of the file.
// Together they say the card holds a whole homebrew build: not an error page, and not a transfer
// that stopped early. The romfs is required because it is where the OpenPak CA travels.
bool openpak_update_is_nro(const char *path) {
    uint8_t head[0x20] = {0}, aset[0x38] = {0};
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(head, 1, sizeof(head), f) == sizeof(head) && fseek(f, 0, SEEK_END) == 0;
    long size = ok ? ftell(f) : -1;
    uint32_t nro = get32(head + 0x18);
    ok = ok && size >= 0x1000 && memcmp(head + 0x10, "NRO0", 4) == 0 &&
         nro >= 0x1000 && (long)nro <= size - (long)sizeof(aset) &&
         fseek(f, (long)nro, SEEK_SET) == 0 && fread(aset, 1, sizeof(aset), f) == sizeof(aset);
    fclose(f);
    if (!ok || memcmp(aset, "ASET", 4) != 0) return false;
    uint64_t off = get64(aset + 0x28), len = get64(aset + 0x30);     // the romfs, the last entry
    return off != 0 && (uint64_t)nro + off + len == (uint64_t)size;
}

#ifndef OPENPAK_HOST_TEST
#define NRO_MAX (16 * 1024 * 1024)

static char self[320];          // the running NRO, the file an update replaces
static char tag[32];            // "" until a newer release is found
static char asset[512];

void openpak_update_self(const char *argv0) {
    const char *dot = argv0 ? strrchr(argv0, '.') : NULL;
    if (dot && strcmp(dot, ".nro") == 0) snprintf(self, sizeof(self), "%s", argv0);
    else snprintf(self, sizeof(self), "sdmc:/switch/" OPENPAK_ASSET);   // launched some other way
}

const char *openpak_update_tag(void) { return tag; }

// OpenPak installed from its own eShop runs as application 01fe000000000000. Its updates then
// come through the console's update path (a patch title, 01fe000000000800), like any store
// title; rewriting the unpacked NRO from GitHub would only fork it from what the console
// thinks is installed. Under hbmenu the program id is the Album's or a taken-over game's.
bool openpak_is_store_title(void) {
#ifdef OPENPAK_HOST_TEST
    return false;
#else
    u64 id = 0;
    return R_SUCCEEDED(svcGetInfo(&id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) && id == 0x01FE000000000000ULL;
#endif
}

void openpak_update_check(char *note, int notelen) {
    tag[0] = asset[0] = '\0';
    if (openpak_is_store_title()) {
        if (note && notelen > 0) note[0] = '\0';
        return;
    }
    if (!self[0]) openpak_update_self(NULL);
    size_t len = 0;
    char err[256] = "";
    char *body = openpak_http_get(OPENPAK_RELEASE_URL, 256 * 1024, &len, err, sizeof(err));
    if (!body) { snprintf(note, notelen, "release check: %s", err); return; }
    struct json_object *root = json_tokener_parse(body);
    free(body);
    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root) json_object_put(root);
        snprintf(note, notelen, "release check: the answer is not a release");
        return;
    }
    struct json_object *v = NULL, *assets = NULL;
    const char *newest = NULL;
    if (json_object_object_get_ex(root, "tag_name", &v) && json_object_is_type(v, json_type_string))
        newest = json_object_get_string(v);
    if (!newest) {
        snprintf(note, notelen, "release check: the answer names no release");
    } else if (!openpak_update_newer(newest, OPENPAK_VERSION)) {
        snprintf(note, notelen, "release check: " OPENPAK_VERSION " is current (newest %s)", newest);
    } else if (!json_object_object_get_ex(root, "assets", &assets) ||
               !json_object_is_type(assets, json_type_array)) {
        snprintf(note, notelen, "release check: %s carries no assets", newest);
    } else {
        snprintf(note, notelen, "release check: %s has no " OPENPAK_ASSET, newest);
        for (size_t i = 0; i < json_object_array_length(assets); ++i) {
            struct json_object *a = json_object_array_get_idx(assets, i), *u = NULL;
            if (!json_object_is_type(a, json_type_object)) continue;
            if (!json_object_object_get_ex(a, "name", &v) ||
                !json_object_is_type(v, json_type_string) ||
                strcmp(json_object_get_string(v), OPENPAK_ASSET) != 0) continue;
            if (!json_object_object_get_ex(a, "browser_download_url", &u) ||
                !json_object_is_type(u, json_type_string)) continue;
            const char *url = json_object_get_string(u);
            if (!openpak_update_asset_ok(url, OPENPAK_ASSET)) {
                snprintf(note, notelen, "release check: %s is not a release asset of this repository", url);
                break;
            }
            snprintf(tag, sizeof(tag), "%s", newest[0] == 'v' ? newest + 1 : newest);
            snprintf(asset, sizeof(asset), "%s", url);
            snprintf(note, notelen, "release check: %s available", tag);
            break;
        }
    }
    json_object_put(root);
}

// The release asset, straight to a file beside the running NRO. Redirects are followed (GitHub
// serves the bytes from its object storage) but only to https, and only so many.
static bool download(const char *path, char *msg, int msglen) {
    if (R_FAILED(socketInitializeDefault())) { snprintf(msg, msglen, "no network"); return false; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    FILE *f = fopen(path, "wb");
    CURL *c = f ? curl_easy_init() : NULL;
    CURLcode rc = CURLE_FAILED_INIT;
    long status = 0;
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, asset);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "openpak-nro/" OPENPAK_VERSION);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
#if CURL_AT_LEAST_VERSION(7, 85, 0)
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);   // a stalled download gives up
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(c, CURLOPT_MAXFILESIZE, (long)NRO_MAX);
        rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(c);
    }
    bool written = f && fflush(f) == 0 && fclose(f) == 0;
    curl_global_cleanup();
    socketExit();
    if (rc != CURLE_OK || status / 100 != 2 || !written) {
        remove(path);
        if (rc != CURLE_OK) snprintf(msg, msglen, "download failed: %s", curl_easy_strerror(rc));
        else if (!written) snprintf(msg, msglen, "could not write to the SD card");
        else snprintf(msg, msglen, "download failed: HTTP %ld", status);
        return false;
    }
    return true;
}

int openpak_update_apply(char *msg, int msglen) {
    if (!asset[0]) { snprintf(msg, msglen, "There is no update to download."); return -1; }
    char temp[352];
    snprintf(temp, sizeof(temp), "%s.openpak-new", self);
    if (!download(temp, msg, msglen)) return -1;
    if (!openpak_update_is_nro(temp)) {
        remove(temp);
        snprintf(msg, msglen, "What arrived is not an OpenPak build; nothing was replaced.");
        return -1;
    }
    // romfs is read from the running NRO on demand, so let go of it before the file underneath
    // changes. Nothing else is read from romfs after this: the tool either restarts or exits.
    romfsExit();
    // The SD card's filesystem will not rename onto an existing name, so the old build goes
    // first. The new one is already written and beside it if this stops here.
    if (remove(self) != 0 && errno != ENOENT) {
        snprintf(msg, msglen, "Could not replace %s. The new build is beside it as %s.", self, temp);
        return -1;
    }
    if (rename(temp, self) != 0) {
        snprintf(msg, msglen, "Could not put the new build at %s. It is there as %s.", self, temp);
        return -1;
    }
    if (!envHasNextLoad()) {
        snprintf(msg, msglen, "OpenPak %s installed. Close and open OpenPak to use it.", tag);
        return 0;
    }
    envSetNextLoad(self, self);
    snprintf(msg, msglen, "OpenPak %s installed. Restarting...", tag);
    return 1;
}
#endif
