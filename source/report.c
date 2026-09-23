// SPDX-License-Identifier: AGPL-3.0-only
#include "report.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

bool report_json_escape(char *out, size_t cap, const char *s) {
    size_t n = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        if (c == '"' || c == '\\') snprintf(esc, sizeof(esc), "\\%c", c);
        else if (c == '\n') snprintf(esc, sizeof(esc), "\\n");
        else if (c == '\r') snprintf(esc, sizeof(esc), "\\r");
        else if (c == '\t') snprintf(esc, sizeof(esc), "\\t");
        else if (c < 0x20 || c == 0x7f) snprintf(esc, sizeof(esc), "\\u%04x", c);
        else { esc[0] = (char)c; esc[1] = '\0'; }   // UTF-8 passes through
        size_t len = strlen(esc);
        if (n + len >= cap) { if (cap) out[0] = '\0'; return false; }
        memcpy(out + n, esc, len);
        n += len;
    }
    if (!cap) return false;
    out[n] = '\0';
    return true;
}

// Appends "key":"value" (with a leading comma unless first) to out.
static bool put(char *out, size_t cap, size_t *n, const char *key, const char *value, bool first) {
    char k[128], v[2048];
    if (!report_json_escape(k, sizeof(k), key) || !report_json_escape(v, sizeof(v), value)) return false;
    int w = snprintf(out + *n, cap - *n, "%s\"%s\":\"%s\"", first ? "" : ",", k, v);
    if (w < 0 || (size_t)w >= cap - *n) return false;
    *n += (size_t)w;
    return true;
}

static bool lit(char *out, size_t cap, size_t *n, const char *s) {
    size_t len = strlen(s);
    if (*n + len >= cap) return false;
    memcpy(out + *n, s, len + 1);
    *n += len;
    return true;
}

bool report_meta(char *out, size_t cap, const char *version, const char *os,
                 const char *title_id, const char *error_code, const char *message,
                 const report_field *fields, int nfields) {
    size_t n = 0;
    bool ok = cap > 0 && lit(out, cap, &n, "{")
        && put(out, cap, &n, "source", "nro", true)
        && put(out, cap, &n, "version", version ? version : "", false)
        && put(out, cap, &n, "os", os ? os : "", false);
    if (ok && title_id && title_id[0])     ok = put(out, cap, &n, "title_id", title_id, false);
    if (ok && error_code && error_code[0]) ok = put(out, cap, &n, "error_code", error_code, false);
    if (ok && message && message[0])       ok = put(out, cap, &n, "message", message, false);
    if (ok && nfields > 0) {
        ok = lit(out, cap, &n, ",\"fields\":{");
        for (int i = 0; ok && i < nfields; i++)
            ok = put(out, cap, &n, fields[i].key, fields[i].value ? fields[i].value : "", i == 0);
        ok = ok && lit(out, cap, &n, "}");
    }
    ok = ok && lit(out, cap, &n, "}");
    if (!ok && cap) out[0] = '\0';
    return ok;
}

char *report_multipart(const char *boundary, const char *meta,
                       const char *att_name, const void *att, size_t att_len, size_t *out_len) {
    char head[512], mid[512], tail[128];
    int h = snprintf(head, sizeof(head),
                     "--%s\r\nContent-Disposition: form-data; name=\"meta\"\r\n"
                     "Content-Type: application/json\r\n\r\n", boundary);
    int m = att ? snprintf(mid, sizeof(mid),
                           "\r\n--%s\r\nContent-Disposition: form-data; name=\"attachment\"; filename=\"%s\"\r\n"
                           "Content-Type: application/octet-stream\r\n\r\n",
                           boundary, att_name ? att_name : "report.log") : 0;
    int t = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", boundary);
    if (h < 0 || m < 0 || t < 0 || h >= (int)sizeof(head) || m >= (int)sizeof(mid) || t >= (int)sizeof(tail))
        return NULL;
    size_t ml = strlen(meta), total = (size_t)h + ml + (size_t)m + (att ? att_len : 0) + (size_t)t;
    char *body = malloc(total + 1), *p = body;
    if (!body) return NULL;
    memcpy(p, head, (size_t)h); p += h;
    memcpy(p, meta, ml);        p += ml;
    if (att) { memcpy(p, mid, (size_t)m); p += m; memcpy(p, att, att_len); p += att_len; }
    memcpy(p, tail, (size_t)t); p += t;
    *p = '\0';
    *out_len = total;
    return body;
}

static bool is_report(const char *name) {
    unsigned seq;
    char end[8];
    return strlen(name) == 14 && sscanf(name, "r%8u%7s", &seq, end) == 2 && strcmp(end, ".json") == 0;
}

static int by_name(const void *a, const void *b) { return strcmp(a, b); }

int report_queue_list(const char *dir, char names[][32], int max) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    for (struct dirent *e; n < max && (e = readdir(d)) != NULL;)
        if (is_report(e->d_name)) memcpy(names[n++], e->d_name, 15);   // 14 chars + NUL
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), by_name);   // zero-padded: name order is age order
    return n;
}

void report_queue_drop(const char *dir, const char *name) {
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    remove(path);
}

char *report_queue_read(const char *dir, const char *name) {
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(REPORT_META_MAX + 1);
    size_t n = buf ? fread(buf, 1, REPORT_META_MAX, f) : 0;
    fclose(f);
    if (buf) buf[n] = '\0';
    return buf;
}

bool report_queue_save(const char *dir, const char *meta) {
    char buf[320];
    snprintf(buf, sizeof(buf), "%s", dir);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = '\0'; mkdir(buf, 0777); *p = '/'; }
    mkdir(buf, 0777);

    // Room for one more, oldest first out. Listing more than the cap catches leftovers.
    char names[REPORT_QUEUE_MAX * 4][32];
    int n = report_queue_list(dir, names, REPORT_QUEUE_MAX * 4);
    unsigned next = 1;
    if (n) { sscanf(names[n - 1], "r%8u", &next); next++; }
    for (int i = 0; i + REPORT_QUEUE_MAX - 1 < n; i++) report_queue_drop(dir, names[i]);

    char path[320];
    snprintf(path, sizeof(path), "%s/r%08u.json", dir, next % 100000000u);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fputs(meta, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) remove(path);
    return ok;
}

report_consent report_consent_parse(const char *s) {
    if (!s) return REPORT_ASK;
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) n--;
    if (n == 6 && strncasecmp(s, "always", 6) == 0) return REPORT_ALWAYS;
    if (n == 5 && strncasecmp(s, "never", 5) == 0) return REPORT_NEVER;
    return REPORT_ASK;
}

report_consent report_consent_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return REPORT_ASK;
    char line[32] = "";
    if (!fgets(line, sizeof(line), f)) line[0] = '\0';
    fclose(f);
    return report_consent_parse(line);
}

bool report_consent_save(const char *path, report_consent c) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(c == REPORT_ALWAYS ? "always\n" : c == REPORT_NEVER ? "never\n" : "ask\n", f);
    return fclose(f) == 0;
}
