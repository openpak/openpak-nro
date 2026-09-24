// SPDX-License-Identifier: AGPL-3.0-only
#include "ceiling.h"
#include "ed25519.h"
#include "hosts.h"

#include <json-c/json.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef OPENPAK_HOST_TEST
#include <openssl/sha.h>
static void sha256(uint8_t out[32], const void *data, size_t n) { SHA256(data, n, out); }
#else
#include <switch.h>
static void sha256(uint8_t out[32], const void *data, size_t n) { sha256CalculateHash(out, data, n); }
#endif

#ifndef OPENPAK_PLATFORM
#define OPENPAK_PLATFORM "switch"
#endif

// docs/signed-ceiling.md, "Pinned public key". The private half is owner-local.
static const openpak_pinned_key pinned[] = {
    {"36e8bcdd93c2c1a1d7e5d87bbba05a7a4f97882131cf6878f1c053a25a377fe4",
     {0x0f, 0xd2, 0xa2, 0x66, 0x08, 0x68, 0xe5, 0x3d, 0x20, 0xfc, 0x81, 0x1e, 0x3f, 0x15, 0x71, 0x0c,
      0xb1, 0xad, 0x15, 0x7b, 0xa7, 0xda, 0x80, 0x19, 0xf7, 0xa5, 0x9e, 0x8a, 0xd6, 0x04, 0xd8, 0x7c}},
};
const openpak_pinned_key *openpak_ceiling_keys = pinned;
int openpak_ceiling_key_count = (int)(sizeof(pinned) / sizeof(*pinned));

// A ceiling envelope is a few hundred bytes; a profile a few KB. Anything near these is not one.
#define ENVELOPE_MAX (64 * 1024)
#define PROFILE_MAX  (256 * 1024)

static bool fail(char *err, int errlen, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)errlen, fmt, ap);
    va_end(ap);
    return false;
}

static const char *path_of(const char *rel) {
    static char buf[4][320];
    static int next;
    char *b = buf[next++ % 4];
    snprintf(b, sizeof(buf[0]), "%s%s", openpak_root, rel);
    return b;
}

char *openpak_read_file(const char *path, size_t max, size_t *len_out) {
    *len_out = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || (size_t)len > max) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *len_out = got;
    return buf;
}

bool openpak_write_file_atomic(const char *path, const void *data, size_t len,
                               char *err, int errlen) {
    char dir[320];
    snprintf(dir, sizeof(dir), "%s/switch", openpak_root);         mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/switch/openpak", openpak_root); mkdir(dir, 0777);
    char tmp[340];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return fail(err, errlen, "cannot write %s", tmp);
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fflush(f) == 0 && ok;
    fclose(f);
    if (!ok) { remove(tmp); return fail(err, errlen, "short write on %s", tmp); }
    chmod(tmp, 0600);
    remove(path);                       // FAT: rename does not replace
    if (rename(tmp, path) != 0) return fail(err, errlen, "cannot replace %s", path);
    return true;
}

// ---- base64, hex --------------------------------------------------------------------

static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Standard alphabet, padding required. Returns a malloc'd buffer or NULL.
static uint8_t *b64decode(const char *s, size_t *out_len) {
    size_t n = strlen(s);
    if (n == 0 || n % 4) return NULL;
    uint8_t *out = malloc(n / 4 * 3 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 4) {
        int v[4];
        for (int k = 0; k < 4; k++) {
            if (s[i + k] == '=' && i + 4 == n && k >= 2) v[k] = -2;
            else if ((v[k] = b64val((unsigned char)s[i + k])) < 0) { free(out); return NULL; }
        }
        if (v[2] == -2 && v[3] != -2) { free(out); return NULL; }
        out[o++] = (uint8_t)(v[0] << 2 | v[1] >> 4);
        if (v[2] != -2) out[o++] = (uint8_t)((v[1] & 15) << 4 | v[2] >> 2);
        if (v[3] != -2) out[o++] = (uint8_t)((v[2] & 3) << 6 | v[3]);
    }
    *out_len = o;
    return out;
}

static void hex_of(const uint8_t *in, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02x", in[i]);
}

// ---- families -----------------------------------------------------------------------

