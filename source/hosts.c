#include "hosts.h"
#include "ceiling.h"
#include "policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Where a fetched bundle is cached, and where the one this NRO shipped with lives.
#define CACHE_POLICY "/switch/openpak/policy.json"
#ifdef OPENPAK_HOST_TEST
#define ROMFS_POLICY "romfs/openpak/policy.json"   // running on a PC, straight from the repo
#else
#define ROMFS_POLICY "romfs:/openpak/policy.json"
#endif

// The last resort, used only when neither bundle can be read: what OpenPak redirected
// when this NRO was built. It exists so a console with a corrupt SD card and a romfs
// read error still reaches the network rather than nothing -- "stale beats broken".
//
// It mirrors the shape the published bundle emits, on purpose. An earlier version of
// this list named twenty-seven individual services while the bundle redirected whole
// families, so a console that fell back here behaved measurably differently from one
// that did not, in a way nothing on screen would have explained.
//
// Do not add a title here. The bundle is where hostnames belong now; this list is
// frozen deliberately, and a family already covers any new name under it.
//
// Redirecting a whole family means a console on OpenPak stops reaching Nintendo -- and
// every third party a title dials -- at all: system updates, the eShop CDN and the
// browser included, not only the services we answer for. That is the intended behaviour for this project's audience, and it is why
// choosing Nintendo has to be a complete revert. See README.md.
static openpak_rule builtin_rules[] = {
    // The NAT check probes two addresses from one socket and reads the console's NAT
    // type from what each saw. Both collapsing onto one address is a type it cannot
    // determine, so the second responder names its own box.
    {"nncs2-lp1.n.n.srv.nintendo.net", "145.241.228.207"},
    // Every family the console reaches, each with its apex -- Nintendo's own, the
    // connection test's, and the third parties a title dials directly. OpenPak answers
    // on some and not yet on others; a console on OpenPak reaches OpenPak for all of them.
    {"*.among.us", ""},
    {"among.us", ""},
    {"*.battle.net", ""},
    {"battle.net", ""},
    {"*.demonware.net", ""},
    {"demonware.net", ""},
    {"*.ea.com", ""},
    {"ea.com", ""},
    {"*.epicgames.dev", ""},
    {"epicgames.dev", ""},
    {"*.exitgames.com", ""},
    {"exitgames.com", ""},
    {"*.live.com", ""},
    {"live.com", ""},
    {"*.microsoft.com", ""},
    {"microsoft.com", ""},
    {"*.mojang.com", ""},
    {"mojang.com", ""},
    {"*.nintendo.com", ""},
    {"nintendo.com", ""},
    {"*.nintendo.net", ""},
    {"nintendo.net", ""},
    {"*.nintendowifi.net", ""},
    {"nintendowifi.net", ""},
    {"*.photonengine.io", ""},
    {"photonengine.io", ""},
    {"*.xboxlive.com", ""},
    {"xboxlive.com", ""},
};

static openpak_policy builtin_policy = {
    builtin_rules,
    (int)(sizeof(builtin_rules) / sizeof(*builtin_rules)),
    0,                          // no revision: the fallback is not a published bundle
    OPENPAK_SOURCE_BUILTIN,
    "",                         // no server address of its own: the configured one
};

static const openpak_policy *active;
static openpak_policy *loaded;          // freed on reload; the builtin never is
static char problem[192];
static char dropped_note[256];
static openpak_ceiling ceiling;         // the verified one, or the compiled fallback
static bool profile_fresh;

// With no verified ceiling cached, the families of the frozen list are the ceiling
// (docs/signed-ceiling.md: the compiled list is the first-boot fallback).
static void compiled_ceiling(openpak_ceiling *c) {
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < builtin_policy.count && c->count < OPENPAK_CEILING_MAX; i++)
        if (builtin_rules[i].host[0] == '*')
            snprintf(c->families[c->count++], OPENPAK_FAMILY_MAX, "%s", builtin_rules[i].host + 1);
}

