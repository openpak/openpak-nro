// SPDX-License-Identifier: AGPL-3.0-only
// Save Data Cloud: the hekate side of the FS key-seed-package key swap. See ksp.h and
// docs/save-data-cloud.md; the hekate behaviour cited here is v6.5.4 (bootloader/hos/pkg2.c,
// pkg2_ini_kippatch.c, bdk/utils/ini.c).
#include "ksp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// The retail modulus: FS 23.0.x .rodata 0x5450 (vaddr 0x1e4450 FAT, 0x1ef450 exFAT), the prod
// KeySeedPackage-signing key that config-init copies to cryptoConfig+0x100. A Nintendo public key,
// shipped the way Atmosphère ships the NCA header and ACID moduli: hekate checks it against the
// KIP before it writes a byte, so it has to be on the card, and the NRO cannot read it from the
// console (package2 is encrypted). SHA-256 fa7d578d1eb5ef2fb9f934c19cf5d6155a754c69db7d7c88394c5dadb9be831a.
const uint8_t openpak_ksp_retail[256] = {
    0xaa,0xe5,0x70,0x4d,0x2e,0x19,0xe8,0xd7,0x18,0x99,0x66,0x59,0xe7,0xf5,0x66,0x9d,
    0x56,0x38,0xfb,0x38,0xaf,0x7c,0xa5,0xf2,0x34,0x9d,0x32,0x9f,0x6a,0x73,0x6b,0xcb,
    0xbd,0xd3,0x96,0x82,0xe4,0xeb,0x66,0xfb,0x76,0x7d,0x79,0x44,0xb6,0xe9,0xae,0xf9,
    0xc5,0xee,0x34,0xf4,0xdf,0xe1,0xc0,0x94,0x54,0xc1,0xd6,0x26,0xdf,0x07,0x90,0x70,
    0x46,0xa7,0x16,0x45,0x32,0x83,0xb3,0x54,0xdf,0xca,0xf4,0xc7,0x18,0xde,0xed,0xe1,
    0xdb,0xb3,0x2a,0x4d,0x0f,0x57,0xde,0x2d,0xb5,0x01,0x7a,0xde,0xac,0xff,0x73,0x83,
    0x4f,0x3e,0x1f,0xa1,0xc1,0x15,0x6f,0xfa,0xd6,0x77,0xa3,0x84,0x87,0x7b,0xc2,0xb9,
    0xb1,0xe3,0x6e,0x23,0x4b,0x9d,0x6f,0x5e,0x88,0x71,0xdb,0x36,0x4c,0xa8,0x0a,0xc0,
    0x80,0x7c,0x68,0x89,0x64,0xd1,0x4a,0x14,0x43,0x44,0x18,0x84,0xbc,0xc5,0xca,0xd3,
    0x63,0x0b,0xa2,0xdf,0x94,0xe7,0x3d,0x8b,0x56,0x2e,0x19,0x7c,0x1e,0xdc,0x8d,0xe0,
    0xcd,0x81,0xad,0xda,0xcf,0x09,0xa5,0x63,0xda,0x48,0xa4,0x98,0xcf,0x31,0x27,0xd7,
    0xd2,0x2c,0x0d,0x0b,0x21,0x34,0xdf,0xcb,0x97,0x11,0xdf,0xce,0xc7,0x8c,0xb1,0xca,
    0x72,0x06,0x5b,0xd5,0x5e,0x2e,0x60,0xb3,0xca,0x1e,0x30,0x36,0x94,0x5e,0xec,0xab,
    0x72,0xb6,0x3d,0x93,0x0c,0xf8,0x78,0x88,0x15,0xd0,0x60,0x11,0xb0,0x03,0xd3,0x54,
    0x53,0x8e,0x89,0x70,0xe2,0xf3,0xb9,0x67,0xd3,0xc5,0x8f,0x39,0xdd,0x6d,0x53,0x47,
    0xb7,0x49,0x12,0x43,0x06,0x54,0x9a,0x40,0x6e,0x89,0xd7,0x24,0x5c,0xbd,0x82,0x91,
};