bool openpak_family_valid(const char *f) {
    size_t n = strlen(f);
    if (n < 4 || n >= OPENPAK_FAMILY_MAX || f[0] != '.') return false;
    int labels = 0;
    const char *p = f + 1;
    while (*p) {
        const char *e = strchr(p, '.');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 0 || len > 63 || p[0] == '-' || p[len - 1] == '-') return false;
        for (size_t i = 0; i < len; i++) {
            char c = p[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        }
        labels++;
        p += len + (e ? 1 : 0);
        if (e && !*p) return false;     // trailing dot
    }
    return labels >= 2;
}

// A hostname fit for a hosts line: lower-case letters, digits, '-' and '.', nothing else.
// Anything else (a space, a newline) could smuggle a second rule into the file.
static bool hostname_valid(const char *h) {
    size_t n = strlen(h);
    if (n == 0 || n >= OPENPAK_HOST_MAX - 2 || h[0] == '.' || h[n - 1] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        char c = h[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        if (c == '.' && h[i + 1] == '.') return false;
    }
    return true;
}

static bool ipv4_valid(const char *s) {
    int parts = 0;
    while (parts < 4) {
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 4) { v = v * 10 + (*s++ - '0'); digits++; }
        if (digits == 0 || digits > 3 || v > 255) return false;
        parts++;
        if (parts < 4 && *s++ != '.') return false;
    }
    return *s == '\0';
}

bool openpak_ceiling_covers(const openpak_ceiling *c, const char *name) {
    size_t nl = strlen(name);
    for (int i = 0; i < c->count; i++) {
        const char *f = c->families[i];         // ".ea.com"
        size_t fl = strlen(f);
        if (strcmp(name, f + 1) == 0) return true;                       // the apex
        if (nl >= fl && strcmp(name + nl - fl, f) == 0) return true;     // below it, or itself
    }
    return false;
}

// ---- the signed file ----------------------------------------------------------------

static struct json_object *parse_json(const char *s, size_t len) {
    struct json_tokener *tok = json_tokener_new();
    if (!tok) return NULL;
    struct json_object *o = json_tokener_parse_ex(tok, s, (int)len);
    bool ok = o && json_tokener_get_error(tok) == json_tokener_success;
    json_tokener_free(tok);
    if (!ok) { if (o) json_object_put(o); return NULL; }
    return o;
}

static const char *str_of(struct json_object *o, const char *key) {
    struct json_object *v = NULL;
    if (!json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_string)) return NULL;
    return json_object_get_string(v);
}

static bool signed_by_pinned_key(struct json_object *sigs, const uint8_t *payload, size_t plen) {
    size_t n = json_object_array_length(sigs);
    for (size_t i = 0; i < n; i++) {
        struct json_object *s = json_object_array_get_idx(sigs, i);
        if (!json_object_is_type(s, json_type_object)) continue;
        const char *keyid = str_of(s, "keyid"), *sig64 = str_of(s, "sig");
        if (!keyid || !sig64) continue;
        size_t slen = 0;
        uint8_t *sig = b64decode(sig64, &slen);
        if (!sig) continue;
        bool ok = false;
        for (int k = 0; k < openpak_ceiling_key_count && !ok && slen == 64; k++)
            if (strcasecmp(keyid, openpak_ceiling_keys[k].keyid) == 0)
                ok = openpak_ed25519_verify(sig, payload, plen, openpak_ceiling_keys[k].pub);
        free(sig);
        if (ok) return true;
    }
    return false;
}