// Best source that parses wins: the profile saved from openpak.org (filtered through the
// ceiling), the downloaded bundle, the bundle this NRO shipped with, the frozen list. A
// source that is present but unusable is reported; one that is simply absent says nothing.
const openpak_policy *openpak_active_policy(void) {
    if (active) return active;
    problem[0] = '\0';
    dropped_note[0] = '\0';

    char path[320], err[160];
    if (!openpak_ceiling_load_cached(&ceiling, err, sizeof(err))) {
        if (err[0]) snprintf(problem, sizeof(problem), "%s", err);
        compiled_ceiling(&ceiling);
    }

    snprintf(path, sizeof(path), "%s%s", openpak_root, OPENPAK_PROFILE_CACHE);
    size_t len = 0;
    char *profile = openpak_read_file(path, 256 * 1024, &len);
    if (profile) {
        loaded = openpak_profile_policy(profile, len, &ceiling,
                                        profile_fresh ? OPENPAK_SOURCE_PROFILE : OPENPAK_SOURCE_PROFILE_SAVED,
                                        dropped_note, sizeof(dropped_note), err, sizeof(err));
        free(profile);
        if (!loaded && !problem[0]) snprintf(problem, sizeof(problem), "saved profile ignored: %s", err);
    }
    if (!loaded) {
        snprintf(path, sizeof(path), "%s%s", openpak_root, CACHE_POLICY);
        loaded = openpak_policy_load(path, OPENPAK_SOURCE_CACHE, err, sizeof(err));
        if (!loaded && err[0] && !problem[0]) snprintf(problem, sizeof(problem), "downloaded bundle ignored: %s", err);
    }
    if (!loaded) {
        loaded = openpak_policy_load(ROMFS_POLICY, OPENPAK_SOURCE_ROMFS, err, sizeof(err));
        if (!loaded && err[0] && !problem[0])
            snprintf(problem, sizeof(problem), "bundled policy ignored: %s", err);
    }
    active = loaded ? loaded : &builtin_policy;
    return active;
}

const char *openpak_policy_problem(void) {
    openpak_active_policy();
    return problem;
}

const char *openpak_policy_dropped(void) {
    openpak_active_policy();
    return dropped_note;
}

const openpak_ceiling *openpak_ceiling_current(void) {
    openpak_active_policy();
    return &ceiling;
}

void openpak_policy_set_fresh(bool fresh) { profile_fresh = fresh; }

void openpak_policy_reload(void) {
    if (loaded) openpak_policy_free(loaded);
    loaded = NULL;
    active = NULL;
}

const char *openpak_root = "";

static const char *const hosts_files[] = {
    "/atmosphere/hosts/default.txt",
    "/atmosphere/hosts/emummc.txt",
};
static const int hosts_files_count = (int)(sizeof(hosts_files) / sizeof(hosts_files[0]));
// The largest hosts file dns_mitm will load; one byte more and ams_mitm aborts at boot.
#define HOSTS_FILE_MAX 0x8000

// Resolves a hosts path under openpak_root (empty on hardware).
static const char *hosts_path(int i) {
    static char buf[320];
    snprintf(buf, sizeof(buf), "%s%s", openpak_root, hosts_files[i]);
    return buf;
}

// Reads a whole file; returns NULL (and an empty result) when it does not exist yet,
// which is the normal case on a console that has never been redirected.
static char *slurp(const char *path, long *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { *len_out = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); *len_out = 0; return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *len_out = (long)got;
    return buf;
}

// dns_mitm's own matcher (wildcardcmp): '*' matches any run of characters, everything else is
// literal. Used to make sure nothing we write is answered by dns_mitm for the NAT-check names.
bool openpak_glob(const char *pattern, const char *string) {
    const char *w = NULL, *s = NULL;
    for (;;) {
        if (!*string) {
            if (!*pattern || *pattern == '*') return true;
            if (!s || !*s) return false;
            string = s++; pattern = w; continue;
        }
        if (*pattern != *string) {
            if (*pattern == '*') { w = ++pattern; s = string; if (*pattern) continue; return true; }
            if (w) { string++; continue; }
            return false;
        }
        pattern++; string++;
    }
}

