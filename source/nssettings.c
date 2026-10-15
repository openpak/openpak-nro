#include "nssettings.h"

#include "ceiling.h"
#include "hosts.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define NS_SECTION "[ns.notification]"

typedef struct { const char *key, *value; } ns_setting;
static const ns_setting settings[] = {
    {"enable_version_list", "u8!0x1"},
    {"debug_version_list_waiting_limit", "u32!0x0"},
};
#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(*settings)))

static bool is_ws(char c) { return c == ' ' || c == '\t'; }

// The line starting at s (end e): content end excludes "\r\n" or "\n"; next is where the
// following line begins.
static void line_bounds(const char *buf, size_t s, size_t len, size_t *content_end, size_t *next) {
    size_t i = s;
    while (i < len && buf[i] != '\n') i++;
    *next = i < len ? i + 1 : len;
    *content_end = i;
    if (i > s && buf[i - 1] == '\r') (*content_end)--;
}

static bool is_header(const char *buf, size_t s, size_t e, const char *name) {
    while (s < e && is_ws(buf[s])) s++;
    while (e > s && is_ws(buf[e - 1])) e--;
    return e - s == strlen(name) && memcmp(buf + s, name, e - s) == 0;
}

static bool starts_section(const char *buf, size_t s, size_t e) {
    while (s < e && is_ws(buf[s])) s++;
    return s < e && buf[s] == '[';
}

// Offset just past "key<ws>=<ws>" when the line is that key, else 0.
static size_t key_value_start(const char *buf, size_t s, size_t e, const char *key) {
    size_t kl = strlen(key);
    while (s < e && is_ws(buf[s])) s++;
    if (e - s < kl || memcmp(buf + s, key, kl) != 0) return 0;
    s += kl;
    while (s < e && is_ws(buf[s])) s++;
    if (s >= e || buf[s] != '=') return 0;
    s++;
    while (s < e && is_ws(buf[s])) s++;
    return s;
}

static bool is_blank_or_comment(const char *buf, size_t s, size_t e) {
    while (s < e && is_ws(buf[s])) s++;
    return s >= e || buf[s] == ';' || buf[s] == '#';
}

// Replace buf[at, at+remove) with text, growing/shrinking in place. Returns false if cap is hit.
static bool splice(char *buf, size_t *len, size_t cap, size_t at, size_t remove,
                   const char *text, size_t tl) {
    if (*len - remove + tl > cap) return false;
    memmove(buf + at + tl, buf + at + remove, *len - at - remove);
    memcpy(buf + at, text, tl);
    *len = *len - remove + tl;
    return true;
}

// Make `key` equal `value` inside [ns.notification]. *anchor is the offset after the line the
// previous setting ended on (0 = none yet) and is updated to this setting's line end.
static bool ensure_key(char *buf, size_t *len, size_t cap, const char *eol,
                       const ns_setting *st, size_t *anchor) {
    bool in_sec = false, found_sec = false, found_key = false;
    size_t sec_end = 0;                       // after the header, then after each key line
    size_t pos = 0, last_line_end = 0;
    while (pos < *len) {
        size_t ce, next;
        line_bounds(buf, pos, *len, &ce, &next);
        if (starts_section(buf, pos, ce)) {
            in_sec = is_header(buf, pos, ce, NS_SECTION);
            if (in_sec) { found_sec = true; sec_end = next; }
        } else if (in_sec && !is_blank_or_comment(buf, pos, ce)) {
            sec_end = next;
            size_t vs = key_value_start(buf, pos, ce, st->key);
            if (vs) {
                size_t ve = ce;
                while (ve > vs && is_ws(buf[ve - 1])) ve--;
                size_t vl = strlen(st->value);
                if (!(ve - vs == vl && memcmp(buf + vs, st->value, vl) == 0)) {
                    if (!splice(buf, len, cap, vs, ve - vs, st->value, vl)) return false;
                    ce = ce - (ve - vs) + vl;
                    next = next - (ve - vs) + vl;
                }
                found_key = true;
                last_line_end = next;
            }
        }
        pos = next;
    }
    if (found_key) { *anchor = last_line_end; return true; }

    char line[96];
    snprintf(line, sizeof(line), "%s = %s%s", st->key, st->value, eol);
    size_t at;
    char head[48] = "";
    if (found_sec) {
        // The previous setting's line end, when there is one, keeps the pair together.
        at = (*anchor && *anchor <= *len) ? *anchor : sec_end;
    } else {
        at = *len;
        if (at > 0) {
            // end the last line, then a blank line before the new section
            snprintf(head, sizeof(head), "%s%s", buf[at - 1] == '\n' ? "" : eol, eol);
        }
        char sec[64];
        snprintf(sec, sizeof(sec), "%s%s", NS_SECTION, eol);
        char full[200];
        int fn = snprintf(full, sizeof(full), "%s%s%s", head, sec, line);
        if (!splice(buf, len, cap, at, 0, full, (size_t)fn)) return false;
        *anchor = *len;
        return true;
    }
    // An insertion point at end of file with no final newline needs the line ended first.
    if (at > 0 && buf[at - 1] != '\n') {
        snprintf(head, sizeof(head), "%s", eol);
    }
    char full[160];
    int fn = snprintf(full, sizeof(full), "%s%s", head, line);
    if (!splice(buf, len, cap, at, 0, full, (size_t)fn)) return false;
    *anchor = at + (size_t)fn;
    return true;
}