static bool payload_into(struct json_object *root, openpak_ceiling *out, char *err, int errlen) {
    const char *type = str_of(root, "type");
    if (!type || strcmp(type, "openpak-ceiling") != 0) return fail(err, errlen, "not an openpak-ceiling");
    struct json_object *v = NULL;
    if (!json_object_object_get_ex(root, "version", &v) || !json_object_is_type(v, json_type_int) ||
        json_object_get_int64(v) <= 0)
        return fail(err, errlen, "version is not a positive integer");
    out->version = json_object_get_int64(v);
    if (!str_of(root, "issued")) return fail(err, errlen, "no issued time");

    struct json_object *plats = NULL;
    if (!json_object_object_get_ex(root, "platforms", &plats) || !json_object_is_type(plats, json_type_object))
        return fail(err, errlen, "no platforms");
    bool mine = false;
    json_object_object_foreach(plats, name, list) {
        if (!json_object_is_type(list, json_type_array))
            return fail(err, errlen, "platform %s is not a list", name);
        bool is_mine = strcmp(name, OPENPAK_PLATFORM) == 0;
        mine = mine || is_mine;
        size_t n = json_object_array_length(list);
        for (size_t i = 0; i < n; i++) {
            struct json_object *f = json_object_array_get_idx(list, i);
            const char *fam = json_object_is_type(f, json_type_string) ? json_object_get_string(f) : NULL;
            // One bad entry anywhere rejects the file: a signer that emits garbage for
            // another platform is not trusted for this one either.
            if (!fam || !openpak_family_valid(fam))
                return fail(err, errlen, "platform %s has a malformed family", name);
            if (!is_mine) continue;
            if (out->count == OPENPAK_CEILING_MAX) return fail(err, errlen, "too many families");
            snprintf(out->families[out->count++], OPENPAK_FAMILY_MAX, "%s", fam);
        }
    }
    if (!mine) return fail(err, errlen, "no " OPENPAK_PLATFORM " families");
    return true;
}