// Pia resolves the NAT-check servers as "nncs1-%.n.n.srv.nintendo.net" (placeholder unexpanded,
// GetAddrInfoRequestWithOptions). When a hosts line matches, dns_mitm answers itself and Pia does
// not accept that answer: the game unregisters within the second, sends no probe, and shows
// 2618-0006. When the names reach the real resolver, the NAT check runs (2026-09-25, Golf and
// Kirby on the console; dns_mitm_debug.log per boot). So no line we write may match them, which
// rules out "*.nintendo.net"; that family is written as the services under it instead.
static const char *const NAT_CHECK_NAMES[] = {
    "nncs1-%.n.n.srv.nintendo.net", "nncs2-%.n.n.srv.nintendo.net",
    "nncs1-lp1.n.n.srv.nintendo.net", "nncs2-lp1.n.n.srv.nintendo.net",
};
static bool catches_nat_check(const char *pattern) {
    for (size_t i = 0; i < sizeof(NAT_CHECK_NAMES) / sizeof(*NAT_CHECK_NAMES); i++)
        if (openpak_glob(pattern, NAT_CHECK_NAMES[i])) return true;
    return false;
}
// ponytail: the services this console was seen resolving under nintendo.net (2026-09-24 debug
// log). A service missing here leaks to the real resolver instead of the box; add it when seen.
static const char *const NINTENDO_NET_SPLIT[] = {
    "*.s.n.srv", "*.p.srv", "*.ndas.srv", "*.frs.srv", "*.znc.srv", "*.acbaa.srv", "*.ctest.srv",
    "*.penne.srv", "*.vermillion.srv", "*.savanna.srv", "*.dg.srv", "*.er.srv", "*.scsi.srv",
    "*.npln.srv", "*.pctl.srv", "*.npns.srv", "*.five", "*.d4c", "*.cdn", "*.eshop", "*.op2",
    "*.dragons", "*.nso", "*.sun",
};
#define NINTENDO_NET_SPLIT_COUNT (sizeof(NINTENDO_NET_SPLIT) / sizeof(*NINTENDO_NET_SPLIT))

// The rules as they are written: the ".nintendo.net" family as its services, and nothing that
// dns_mitm would answer for the NAT-check names. The block and both digests come from here, so
// what the tool installs and what it later reads back from the card agree.
static openpak_rule *effective_rules(const openpak_policy *p, int *count) {
    openpak_rule *out = calloc((size_t)p->count + NINTENDO_NET_SPLIT_COUNT, sizeof(*out));
    int n = 0;
    if (!out) { *count = 0; return NULL; }
    // dns_mitm takes the LAST matching line: overrides (a name with its own address) go after
    // the family wildcards, or the wildcard would swallow them.
    for (int pass = 0; pass < 2; pass++)
        for (int h = 0; h < p->count; h++) {
            bool override = p->rules[h].address[0] != '\0';
            if (override != (pass == 1)) continue;
            const char *host = p->rules[h].host;
            if (strcmp(host, "*.nintendo.net") == 0) {
                for (size_t k = 0; k < NINTENDO_NET_SPLIT_COUNT; k++, n++)
                    snprintf(out[n].host, OPENPAK_HOST_MAX, "%s.nintendo.net", NINTENDO_NET_SPLIT[k]);
                continue;
            }
            if (catches_nat_check(host)) continue;       // an nncs override would be answered by dns_mitm
            out[n++] = p->rules[h];
        }
    *count = n;
    return out;
}

// True when a hosts line redirects one of the names we care about. Wildcards match by their
// suffix, the same way dns_mitm reads them.
static bool line_targets_ours(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == '\0' || *line == '\r' || *line == '\n') return false;
    const char *host = line;
    while (*host && *host != ' ' && *host != '\t') host++;     // skip the address
    while (*host == ' ' || *host == '\t') host++;
    if (!*host) return false;

    const openpak_policy *p = openpak_active_policy();
    for (int i = 0; i < p->count; i++) {
        // Only names we actually serve. Commenting out someone else's redirect for a host we
        // deliberately leave alone would take their working setup down and give nothing back.
        const char *pat = p->rules[i].host;
        if (pat[0] == '*') {
            const char *suffix = pat + 1;                       // ".example.com"
            size_t hl = strcspn(host, " \t\r\n"), sl = strlen(suffix);
            if (hl >= sl && strncmp(host + hl - sl, suffix, sl) == 0) return true;
        } else if (strncmp(host, pat, strlen(pat)) == 0) {
            char after = host[strlen(pat)];
            if (after == '\0' || after == ' ' || after == '\t' || after == '\r' || after == '\n') return true;
        }
    }
    return false;
}

// Comments out anyone else's redirect for our hostnames, so choosing a network actually
// decides where the console goes: removing our block alone would leave another tool's rules
// in force. ponytail: commented, not deleted — the line stays visible and reversible, and we
// never pin Nintendo's own IPs, which rotate.
static char *neutralize_conflicts(const char *text) {
    size_t cap = strlen(text) * 2 + 64;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t n = 0;
    out[0] = '\0';                 // an empty hosts file must come back empty, not uninitialised
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p + 1) : strlen(p);
        char line[512];
        size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, p, copy);
        line[copy] = '\0';

        if (line_targets_ours(line)) {
            n += (size_t)snprintf(out + n, cap - n, "%s%s", OPENPAK_DISABLED, line);
        } else {
            memcpy(out + n, p, len);
            n += len;
        }
        out[n] = '\0';
        p += len;
    }
    return out;
}