// OpenPak's KeySeedPackage-signing public modulus (e = 65537); the saves server holds the private
// half. SHA-256 fc53edd13cffb435f3b5e66c915b04260637b411f103429c1a0cd4d4eaf601be.
const uint8_t openpak_ksp_openpak[256] = {
    0xb9,0xf3,0xed,0xe7,0x09,0x3f,0x7a,0xf3,0x2e,0xfa,0xa8,0xb1,0x81,0x27,0x96,0xc6,
    0x8a,0x10,0x98,0x59,0xce,0x30,0x9c,0x98,0x07,0x94,0x4a,0x81,0xed,0xc7,0xf3,0xe6,
    0x8e,0xe4,0x0c,0xf9,0x3f,0x31,0xcc,0x9e,0x0a,0xea,0x7a,0xb6,0x59,0x25,0x6b,0xe0,
    0x2b,0x51,0xde,0xcb,0xc1,0x63,0xa9,0x30,0x93,0x25,0x6b,0x90,0x6a,0x0e,0x5c,0xed,
    0xc4,0x11,0x2b,0x2d,0xc4,0x9f,0x9a,0xb5,0xb0,0x86,0x3c,0x77,0xb1,0x95,0x16,0xfb,
    0x6e,0x3c,0xc9,0x05,0xc1,0xca,0x52,0x42,0x3f,0x50,0xfb,0xfe,0xf3,0xe7,0xa5,0x80,
    0x83,0xf6,0x05,0xdc,0xd6,0xb5,0x6f,0xc8,0x75,0xc9,0x80,0xac,0xf2,0x45,0x69,0xce,
    0xbf,0x7f,0x69,0x4a,0x33,0x7b,0x01,0xfc,0xa2,0x9d,0x61,0x58,0x0c,0x23,0xf0,0xda,
    0xbf,0x05,0x01,0x01,0xee,0x6d,0x1e,0x9c,0xb8,0xc8,0xf0,0x57,0x88,0x61,0xd7,0x11,
    0x1f,0x73,0x0a,0x12,0x80,0x29,0x1a,0x33,0xc5,0x8c,0xd8,0x25,0xfb,0x83,0x84,0xe7,
    0xca,0x58,0xab,0xbb,0x04,0xf2,0x07,0x47,0xa8,0x17,0xff,0xb3,0x1f,0xdb,0x9b,0x32,
    0xa1,0x59,0x35,0x8c,0xc5,0xa1,0x1c,0xce,0xf0,0x2f,0xea,0x5e,0x07,0x1b,0xc0,0xa8,
    0x78,0x28,0x9e,0xb8,0xc5,0xf5,0x79,0x5e,0xa5,0x4d,0xc1,0x55,0x53,0x5e,0xb2,0x08,
    0xeb,0x54,0x88,0x49,0x71,0x8a,0x67,0x32,0x27,0x09,0xe9,0xa0,0xf5,0x6e,0xa6,0x5a,
    0xf4,0xf3,0x0a,0x1a,0x90,0x36,0xaf,0x60,0x01,0xe6,0x88,0xe6,0xea,0x2a,0x55,0xaa,
    0xaa,0x1d,0xb8,0xe0,0x6e,0xcb,0x6b,0x11,0x32,0xfa,0x16,0xb4,0x77,0x49,0x53,0x9f,
};

#define MAX_FILE 65536
// hekate reads both files with f_gets(lbuf, 512): 511 bytes with the '\n' (FatFs drops '\r').
// A longer line is read as two, so a file with one is not what hekate sees.
#define MAX_LINE 510
// hekate allocates 16 patch sets per KIP and writes a NULL terminator after the last; FS 23.0.x
// has one built in (nogc). Every run of same-named lines in a matching section is one more.
#define MAX_SETS 15
#define BUILTIN_SETS 1

static const char *const fs_hash[2] = {"34383ee799926340", "fdaf163288e10805"};   // FAT, exFAT
static const uint32_t chunk[4] = {0x5450, 0x5490, 0x54D0, 0x5510};                // .rodata, 0x40 each
static const char marker[] = "# OpenPak Save Data Cloud";
static const char patch_line[] = "." OPENPAK_KSP_PATCH "=";

bool openpak_ksp_firmware(const char *v) {
    return v && (!strcmp(v, "23.0.0") || !strcmp(v, "23.0.1"));
}

typedef struct { size_t at, end, next; } line_t;   // [at,end): the text, no "\r\n"; next: the line after

static int split(const uint8_t *s, size_t n, line_t **out) {
    size_t count = 1;
    for (size_t i = 0; i < n; ++i) count += s[i] == '\n';
    line_t *l = malloc(count * sizeof(*l));
    if (!l) return -1;
    size_t k = 0;
    for (size_t i = 0; i < n;) {
        size_t e = i;
        while (e < n && s[e] != '\n') ++e;
        size_t next = e < n ? e + 1 : e, end = e;
        if (end > i && s[end - 1] == '\r') --end;
        l[k++] = (line_t){i, end, next};
        i = next;
    }
    *out = l;
    return (int)k;
}

