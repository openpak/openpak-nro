#include "policy.h"

#include <json-c/json.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Which console family's projection this build applies. Set at build time so the
// same parser serves the other setup apps: only this string changes.
#ifndef OPENPAK_PLATFORM
#define OPENPAK_PLATFORM "switch"
#endif

// What this build can actually carry out. A bundle requiring anything else is refused
// whole: SETUP-002 wants zero writes on unsupported input, and "skip the capability we
// do not recognise" is precisely how a policy silently half-applies.
static const char *const supported_caps[] = {
    "dns.suffix.v1",     // a leading-dot family, written as dns_mitm's "*.family"
    "dns.exact.v1",      // one name, written as itself
    "dns.override.v1",   // a rule that moves one name off the server address
    "tls.scoped-ca.v1",  // the CA this NRO installs into the ssl store and the browser
};
static const int supported_caps_count = (int)(sizeof(supported_caps) / sizeof(*supported_caps));

const char *openpak_source_name(openpak_source s) {
    switch (s) {
        case OPENPAK_SOURCE_CACHE: return "downloaded";
        case OPENPAK_SOURCE_ROMFS: return "bundled";
        case OPENPAK_SOURCE_PROFILE: return "openpak.org";
        case OPENPAK_SOURCE_PROFILE_SAVED: return "saved from openpak.org";
        default:                   return "built in";
    }
}

static bool fail(char *err, int errlen, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)errlen, fmt, ap);
    va_end(ap);
    return false;
}

// A string field, or NULL when absent or of the wrong type. Never coerces: a number
// where a hostname belongs is a broken bundle, not a hostname.
static const char *str_of(struct json_object *o, const char *key) {
    struct json_object *v = NULL;
    if (!json_object_object_get_ex(o, key, &v)) return NULL;
    if (!json_object_is_type(v, json_type_string)) return NULL;
    return json_object_get_string(v);
}

static bool copy_into(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n == 0 || n >= cap) return false;
    memcpy(dst, src, n + 1);
    return true;
}

