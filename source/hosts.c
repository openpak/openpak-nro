#include "hosts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const openpak_hosts[] = {
    // BAAS + Nintendo Account (login, device accounts, link)
    "*.baas.nintendo.com",
    "accounts.nintendo.com",
    "api.accounts.nintendo.com",
    "cdn.accounts.nintendo.com",
    // device/application auth
    "*.ndas.srv.nintendo.net",
    // push (Penne), Vermillion, eShop beach, NSO membership
    "*.penne.srv.nintendo.net",
    "gw.hac.lp1.vermillion.srv.nintendo.net",
    "beach.hac.lp1.eshop.nintendo.net",
    "capi.lp1.op2.nintendo.net",
    // per-title NPLN tenants
    "*.t.npln.srv.nintendo.net",
    // Photon: titles on Photon Realtime/Fusion (Outbound) resolve the name server themselves
    // and never touch a Nintendo host, so without these the console still reaches Photon Cloud.
    "*.photonengine.io",
    "*.exitgames.com",
};
const int openpak_hosts_count = (int)(sizeof(openpak_hosts) / sizeof(openpak_hosts[0]));

const char *openpak_root = "";

static const char *const hosts_files[] = {
    "/atmosphere/hosts/default.txt",
    "/atmosphere/hosts/emummc.txt",
};
static const int hosts_files_count = (int)(sizeof(hosts_files) / sizeof(hosts_files[0]));

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
        char *base = strip_block(text ? text : "");
        free(text);
        if (!base) { snprintf(err, errlen, "out of memory"); return false; }

        // block = existing content (ours removed) + a fresh block
        size_t cap = strlen(base) + 128 + (size_t)openpak_hosts_count * 128;
        char *out = malloc(cap);
        if (!out) { free(base); snprintf(err, errlen, "out of memory"); return false; }
        int n = snprintf(out, cap, "%s%s%s\n", base,
                         (strlen(base) && base[strlen(base) - 1] != '\n') ? "\n" : "",
                         OPENPAK_BEGIN);
        for (int h = 0; h < openpak_hosts_count && n > 0 && (size_t)n < cap; h++)
            n += snprintf(out + n, cap - (size_t)n, "%s %s\n", ip, openpak_hosts[h]);
        if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "%s\n", OPENPAK_END);
        free(base);

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
        char *base = strip_block(text);
        free(text);
        if (!base) { snprintf(err, errlen, "out of memory"); return false; }
        bool ok = write_atomic(hosts_path(i), base, err, errlen);
        free(base);
        if (!ok) return false;
    }
    return true;
}