bool openpak_ceiling_verify(const char *envelope, size_t len, openpak_ceiling *out,
                            char *err, int errlen) {
    memset(out, 0, sizeof(*out));
    if (len == 0 || len > ENVELOPE_MAX) return fail(err, errlen, "envelope is %zu bytes", len);
    struct json_object *env = parse_json(envelope, len);
    if (!env || !json_object_is_type(env, json_type_object)) {
        if (env) json_object_put(env);
        return fail(err, errlen, "envelope is not JSON");
    }
    bool ok = false;
    uint8_t *payload = NULL;
    size_t plen = 0;
    struct json_object *sigs = NULL, *root = NULL;
    const char *p64 = str_of(env, "payload");
    if (!p64 || !(payload = b64decode(p64, &plen))) { fail(err, errlen, "payload is not base64"); goto out; }
    if (!json_object_object_get_ex(env, "signatures", &sigs) || !json_object_is_type(sigs, json_type_array)) {
        fail(err, errlen, "no signatures");
        goto out;
    }
    // Over the decoded bytes, never a re-serialisation; only then are they parsed.
    if (!signed_by_pinned_key(sigs, payload, plen)) { fail(err, errlen, "no valid signature under a pinned key"); goto out; }
    root = parse_json((const char *)payload, plen);
    if (!root || !json_object_is_type(root, json_type_object)) { fail(err, errlen, "payload is not JSON"); goto out; }
    ok = payload_into(root, out, err, errlen);
out:
    if (root) json_object_put(root);
    free(payload);
    json_object_put(env);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static long long highest_accepted(void) {
    size_t len = 0;
    char *s = openpak_read_file(path_of(OPENPAK_CEILING_VERSION), 64, &len);
    long long v = s ? strtoll(s, NULL, 10) : 0;
    free(s);
    return v > 0 ? v : 0;
}

bool openpak_ceiling_load_cached(openpak_ceiling *out, char *err, int errlen) {
    err[0] = '\0';
    size_t len = 0;
    char *env = openpak_read_file(path_of(OPENPAK_CEILING_CACHE), ENVELOPE_MAX, &len);
    if (!env) { memset(out, 0, sizeof(*out)); return false; }
    char why[128];
    bool ok = openpak_ceiling_verify(env, len, out, why, sizeof(why));
    free(env);
    if (!ok) return fail(err, errlen, "cached ceiling ignored: %s", why);
    long long high = highest_accepted();
    if (out->version < high) {
        memset(out, 0, sizeof(*out));
        return fail(err, errlen, "cached ceiling is older than version %lld already accepted", high);
    }
    return true;
}

bool openpak_ceiling_accept(const char *envelope, size_t len, openpak_ceiling *out,
                            char *err, int errlen) {
    char why[128];
    if (!openpak_ceiling_verify(envelope, len, out, why, sizeof(why)))
        return fail(err, errlen, "fetched ceiling rejected: %s", why);
    long long high = highest_accepted();
    if (out->version < high) {
        long long got = out->version;
        memset(out, 0, sizeof(*out));
        return fail(err, errlen, "fetched ceiling version %lld is older than %lld", got, high);
    }
    // Envelope first: a crash between the two writes leaves a newer cache than the recorded
    // high-water mark, which still loads; the other order would strand the cache.
    if (!openpak_write_file_atomic(path_of(OPENPAK_CEILING_CACHE), envelope, len, err, errlen)) return false;
    if (out->version > high) {
        char num[32];
        int n = snprintf(num, sizeof(num), "%lld\n", out->version);
        if (!openpak_write_file_atomic(path_of(OPENPAK_CEILING_VERSION), num, (size_t)n, err, errlen)) return false;
    }
    return true;
}

// ---- the profile --------------------------------------------------------------------

static void note_drop(char *dropped, int cap, const char *name) {
    if (!dropped || cap <= 0) return;
    size_t n = strlen(dropped);
    if (n + 1 >= (size_t)cap) return;
    snprintf(dropped + n, (size_t)cap - n, "%s%s", n ? ", " : "", name);
}

static bool add_rule(openpak_rule *rules, int *count, int cap, const char *host, const char *addr) {
    if (*count >= cap) return false;
    snprintf(rules[*count].host, OPENPAK_HOST_MAX, "%s", host);
    snprintf(rules[*count].address, OPENPAK_ADDR_MAX, "%s", addr);
    (*count)++;
    return true;
}

openpak_policy *openpak_profile_policy(const char *json, size_t len, const openpak_ceiling *c,
                                       openpak_source src, char *dropped, int droppedlen,
                                       char *err, int errlen) {
    if (dropped && droppedlen > 0) dropped[0] = '\0';
    if (len == 0 || len > PROFILE_MAX) { fail(err, errlen, "profile is %zu bytes", len); return NULL; }
    struct json_object *root = parse_json(json, len);
    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root) json_object_put(root);
        fail(err, errlen, "profile is not JSON");
        return NULL;
    }
    openpak_policy *p = NULL;
    openpak_rule *rules = NULL;
    struct json_object *v = NULL, *redirect = NULL, *arr = NULL;

    if (json_object_object_get_ex(root, "version", &v) && json_object_get_int(v) != 1) {
        fail(err, errlen, "profile version %d is not 1", json_object_get_int(v));
        goto bad;
    }
    const char *platform = str_of(root, "platform");
    if (!platform || strcmp(platform, OPENPAK_PLATFORM) != 0) {
        fail(err, errlen, "profile is for %s, not " OPENPAK_PLATFORM, platform ? platform : "nothing");
        goto bad;
    }
    if (!json_object_object_get_ex(root, "redirect", &redirect) || !json_object_is_type(redirect, json_type_object)) {
        fail(err, errlen, "profile has no redirect section");
        goto bad;
    }
    int cap = OPENPAK_RULES_MAX, count = 0;
    rules = calloc((size_t)cap, sizeof(*rules));
    if (!rules) { fail(err, errlen, "out of memory"); goto bad; }

    // A family is written the way the bundle and the compiled list write one: "*.family"
    // for everything below it, then the apex, which dns_mitm's wildcard does not answer.
    if (json_object_object_get_ex(redirect, "suffixes", &arr) && json_object_is_type(arr, json_type_array)) {
        size_t n = json_object_array_length(arr);
        for (size_t i = 0; i < n; i++) {
            struct json_object *e = json_object_array_get_idx(arr, i);
            const char *f = json_object_is_type(e, json_type_string) ? json_object_get_string(e) : "?";
            if (!openpak_family_valid(f) || !openpak_ceiling_covers(c, f)) { note_drop(dropped, droppedlen, f); continue; }
            char wild[OPENPAK_HOST_MAX];
            snprintf(wild, sizeof(wild), "*%s", f);
            if (!add_rule(rules, &count, cap, wild, "") || !add_rule(rules, &count, cap, f + 1, "")) {
                fail(err, errlen, "profile carries more than %d rules", cap);
                goto bad;
            }
        }
    }
    if (json_object_object_get_ex(redirect, "exact", &arr) && json_object_is_type(arr, json_type_array)) {
        size_t n = json_object_array_length(arr);
        for (size_t i = 0; i < n; i++) {
            struct json_object *e = json_object_array_get_idx(arr, i);
            const char *h = json_object_is_type(e, json_type_string) ? json_object_get_string(e) : "?";
            if (!hostname_valid(h) || !openpak_ceiling_covers(c, h)) { note_drop(dropped, droppedlen, h); continue; }
            if (!add_rule(rules, &count, cap, h, "")) { fail(err, errlen, "profile carries more than %d rules", cap); goto bad; }
        }
    }
    // Overrides go in as their own rules; hosts.c writes them after the wildcards, which is
    // what makes them win in dns_mitm.
    if (json_object_object_get_ex(redirect, "overrides", &arr) && json_object_is_type(arr, json_type_object)) {
        json_object_object_foreach(arr, name, ip) {
            const char *a = json_object_is_type(ip, json_type_string) ? json_object_get_string(ip) : "";
            if (!hostname_valid(name) || !ipv4_valid(a) || !openpak_ceiling_covers(c, name)) {
                note_drop(dropped, droppedlen, name);
                continue;
            }
            if (!add_rule(rules, &count, cap, name, a)) { fail(err, errlen, "profile carries more than %d rules", cap); goto bad; }
        }
    }
    // "never" cannot be expressed in a hosts file (no exceptions under a wildcard). The
    // generator keeps those names out of the Switch families; one that is not is reported.
    if (json_object_object_get_ex(redirect, "never", &arr) && json_object_is_type(arr, json_type_array)) {
        size_t n = json_object_array_length(arr);
        for (size_t i = 0; i < n; i++) {
            struct json_object *e = json_object_array_get_idx(arr, i);
            if (json_object_is_type(e, json_type_string)) {
                char label[OPENPAK_HOST_MAX + 16];
                snprintf(label, sizeof(label), "never:%s", json_object_get_string(e));
                note_drop(dropped, droppedlen, label);
            }
        }
    }
    if (count == 0) { fail(err, errlen, "no profile name lies inside the signed ceiling"); goto bad; }

    p = calloc(1, sizeof(*p));
    if (!p) { fail(err, errlen, "out of memory"); goto bad; }
    p->rules = rules;
    p->count = count;
    p->source = src;
    struct json_object *server = NULL;
    if (json_object_object_get_ex(root, "server", &server) && json_object_is_type(server, json_type_object)) {
        const char *a = str_of(server, "address");
        if (a && ipv4_valid(a)) snprintf(p->address, sizeof(p->address), "%s", a);
    }
    json_object_put(root);
    return p;