// Puts back every line we commented out, so choosing Nintendo leaves the file as it was
// before this tool ever ran — including another tool's redirects.
static char *restore_conflicts(const char *text) {
    size_t cap = strlen(text) + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t n = 0, marker = strlen(OPENPAK_DISABLED);
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p + 1) : strlen(p);
        if (len >= marker && strncmp(p, OPENPAK_DISABLED, marker) == 0) {
            memcpy(out + n, p + marker, len - marker);
            n += len - marker;
        } else {
            memcpy(out + n, p, len);
            n += len;
        }
        p += len;
    }
    out[n] = '\0';
    return out;
}

// True when nothing but blank lines is left, i.e. the file holds nothing we did not add.
static bool only_whitespace(const char *text) {
    for (; *text; text++)
        if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n') return false;
    return true;
}

// Returns a copy of text with our marked block removed. Caller frees.
static char *strip_block(const char *text) {
    if (!text) return NULL;
    const char *begin = strstr(text, OPENPAK_BEGIN);
    if (!begin) return strdup(text);
    const char *end = strstr(begin, OPENPAK_END);
    const char *resume = end ? end + strlen(OPENPAK_END) : text + strlen(text);
    while (*resume == '\r' || *resume == '\n') resume++;

    size_t head = (size_t)(begin - text);
    size_t tail = strlen(resume);
    char *out = malloc(head + tail + 1);
    if (!out) return NULL;
    memcpy(out, text, head);
    memcpy(out + head, resume, tail);
    out[head + tail] = '\0';
    return out;
}

// ponytail: write to a temp file and rename, so a crash mid-write cannot leave the
// console with a half-written hosts file and no way back to Nintendo.
static bool write_atomic(const char *path, const char *content, char *err, int errlen) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(err, errlen, "cannot write %s", tmp); return false; }
    size_t len = strlen(content);
    bool ok = fwrite(content, 1, len, f) == len;
    fclose(f);
    if (!ok) { remove(tmp); snprintf(err, errlen, "short write on %s", tmp); return false; }
    remove(path);
    if (rename(tmp, path) != 0) { snprintf(err, errlen, "cannot replace %s", path); return false; }
    return true;
}

bool openpak_enabled(void) {
    for (int i = 0; i < hosts_files_count; i++) {
        long len = 0;
        char *text = slurp(hosts_path(i), &len);
        if (!text) continue;
        bool found = strstr(text, OPENPAK_BEGIN) != NULL;
        free(text);
        if (found) return true;
    }
    return false;
}

bool openpak_enable(const char *ip, char *err, int errlen) {
    for (int i = 0; i < hosts_files_count; i++) {
        long len = 0;
        char *text = slurp(hosts_path(i), &len);
        char *stripped = strip_block(text ? text : "");
        free(text);
        if (!stripped) { snprintf(err, errlen, "out of memory"); return false; }
        char *base = neutralize_conflicts(stripped);
        free(stripped);
        if (!base) { snprintf(err, errlen, "out of memory"); return false; }

        // block = existing content (ours removed) + a fresh block
        int rc = 0;
        openpak_rule *rules = effective_rules(openpak_active_policy(), &rc);
        if (!rules) { free(base); snprintf(err, errlen, "out of memory"); return false; }
        size_t cap = strlen(base) + 512 + (size_t)rc * (OPENPAK_HOST_MAX + OPENPAK_ADDR_MAX + 4);
        char *out = malloc(cap);
        if (!out) { free(rules); free(base); snprintf(err, errlen, "out of memory"); return false; }
        int n = snprintf(out, cap, "%s%s%s\n", base,
                         (strlen(base) && base[strlen(base) - 1] != '\n') ? "\n" : "",
                         OPENPAK_BEGIN);
        for (int h = 0; h < rc && n > 0 && (size_t)n < cap; h++)
            n += snprintf(out + n, cap - (size_t)n, "%s %s\n",
                          rules[h].address[0] ? rules[h].address : ip, rules[h].host);
        free(rules);
        if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "%s\n", OPENPAK_END);
        free(base);

        // ams_mitm aborts at boot on a hosts file of 0x8000 bytes or more
        // (dnsmitm_host_redirection.cpp: AMS_ABORT_UNLESS(hosts_size < 0x8000)), and a
        // console that fatals before the menu can only be fixed from a PC. Refuse here
        // instead, before anything is written, so the file on the card stays as it was.
        if (strlen(out) >= HOSTS_FILE_MAX) {
            snprintf(err, errlen, "hosts file would be %zu bytes; Atmosphere refuses %d or more",
                     strlen(out), HOSTS_FILE_MAX);
            free(out);
            return false;
        }

        bool ok = write_atomic(hosts_path(i), out, err, errlen);
        free(out);
        if (!ok) return false;
    }
    // What was just installed, so the next launch can tell whether openpak.org has moved on.
    // Best effort: without the record the installed set is read back from the hosts block.
    char digest[65], e2[96];
    openpak_pending_digest(ip, digest);
    char path[320];
    snprintf(path, sizeof(path), "%s%s", openpak_root, OPENPAK_DIGEST_RECORD);
    openpak_write_file_atomic(path, digest, strlen(digest), e2, sizeof(e2));
    return true;
}

