#include "hosts.h"
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
};

static const openpak_policy *active;
static openpak_policy *loaded;          // freed on reload; the builtin never is
static char problem[192];

// Best source that parses wins. A bundle that is present but unreadable is reported;
// one that is simply absent is the ordinary state and says nothing.
const openpak_policy *openpak_active_policy(void) {
    if (active) return active;
    problem[0] = '\0';

    char path[320], err[160];
    snprintf(path, sizeof(path), "%s%s", openpak_root, CACHE_POLICY);
    loaded = openpak_policy_load(path, OPENPAK_SOURCE_CACHE, err, sizeof(err));
    if (!loaded) {
        if (err[0]) snprintf(problem, sizeof(problem), "downloaded bundle ignored: %s", err);
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
        const openpak_policy *p = openpak_active_policy();
        size_t cap = strlen(base) + 512 + (size_t)p->count * (OPENPAK_HOST_MAX + OPENPAK_ADDR_MAX + 4);
        char *out = malloc(cap);
        if (!out) { free(base); snprintf(err, errlen, "out of memory"); return false; }
        int n = snprintf(out, cap, "%s%s%s\n", base,
                         (strlen(base) && base[strlen(base) - 1] != '\n') ? "\n" : "",
                         OPENPAK_BEGIN);
        for (int h = 0; h < p->count && n > 0 && (size_t)n < cap; h++)
            n += snprintf(out + n, cap - (size_t)n, "%s %s\n",
                          p->rules[h].address[0] ? p->rules[h].address : ip, p->rules[h].host);
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
    return true;
}

bool openpak_disable(char *err, int errlen) {
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