size_t openpak_nssettings_transform(const char *in, size_t inlen, char *out, size_t cap) {
    if (inlen > cap) return (size_t)-1;
    if (inlen) memcpy(out, in, inlen);
    size_t len = inlen;
    const char *eol = "\n";
    for (size_t i = 0; i < inlen; i++)
        if (in[i] == '\n') { if (i > 0 && in[i - 1] == '\r') eol = "\r\n"; break; }
    size_t anchor = 0;
    for (int i = 0; i < SETTING_COUNT; i++)
        if (!ensure_key(out, &len, cap, eol, &settings[i], &anchor)) return (size_t)-1;
    return len;
}

// ---- the console's file ------------------------------------------------------------------

#define NS_FILE    "/atmosphere/config/system_settings.ini"
#define NS_DIR     "/switch/openpak"
#define NS_BACKUPS "/switch/openpak/nssettings-backup"
#define NS_MARKER  "/switch/openpak/nssettings-backup-done"
#define NS_LOG     "/switch/openpak/nssettings.log"

static void ns_log(const char *fmt, ...) {
    char p[320];
    snprintf(p, sizeof(p), "%s" NS_DIR, openpak_root);
    mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s" NS_LOG, openpak_root);
    FILE *f = fopen(p, "ab");
    if (!f) return;
    fprintf(f, "%lld ", (long long)time(NULL));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int fail(char *err, size_t errsz, const char *msg) {
    snprintf(err, errsz, "%s", msg);
    ns_log("error: %s", msg);
    return -1;
}

int openpak_nssettings_ensure(char *err, size_t errsz) {
    char path[320], tmp[340], marker[320];
    snprintf(tmp, sizeof(tmp), "%s/switch", openpak_root);
    mkdir(tmp, 0777);
    snprintf(tmp, sizeof(tmp), "%s" NS_DIR, openpak_root);
    mkdir(tmp, 0777);
    snprintf(tmp, sizeof(tmp), "%s/atmosphere", openpak_root);
    mkdir(tmp, 0777);
    snprintf(tmp, sizeof(tmp), "%s/atmosphere/config", openpak_root);
    mkdir(tmp, 0777);
    snprintf(path, sizeof(path), "%s" NS_FILE, openpak_root);
    snprintf(marker, sizeof(marker), "%s" NS_MARKER, openpak_root);

    char *in = NULL;
    size_t inlen = 0;
    bool existed = false;
    FILE *f = fopen(path, "rb");
    if (f) {
        existed = true;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > (4 << 20)) { fclose(f); return fail(err, errsz, "system_settings.ini unreadable or too large, left alone"); }
        in = malloc((size_t)sz + 1);
        if (!in) { fclose(f); return fail(err, errsz, "No memory for system_settings.ini"); }
        inlen = fread(in, 1, (size_t)sz, f);
        fclose(f);
        if (inlen != (size_t)sz) { free(in); return fail(err, errsz, "Could not read system_settings.ini, left alone"); }
    }
    size_t cap = inlen + NSSETTINGS_GROWTH;
    char *out = malloc(cap);
    if (!out) { free(in); return fail(err, errsz, "No memory for system_settings.ini"); }
    size_t outlen = openpak_nssettings_transform(in, inlen, out, cap);
    int rc = -1;
    if (outlen == (size_t)-1) {
        fail(err, errsz, "Could not edit system_settings.ini, left alone");
    } else if (existed && outlen == inlen && memcmp(in, out, inlen) == 0) {
        ns_log("system_settings.ini already has the version list keys");
        rc = 0;
    } else {
        char werr[160] = "";
        bool ok = true;
        FILE *m = fopen(marker, "rb");
        if (m) {
            fclose(m);
        } else {
            char b[320] = "";
            if (existed) {
                snprintf(b, sizeof(b), "%s" NS_BACKUPS, openpak_root);
                mkdir(b, 0777);
                snprintf(b, sizeof(b), "%s" NS_BACKUPS "/system_settings-%lld.ini", openpak_root, (long long)time(NULL));
                FILE *bf = fopen(b, "wb");
                ok = bf && fwrite(in, 1, inlen, bf) == inlen;
                if (bf && fclose(bf) != 0) ok = false;
            }
            if (!ok) {
                fail(err, errsz, "Could not back up system_settings.ini, left alone");
            } else {
                FILE *mk = fopen(marker, "wb");
                if (mk) { fprintf(mk, "%s\n", existed ? b : "(file did not exist)"); fclose(mk); }
            }
        }
        if (ok) {
            // ponytail: ceiling.c's helper is the shared temp-file-plus-rename writer
            if (!openpak_write_file_atomic(path, out, outlen, werr, sizeof(werr))) {
                snprintf(err, errsz, "Could not write system_settings.ini (%s)", werr);
                ns_log("error: %s", err);
            } else {
                ns_log("system_settings.ini %s: enable_version_list=1, debug_version_list_waiting_limit=0 (applies at next boot)",
                       existed ? "updated" : "created");
                rc = 1;
            }
        }
    }
    free(in);
    free(out);
    return rc;
}