bool openpak_disable(char *err, int errlen) {
    char record[320];
    snprintf(record, sizeof(record), "%s%s", openpak_root, OPENPAK_DIGEST_RECORD);
    remove(record);
    for (int i = 0; i < hosts_files_count; i++) {
        long len = 0;
        char *text = slurp(hosts_path(i), &len);
        if (!text) continue;               // nothing there = nothing to undo
        char *stripped = strip_block(text);
        free(text);
        if (!stripped) { snprintf(err, errlen, "out of memory"); return false; }
        // A full revert: our block goes, and the lines we commented out come back exactly as
        // they were. Whatever the console did before this tool ran, it does again.
        char *base = restore_conflicts(stripped);
        free(stripped);
        if (!base) { snprintf(err, errlen, "out of memory"); return false; }

        // If nothing but our own additions was ever in there, take the file with us.
        if (only_whitespace(base)) {
            free(base);
            remove(hosts_path(i));
            continue;
        }
        bool ok = write_atomic(hosts_path(i), base, err, errlen);
        free(base);
        if (!ok) return false;
    }
    return true;
}

void openpak_pending_digest(const char *ip, char out[65]) {
    int n = 0;
    openpak_rule *rules = effective_rules(openpak_active_policy(), &n);
    if (!rules) { out[0] = '\0'; return; }
    openpak_policy p = {rules, n, 0, OPENPAK_SOURCE_BUILTIN, ""};
    openpak_policy_digest(&p, ip, out);
    free(rules);
}

// The installed block read back as rules: the address on the first line is the server's,
// any other address is an override. Only for an install made before the digest was recorded.
static bool digest_of_block(char out[65]) {
    for (int i = 0; i < hosts_files_count; i++) {
        long len = 0;
        char *text = slurp(hosts_path(i), &len);
        const char *begin = text ? strstr(text, OPENPAK_BEGIN) : NULL;
        const char *end = begin ? strstr(begin, OPENPAK_END) : NULL;
        if (!end) { free(text); continue; }
        openpak_rule *rules = calloc(OPENPAK_RULES_MAX * 2, sizeof(*rules));
        int count = 0;
        char server[OPENPAK_ADDR_MAX] = "";
        for (const char *p = strchr(begin, '\n'); rules && p && p < end && count < OPENPAK_RULES_MAX * 2; p = strchr(p + 1, '\n')) {
            char addr[OPENPAK_ADDR_MAX], host[OPENPAK_HOST_MAX];
            if (sscanf(p + 1, "%45s %159s", addr, host) != 2 || addr[0] == '#') continue;
            if (!server[0]) snprintf(server, sizeof(server), "%s", addr);
            snprintf(rules[count].host, OPENPAK_HOST_MAX, "%s", host);
            snprintf(rules[count].address, OPENPAK_ADDR_MAX, "%s", strcmp(addr, server) ? addr : "");
            count++;
        }
        free(text);
        if (!rules) return false;
        openpak_policy p = {rules, count, 0, OPENPAK_SOURCE_BUILTIN, ""};
        openpak_policy_digest(&p, server, out);
        free(rules);
        return true;
    }
    return false;
}

bool openpak_installed_digest(char out[65]) {
    if (!openpak_enabled()) return false;
    char path[320];
    snprintf(path, sizeof(path), "%s%s", openpak_root, OPENPAK_DIGEST_RECORD);
    size_t len = 0;
    char *rec = openpak_read_file(path, 256, &len);
    if (rec && len >= 64) {
        memcpy(out, rec, 64);
        out[64] = '\0';
        free(rec);
        return true;
    }
    free(rec);
    return digest_of_block(out);
}