static bool text(const uint8_t *s, size_t n) { return n <= MAX_FILE && (!n || !memchr(s, 0, n)); }
static size_t lead(const uint8_t *s, line_t l) {
    size_t i = l.at;
    while (i < l.end && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}
static bool begins(const uint8_t *s, size_t at, size_t end, const char *p) {
    size_t pl = strlen(p);
    return end - at >= pl && !memcmp(s + at, p, pl);
}
static const char *eol_of(const uint8_t *s, size_t n) {
    const uint8_t *nl = n ? memchr(s, '\n', n) : NULL;
    return nl && nl > s && nl[-1] == '\r' ? "\r\n" : "\n";
}

// A section header as hekate's parsers see one: '[' in column 0.
static bool header(const uint8_t *s, line_t l) { return l.end - l.at >= 2 && s[l.at] == '['; }
// [FS:<one of our hashes>] — 0 FAT, 1 exFAT, -1 anything else.
static int fs_header(const uint8_t *s, line_t l) {
    if (l.end - l.at != 21 || memcmp(s + l.at, "[FS:", 4) || s[l.at + 20] != ']') return -1;
    for (int h = 0; h < 2; ++h)
        if (!strncasecmp((const char *)s + l.at + 4, fs_hash[h], 16)) return h;
    return -1;
}
static bool ours(const uint8_t *s, line_t l) {
    size_t a = lead(s, l);
    return begins(s, a, l.end, patch_line) || begins(s, a, l.end, marker);
}

typedef struct { uint8_t *b; size_t n, cap; bool bad; } buf_t;
static void put(buf_t *o, const void *p, size_t n) {
    if (o->bad) return;
    if (o->n + n > o->cap) {
        size_t cap = (o->n + n) * 2 + 256;
        uint8_t *b = realloc(o->b, cap);
        if (!b) { o->bad = true; return; }
        o->b = b; o->cap = cap;
    }
    memcpy(o->b + o->n, p, n); o->n += n;
}
static void puts_(buf_t *o, const char *p) { put(o, p, strlen(p)); }
static void hex(buf_t *o, const uint8_t *p, size_t n) {
    char h[3];
    for (size_t i = 0; i < n; ++i) { snprintf(h, sizeof(h), "%02X", p[i]); put(o, h, 2); }
}
static int finish(buf_t *o, const uint8_t *s, size_t n, uint8_t **out, size_t *out_size) {
    if (o->bad) { free(o->b); return -2; }
    if (o->n == n && (!n || !memcmp(o->b, s, n))) { free(o->b); return 0; }
    *out = o->b ? o->b : malloc(1);
    if (!*out) return -2;
    *out_size = o->n;
    return 1;
}

int openpak_ksp_patches_build(const uint8_t *s, size_t n, bool enable, uint8_t **out, size_t *out_size) {
    if (!s) n = 0;
    if (!text(s, n)) return -1;
    line_t *l = NULL;
    int count = split(s, n, &l);
    if (count < 0) return -2;
    bool *drop = calloc((size_t)count + 1, sizeof(bool));
    if (!drop) { free(l); return -2; }
    int result = -1;
    for (int i = 0; i < count; ++i) {
        if (l[i].end - l[i].at > MAX_LINE) goto done;
        drop[i] = ours(s, l[i]);
    }
    // A header of ours goes with our lines, unless something else still patches under it.
    for (int i = 0; i < count; ++i) {
        if (fs_header(s, l[i]) < 0) continue;
        bool had = false, other = false;
        for (int j = i + 1; j < count && !header(s, l[j]); ++j) {
            if (drop[j]) had |= begins(s, lead(s, l[j]), l[j].end, patch_line);
            else other |= l[j].end > l[j].at && s[l[j].at] == '.';
        }
        drop[i] = had && !other;
    }
    buf_t o = {0};
    for (int i = 0; i < count; ++i)
        if (!drop[i]) put(&o, s + l[i].at, l[i].next - l[i].at);
    if (enable) {
        // What hekate will build for each FS: one set per run of same-named lines, per section.
        int runs[2] = {0, 0}, h = -1;
        size_t name = 0, name_len = 0;
        bool named = false;
        for (int i = 0; i < count; ++i) {
            if (drop[i]) continue;
            if (header(s, l[i])) { h = fs_header(s, l[i]); named = false; continue; }
            if (h < 0 || l[i].end == l[i].at || s[l[i].at] != '.') continue;
            size_t e = l[i].at + 1;
            while (e < l[i].end && s[e] != '=') ++e;
            size_t len = e - l[i].at - 1;
            if (!named || len != name_len || memcmp(s + name, s + l[i].at + 1, len)) ++runs[h];
            name = l[i].at + 1; name_len = len; named = true;
        }
        if (BUILTIN_SETS + runs[0] + 1 > MAX_SETS || BUILTIN_SETS + runs[1] + 1 > MAX_SETS) {
            free(o.b); goto done;
        }
        const char *eol = eol_of(s, n);
        if (o.n && o.b[o.n - 1] != '\n') puts_(&o, eol);
        puts_(&o, marker); puts_(&o, " (openpak.nro): firmware 23.0.x FS trusts OpenPak's save transfer key."); puts_(&o, eol);
        puts_(&o, marker); puts_(&o, ": undo by selecting Nintendo in OpenPak, or delete these lines and every "
                              "kip1patch=" OPENPAK_KSP_PATCH " in bootloader/hekate_ipl.ini."); puts_(&o, eol);
        for (int f = 0; f < 2; ++f) {
            puts_(&o, "[FS:"); puts_(&o, fs_hash[f]); puts_(&o, "]"); puts_(&o, eol);
            for (int c = 0; c < 4; ++c) {
                char head[40];
                snprintf(head, sizeof(head), "%s1:0x%04X:0x40:", patch_line, (unsigned)chunk[c]);
                puts_(&o, head);
                hex(&o, openpak_ksp_retail + c * 0x40, 0x40); puts_(&o, ",");
                hex(&o, openpak_ksp_openpak + c * 0x40, 0x40); puts_(&o, eol);
            }
        }
        puts_(&o, marker); puts_(&o, " ends here."); puts_(&o, eol);
    }
    result = finish(&o, s, n, out, out_size);
done:
    free(l); free(drop);
    return result;
}

// key=value in column 0, as hekate's ini_parse splits it: the key is everything before '='.
static bool key_is(const uint8_t *s, line_t l, const char *key, size_t *value) {
    size_t kl = strlen(key);
    if (l.end - l.at <= kl || memcmp(s + l.at, key, kl) || s[l.at + kl] != '=') return false;
    *value = l.at + kl + 1;
    return true;
}
static long number(const uint8_t *s, size_t at, size_t end) {   // atoi
    char v[24];
    size_t n = end - at < sizeof(v) - 1 ? end - at : sizeof(v) - 1;
    memcpy(v, s + at, n); v[n] = 0;
    return atol(v);
}
static bool value_is(const uint8_t *s, size_t at, size_t end, const char *v) {
    return end - at == strlen(v) && !memcmp(s + at, v, end - at);
}
// hekate's kip1patch list: comma separated, case-insensitive, blanks trimmed.
static bool token(const uint8_t *s, size_t at, size_t end, size_t *t, size_t *te) {
    while (at < end && (s[at] == ' ' || s[at] == '\t')) ++at;
    size_t e = end;
    while (e > at && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    *t = at; *te = e;
    return e - at == strlen(OPENPAK_KSP_PATCH) && !strncasecmp((const char *)s + at, OPENPAK_KSP_PATCH, e - at);
}
static bool requests(const uint8_t *s, line_t l) {
    size_t v, t, te;
    if (!key_is(s, l, "kip1patch", &v)) return false;
    for (size_t a = v; a <= l.end;) {
        size_t e = a;
        while (e < l.end && s[e] != ',') ++e;
        if (token(s, a, e, &t, &te)) return true;
        a = e + 1;
    }
    return false;
}
// What ends a launch entry's keys for hekate: a blank line, a comment, a caption or a section.
static bool ends_entry(const uint8_t *s, line_t l) {
    return l.end == l.at || s[l.at] == '#' || s[l.at] == '{' || s[l.at] == '[';
}

int openpak_ksp_boot_build(const uint8_t *s, size_t n, bool enable, bool emummc, bool emummc_enabled,
                           uint8_t **out, size_t *out_size) {
    if (!s) n = 0;
    if (!text(s, n)) return -1;
    line_t *l = NULL;
    int count = split(s, n, &l);
    if (count < 0) return -2;
    char *after = calloc((size_t)count + 1, 1);      // enable: kip1patch goes after this line
    if (!after) { free(l); return -2; }
    bool found = false;
    if (enable) {
        for (int i = 0; i < count; ++i) {
            if (!header(s, l[i]) || (l[i].end - l[i].at == 8 && !memcmp(s + l[i].at, "[config]", 8))) continue;
            int pkg3 = -1;
            bool sysmmc = false, stock = false, asked = false;
            for (int j = i + 1; j < count && !ends_entry(s, l[j]); ++j) {
                size_t v;
                if ((key_is(s, l[j], "pkg3", &v) || key_is(s, l[j], "fss0", &v)) &&
                    (value_is(s, v, l[j].end, "atmosphere/package3") || value_is(s, v, l[j].end, "atmosphere/package3-openpak")))
                    pkg3 = j;
                else if (key_is(s, l[j], "emummc_force_disable", &v)) sysmmc = number(s, v, l[j].end) != 0;
                else if (key_is(s, l[j], "stock", &v)) stock = number(s, v, l[j].end) != 0;
                asked |= requests(s, l[j]);
            }
            // A stock entry is the way back to Nintendo: its FS stays as Nintendo shipped it.
            if (pkg3 < 0 || stock || (!sysmmc && emummc_enabled) != emummc) continue;
            found = true;
            if (!asked) after[pkg3] = 1;
        }
        if (!found) { free(l); free(after); return -1; }
    }
    buf_t o = {0};
    const char *eol = eol_of(s, n);
    for (int i = 0; i < count; ++i) {
        if (!enable && requests(s, l[i])) {
            // Keep any other patch on the line; drop the line if ours was all it asked for.
            size_t v = l[i].end, t, te;
            key_is(s, l[i], "kip1patch", &v);
            buf_t keep = {0};
            for (size_t a = v; a <= l[i].end;) {
                size_t e = a;
                while (e < l[i].end && s[e] != ',') ++e;
                if (!token(s, a, e, &t, &te) && te > t) { if (keep.n) puts_(&keep, ","); put(&keep, s + t, te - t); }
                a = e + 1;
            }
            if (keep.bad) { free(keep.b); free(o.b); free(l); free(after); return -2; }
            if (keep.n) {
                put(&o, s + l[i].at, v - l[i].at); put(&o, keep.b, keep.n); put(&o, s + l[i].end, l[i].next - l[i].end);
            }
            free(keep.b);
            continue;
        }
        put(&o, s + l[i].at, l[i].next - l[i].at);
        if (after[i]) {
            bool last = l[i].next == l[i].end;      // no newline after the pkg3 line
            const char *le = l[i].next - l[i].end == 2 ? "\r\n" : eol;
            if (last) puts_(&o, le);
            puts_(&o, "kip1patch=" OPENPAK_KSP_PATCH);
            if (!last) puts_(&o, le);
        }
    }
    free(l); free(after);
    return finish(&o, s, n, out, out_size);
}

bool openpak_ksp_emummc_enabled(const uint8_t *s, size_t n) {
    if (!s || !text(s, n)) return false;
    line_t *l = NULL;
    int count = split(s, n, &l);
    if (count < 0) return false;
    bool in = false, enabled = false;
    for (int i = 0; i < count; ++i) {
        size_t v;
        if (header(s, l[i])) in = l[i].end - l[i].at >= 8 && !memcmp(s + l[i].at, "[emummc]", 8);
        else if (in && key_is(s, l[i], "enabled", &v)) enabled = number(s, v, l[i].end) != 0;
    }
    free(l);
    return enabled;
}

size_t openpak_ksp_describe(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, char *out, size_t len) {
    if (!len) return 0;
    out[0] = 0;
    line_t *la = NULL, *lb = NULL;
    int ca = a ? split(a, an, &la) : 0, cb = b ? split(b, bn, &lb) : 0;
    bool *used = cb > 0 ? calloc((size_t)cb, sizeof(bool)) : NULL;
    size_t w = 0;
    if (ca < 0 || cb < 0 || (cb > 0 && !used)) goto done;
    for (int pass = 0; pass < 2; ++pass) {
        int lines = pass ? cb : ca;
        for (int i = 0; i < lines; ++i) {
            line_t x = pass ? lb[i] : la[i];
            const uint8_t *s = pass ? b : a;
            if (!pass) {
                bool matched = false;
                for (int j = 0; j < cb && !matched; ++j)
                    if (!used[j] && lb[j].end - lb[j].at == x.end - x.at && !memcmp(b + lb[j].at, s + x.at, x.end - x.at))
                        used[j] = matched = true;
                if (matched) continue;
            } else if (used[i]) continue;
            if (x.end == x.at && x.next == x.end) continue;      // the empty tail after a final newline
            int k = snprintf(out + w, len - w, "%c %.*s\n", pass ? '+' : '-', (int)(x.end - x.at), (const char *)s + x.at);
            if (k < 0 || (size_t)k >= len - w) { w = len - 1; goto done; }
            w += (size_t)k;
        }
    }
done:
    free(la); free(lb); free(used);
    return w;
}