bad:
    free(rules);
    json_object_put(root);
    return NULL;
}

// ---- the effective-set digest -------------------------------------------------------

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool has_wildcard_for(const openpak_policy *p, const char *apex) {
    for (int i = 0; i < p->count; i++)
        if (p->rules[i].host[0] == '*' && p->rules[i].address[0] == '\0' &&
            strcmp(p->rules[i].host + 2, apex) == 0)
            return true;
    return false;
}

void openpak_policy_digest(const openpak_policy *p, const char *ip, char out[65]) {
    int n = 0;
    char **entries = calloc((size_t)p->count + 1, sizeof(char *));
    const size_t each = OPENPAK_HOST_MAX + OPENPAK_ADDR_MAX + 16;
    char *store = calloc((size_t)p->count + 1, each);
    if (!entries || !store) { free(entries); free(store); snprintf(out, 65, "%s", ""); return; }
    for (int i = 0; i < p->count; i++) {
        const openpak_rule *r = &p->rules[i];
        char *e = store + (size_t)n * each;
        if (r->address[0]) snprintf(e, each, "override:%s=%s", r->host, r->address);
        else if (r->host[0] == '*') snprintf(e, each, "family:%s", r->host + 1);
        else if (has_wildcard_for(p, r->host)) continue;          // a family's apex line
        else snprintf(e, each, "exact:%s", r->host);
        entries[n++] = e;
    }
    char *e = store + (size_t)n * each;
    snprintf(e, each, "address:%s", ip ? ip : "");
    entries[n++] = e;
    qsort(entries, (size_t)n, sizeof(char *), cmp_str);

    size_t total = 0;
    for (int i = 0; i < n; i++) total += strlen(entries[i]) + 1;
    char *text = malloc(total + 1), *w = text;
    uint8_t digest[32];
    if (text) {
        for (int i = 0; i < n; i++) w += sprintf(w, "%s\n", entries[i]);
        sha256(digest, text, total);
        hex_of(digest, 32, out);
    } else {
        out[0] = '\0';
    }
    free(text);
    free(entries);
    free(store);
}