// True when a redirect entry already claims this name, so a passthrough for it could
// never take effect.
static bool covered_by(const openpak_rule *rules, int count, const char *name) {
    size_t nl = strlen(name);
    for (int i = 0; i < count; i++) {
        const char *h = rules[i].host;
        if (h[0] == '*') {
            size_t sl = strlen(h + 1);                 // ".nintendo.net"
            if (nl >= sl && strcmp(name + nl - sl, h + 1) == 0) return true;
        } else if (strcmp(h, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool check_capabilities(struct json_object *root, char *err, int errlen) {
    struct json_object *caps = NULL;
    if (!json_object_object_get_ex(root, "required_capabilities", &caps)) return true;
    if (!json_object_is_type(caps, json_type_array))
        return fail(err, errlen, "required_capabilities is not a list");
    size_t n = json_object_array_length(caps);
    for (size_t i = 0; i < n; i++) {
        struct json_object *c = json_object_array_get_idx(caps, i);
        if (!json_object_is_type(c, json_type_string))
            return fail(err, errlen, "required_capabilities[%zu] is not a string", i);
        const char *want = json_object_get_string(c);
        bool have = false;
        for (int k = 0; k < supported_caps_count && !have; k++)
            have = strcmp(want, supported_caps[k]) == 0;
        if (!have)
            return fail(err, errlen, "bundle needs %s, which this release cannot apply", want);
    }
    return true;
}

// Turns one rule into the nought, one or two host entries it means. Returns false on
// anything malformed; *emitted is how many entries were appended.
static bool rule_into(struct json_object *rule, openpak_rule *out, int *emitted,
                      char *err, int errlen) {
    *emitted = 0;
    const char *action = str_of(rule, "action");
    if (!action) return fail(err, errlen, "a rule has no action");
    // Anything that is neither of the two documented actions is a schema this build
    // does not know. Guessing at it is how a console redirects something it should not.
    if (strcmp(action, "passthrough") == 0) return true;
    if (strcmp(action, "redirect") != 0)
        return fail(err, errlen, "unknown rule action %s", action);

    struct json_object *match = NULL;
    if (!json_object_object_get_ex(rule, "match", &match) ||
        !json_object_is_type(match, json_type_object))
        return fail(err, errlen, "a rule has no match");

    const char *exact = str_of(match, "exact");
    const char *suffix = str_of(match, "suffix");
    if ((exact == NULL) == (suffix == NULL))
        return fail(err, errlen, "a match needs exactly one of exact or suffix");

    // An override moves one name off the server address; the second NAT responder is
    // the reason this exists. Without one the caller's configured address is used.
    char address[OPENPAK_ADDR_MAX] = "";
    struct json_object *dest = NULL;
    if (json_object_object_get_ex(rule, "destination", &dest) &&
        json_object_is_type(dest, json_type_object)) {
        const char *v4 = str_of(dest, "ipv4");
        if (v4 && !copy_into(address, sizeof(address), v4))
            return fail(err, errlen, "destination %s does not fit", v4);
    }

    if (exact) {
        if (!copy_into(out[0].host, OPENPAK_HOST_MAX, exact))
            return fail(err, errlen, "hostname %s does not fit", exact);
        snprintf(out[0].address, OPENPAK_ADDR_MAX, "%s", address);
        *emitted = 1;
        return true;
    }

    if (suffix[0] != '.')
        return fail(err, errlen, "suffix %s does not start with a dot", suffix);
    char wildcard[OPENPAK_HOST_MAX];
    if (snprintf(wildcard, sizeof(wildcard), "*%s", suffix) >= (int)sizeof(wildcard))
        return fail(err, errlen, "suffix %s does not fit", suffix);
    copy_into(out[0].host, OPENPAK_HOST_MAX, wildcard);
    snprintf(out[0].address, OPENPAK_ADDR_MAX, "%s", address);
    *emitted = 1;

    // dns_mitm's "*.family" does not answer for the family's own apex, so a bundle that
    // asks for the apex needs a second line naming it.
    struct json_object *apex = NULL;
    if (json_object_object_get_ex(match, "include_apex", &apex) &&
        json_object_get_boolean(apex)) {
        if (!copy_into(out[1].host, OPENPAK_HOST_MAX, suffix + 1))
            return fail(err, errlen, "apex of %s does not fit", suffix);
        snprintf(out[1].address, OPENPAK_ADDR_MAX, "%s", address);
        *emitted = 2;
    }
    return true;
}

openpak_policy *openpak_policy_parse(const char *json, size_t len, openpak_source src,
                                     char *err, int errlen) {
    struct json_tokener *tok = json_tokener_new();
    if (!tok) { fail(err, errlen, "out of memory"); return NULL; }
    struct json_object *root = json_tokener_parse_ex(tok, json, (int)len);
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (!root || jerr != json_tokener_success) {
        if (root) json_object_put(root);
        fail(err, errlen, "bundle is not valid JSON");
        return NULL;
    }

    openpak_policy *p = NULL;
    openpak_rule *rules = NULL;
    struct json_object *arr = NULL;
    struct json_object *v = NULL;

    if (!json_object_is_type(root, json_type_object)) { fail(err, errlen, "bundle is not an object"); goto bad; }

    // A consumer that does not know a major version rejects the document; it never guesses.
    if (!json_object_object_get_ex(root, "schema_version", &v) ||
        json_object_get_int(v) != 2) {
        fail(err, errlen, "bundle schema is not version 2");
        goto bad;
    }
    const char *platform = str_of(root, "platform");
    if (!platform || strcmp(platform, OPENPAK_PLATFORM) != 0) {
        fail(err, errlen, "bundle is for %s, not " OPENPAK_PLATFORM, platform ? platform : "nothing");
        goto bad;
    }
    if (!check_capabilities(root, err, errlen)) goto bad;

    if (!json_object_object_get_ex(root, "rules", &arr) ||
        !json_object_is_type(arr, json_type_array)) {
        fail(err, errlen, "bundle has no rules");
        goto bad;
    }
    size_t n = json_object_array_length(arr);
    if (n == 0 || n > OPENPAK_RULES_MAX) {
        fail(err, errlen, "bundle carries %zu rules", n);
        goto bad;
    }

    // Two per rule: a family that also wants its apex is the only rule that emits twice.
    rules = calloc(n * 2, sizeof(*rules));
    if (!rules) { fail(err, errlen, "out of memory"); goto bad; }

    int count = 0;
    for (size_t i = 0; i < n; i++) {
        struct json_object *r = json_object_array_get_idx(arr, i);
        if (!json_object_is_type(r, json_type_object)) { fail(err, errlen, "rule %zu is not an object", i); goto bad; }
        int emitted = 0;
        if (!rule_into(r, rules + count, &emitted, err, errlen)) goto bad;
        count += emitted;
    }
    if (count == 0) { fail(err, errlen, "bundle redirects nothing"); goto bad; }

    // A passthrough under a family we redirect cannot be honoured: a hosts file has no
    // way to say "not this one". Refuse rather than quietly send it to OpenPak.
    for (size_t i = 0; i < n; i++) {
        struct json_object *r = json_object_array_get_idx(arr, i);
        const char *action = str_of(r, "action");
        struct json_object *match = NULL;
        if (!action || strcmp(action, "passthrough") != 0) continue;
        if (!json_object_object_get_ex(r, "match", &match)) continue;
        const char *name = str_of(match, "exact");
        if (name && covered_by(rules, count, name)) {
            fail(err, errlen, "%s must reach the internet but a redirect covers it", name);
            goto bad;
        }
    }

    p = calloc(1, sizeof(*p));
    if (!p) { fail(err, errlen, "out of memory"); goto bad; }
    p->rules = rules;
    p->count = count;
    p->source = src;
    if (json_object_object_get_ex(root, "sequence", &v))
        p->sequence = json_object_get_int64(v);
    json_object_put(root);
    return p;

bad:
    free(rules);
    json_object_put(root);
    return NULL;
}

openpak_policy *openpak_policy_load(const char *path, openpak_source src, char *err, int errlen) {
    FILE *f = fopen(path, "rb");
    // Absent is not a failure: it is the normal state of a console that has never
    // fetched one. An empty message is how the caller tells the two apart.
    if (!f) { err[0] = '\0'; return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    // The schema's own ceiling. A larger file is not a bundle, and reading it to find
    // that out is how a console runs out of memory on a hostile SD card.
    if (len <= 0 || len > (1 << 20)) {
        fclose(f);
        fail(err, errlen, "bundle at %s is %ld bytes", path, len);
        return NULL;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); fail(err, errlen, "out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    openpak_policy *p = openpak_policy_parse(buf, got, src, err, errlen);
    free(buf);
    return p;
}

void openpak_policy_free(openpak_policy *p) {
    if (!p) return;
    free(p->rules);
    free(p);
}
