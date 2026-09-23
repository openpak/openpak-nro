// SPDX-License-Identifier: AGPL-3.0-only
#include "crash.h"
#include "installtrust.h"

#include <curl/curl.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define REPORT_DIR     "/switch/openpak/reports"
#define REPORT_CONSENT "/switch/openpak/reports.txt"
#ifndef OPENPAK_VERSION
#define OPENPAK_VERSION "unknown"
#endif

static char os_name[96] = "Horizon";
static const char *ui_name = "";

static void save(const char *action, const char *error_code, const char *message,
                 const report_field *extra, int nextra) {
    report_field f[8] = {{"action", action}, {"ui", ui_name}};
    int n = 2;
    for (int i = 0; i < nextra && n < 8; i++) f[n++] = extra[i];
    static char meta[REPORT_META_MAX];   // static: the crash hook runs on a small stack
    if (report_meta(meta, sizeof(meta), OPENPAK_VERSION, os_name, NULL, error_code, message, f, n))
        report_queue_save(REPORT_DIR, meta);
}

// libnx calls this when the NRO itself crashes. It only writes the report; Atmosphère's own
// crash handling still runs after it, and the report is offered on the next launch.
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
extern char __start__[];

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    if (report_consent_load(REPORT_CONSENT) == REPORT_NEVER) return;
    static char code[16], pc[24], lr[24], far[24], esr[16];
    u64 base = (u64)__start__;
    snprintf(code, sizeof(code), "0x%x", ctx->error_desc);
    snprintf(pc, sizeof(pc), "0x%lx", ctx->pc.x - base);
    snprintf(lr, sizeof(lr), "0x%lx", ctx->lr.x - base);
    snprintf(far, sizeof(far), "0x%lx", ctx->far.x);
    snprintf(esr, sizeof(esr), "0x%x", ctx->esr);
    report_field f[] = {{"pc_offset", pc}, {"lr_offset", lr}, {"far", far}, {"esr", esr}};
    save("crash", code, "The OpenPak NRO crashed", f, 4);
}

void openpak_report_init(const char *ui) {
    ui_name = ui;
    char fw[32] = "";
    openpak_firmware_supported(fw, sizeof(fw));
    u64 ams = 0;   // config item 65000: Atmosphère (exosphère) API version, major.minor.micro in the top bytes
    if (R_SUCCEEDED(splInitialize())) {
        if (R_FAILED(splGetConfig((SplConfigItem)65000, &ams))) ams = 0;
        splExit();
    }
    if (ams)
        snprintf(os_name, sizeof(os_name), "Horizon %s, Atmosphere %u.%u.%u", fw,
                 (unsigned)(ams >> 56) & 0xff, (unsigned)(ams >> 48) & 0xff, (unsigned)(ams >> 40) & 0xff);
    else
        snprintf(os_name, sizeof(os_name), "Horizon %s", fw);
}

void openpak_report_failure(const char *action, const char *error_code, const char *message) {
    if (openpak_report_consent() != REPORT_NEVER) save(action, error_code, message, NULL, 0);
}

int openpak_report_pending(void) {
    char names[REPORT_QUEUE_MAX][32];
    return report_queue_list(REPORT_DIR, names, REPORT_QUEUE_MAX);
}

report_consent openpak_report_consent(void) { return report_consent_load(REPORT_CONSENT); }

void openpak_report_set_consent(report_consent c) {
    report_consent_save(REPORT_CONSENT, c);
    if (c == REPORT_NEVER) openpak_report_discard_all();
}

void openpak_report_discard_all(void) {
    char names[REPORT_QUEUE_MAX][32];
    int n = report_queue_list(REPORT_DIR, names, REPORT_QUEUE_MAX);
    for (int i = 0; i < n; i++) report_queue_drop(REPORT_DIR, names[i]);
}

static size_t discard_body(char *p, size_t size, size_t n, void *u) { (void)p; (void)u; return size * n; }

// One POST through libcurl, whose Switch build does TLS in the console's own ssl service.
// The NRO holds no OpenPak account token, so no Authorization header is sent.
static bool post(const char *meta, char *err, int errlen) {
    char boundary[48];
    snprintf(boundary, sizeof(boundary), "openpak-%016lx", randomGet64());
    size_t len = 0;
    char *body = report_multipart(boundary, meta, NULL, NULL, 0, &len);
    if (!body) { snprintf(err, errlen, "out of memory"); return false; }
    char ctype[96];
    snprintf(ctype, sizeof(ctype), "Content-Type: multipart/form-data; boundary=%s", boundary);
    struct curl_slist *hdr = curl_slist_append(NULL, ctype);
    hdr = curl_slist_append(hdr, "Expect:");

    bool ok = false;
    CURL *c = curl_easy_init();
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, REPORT_URL);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "openpak-nro/" OPENPAK_VERSION);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard_body);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
        CURLcode rc = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        ok = rc == CURLE_OK && status >= 200 && status < 300;
        if (rc != CURLE_OK) snprintf(err, errlen, "%s", curl_easy_strerror(rc));
        else if (!ok) snprintf(err, errlen, "server answered %ld", status);
        curl_easy_cleanup(c);
    } else {
        snprintf(err, errlen, "could not start the upload");
    }
    curl_slist_free_all(hdr);
    free(body);
    return ok;
}

int openpak_report_send_all(char *msg, int msglen) {
    char names[REPORT_QUEUE_MAX][32];
    int n = report_queue_list(REPORT_DIR, names, REPORT_QUEUE_MAX), sent = 0;
    char err[96] = "";
    if (R_FAILED(socketInitializeDefault())) {
        snprintf(msg, msglen, "Report not sent: no network");
        return 0;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for (int i = 0; i < n; i++) {
        char *meta = report_queue_read(REPORT_DIR, names[i]);
        if (!meta) continue;
        bool ok = post(meta, err, sizeof(err));
        free(meta);
        if (!ok) break;                  // the network is down or refusing; keep the rest too
        report_queue_drop(REPORT_DIR, names[i]);
        sent++;
    }
    curl_global_cleanup();
    socketExit();
    if (sent == n) snprintf(msg, msglen, "Report sent. Thank you.");
    else snprintf(msg, msglen, "Report not sent (%s). It will be offered again.", err);
    return sent;
}

bool openpak_report_offer(char *status, int len, const char *lead) {
    if (!openpak_report_pending()) return false;
    char first[192];
    snprintf(first, sizeof(first), "%s", lead);
    switch (openpak_report_consent()) {
    case REPORT_NEVER:
        openpak_report_discard_all();
        return false;
    case REPORT_ALWAYS: {
        char note[128];
        openpak_report_send_all(note, sizeof(note));
        snprintf(status, len, "%s  %s", first, note);
        return false;
    }
    default:
        snprintf(status, len, "%s  Send a report to OpenPak?", first);
        return true;
    }
}
