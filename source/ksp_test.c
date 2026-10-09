// Host-side checks for Save Data Cloud's hekate files (ksp.c). Build and run on a PC, no console:
//   cc -o ksp_test source/ksp.c source/ksp_test.c -lcrypto && ./ksp_test
// When the owner-local 23.0.x FS dumps are in the workspace (scratch/home/openpak-firmware-audit-23.0.x)
// and OpenPak's public modulus at ~/openpak-saves-private/ksp-modulus.bin, the patch is also applied,
// the way hekate applies it, to the real decompressed FS .rodata; otherwise those checks say SKIP.
#include "ksp.h"
#include <assert.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// build(): 1/0/-1 as the function returns; the output (or a copy of the input on 0) in *out.
static int patches(const char *in, bool enable, char **out) {
    uint8_t *o = NULL; size_t on = 0;
    int r = openpak_ksp_patches_build((const uint8_t *)in, in ? strlen(in) : 0, enable, &o, &on);
    if (r == 1) { *out = malloc(on + 1); memcpy(*out, o, on); (*out)[on] = 0; free(o); }
    else if (out) *out = in ? strdup(in) : strdup("");
    return r;
}
static int boot(const char *in, bool enable, bool emummc, bool emu_enabled, char **out) {
    uint8_t *o = NULL; size_t on = 0;
    int r = openpak_ksp_boot_build((const uint8_t *)in, strlen(in), enable, emummc, emu_enabled, &o, &on);
    if (r == 1) { *out = malloc(on + 1); memcpy(*out, o, on); (*out)[on] = 0; free(o); }
    else *out = strdup(in);
    return r;
}

// hekate v6.5.4 reading bootloader/patches.ini (pkg2_ini_kippatch.c) and applying the sets named
// `want` to one KIP's decompressed sections (pkg2.c pkg2_patch_kips), reduced to what matters
// here: f_gets(512) with '\r' dropped, '[' sections, '.' patch lines, src-or-dst check.
// Returns the number of patches applied, -1 on "Patch mismatch".
static uint32_t hx(const char *p) { return (uint32_t)strtol(p, NULL, 16); }
static void htoa(uint8_t *dst, const char *p, unsigned len) {
    while (*p == ' ' || *p == '\t') ++p;
    for (unsigned i = 0; i < len * 2; ++i, ++p) {
        uint8_t v = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : 0;
        if (!(i & 1)) dst[i / 2] = (uint8_t)(v << 4); else dst[i / 2] |= v;
    }
}
static int hekate_apply(const char *ini, const char *kip_hash, const char *want, uint8_t *sections[3]) {
    int applied = 0;
    bool in = false;
    for (const char *p = ini; *p;) {
        char lb[512]; size_t k = 0;
        while (*p && k < 511) { char c = *p++; if (c == '\r') continue; lb[k++] = c; if (c == '\n') break; }
        lb[k] = 0;
        size_t len = k;
        if (len && lb[len - 1] == '\n') lb[len - 1] = 0;
        if (len > 2 && lb[0] == '[') {
            char name[64] = ""; char *colon = strchr(lb, ':'), *close = strchr(lb, ']');
            if (close) *close = 0;
            if (colon) { snprintf(name, sizeof(name), "%.*s", (int)(colon - lb - 1), lb + 1); }
            uint8_t h[8]; htoa(h, colon ? colon + 1 : "", 8);
            uint8_t w[8]; htoa(w, kip_hash, 8);
            in = !strcmp(name, "FS") && !memcmp(h, w, 8);
        } else if (in && lb[0] == '.') {
            char *eq = strchr(lb, '=');
            if (!eq) continue;
            *eq = 0;
            if (strcmp(lb + 1, want)) continue;
            unsigned sect = (unsigned)(eq[1] - '0');
            char *f = eq + 3, *c1 = strchr(f, ':'); assert(c1);
            uint32_t off = hx(f); *c1 = 0;
            char *f2 = c1 + 1, *c2 = strchr(f2, ':'); assert(c2);
            uint32_t n = hx(f2);
            char *src = c2 + 1, *comma = strchr(src, ','); assert(comma);
            uint8_t a[0x100], b[0x100]; assert(n <= sizeof(a) && sect < 3);
            htoa(a, src, n); htoa(b, comma + 1, n);
            uint8_t *at = sections[sect] + off;
            if (memcmp(at, a, n) && memcmp(at, b, n)) return -1;
            memcpy(at, b, n);
            ++applied;
        }
    }
    return applied;
}

static void sha_hex(const uint8_t *d, size_t n, char out[65]) {
    uint8_t h[32]; SHA256(d, n, h);
    for (int i = 0; i < 32; ++i) snprintf(out + i * 2, 3, "%02x", h[i]);
}
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(size > 0 ? (size_t)size : 1);
    *n = fread(b, 1, (size_t)size, f); fclose(f);
    return b;
}

int main(void) {
    char *o, *o2, hexd[65];

    // The two moduli: pinned, distinct, both a plausible RSA-2048 modulus (top bit set, odd).
    sha_hex(openpak_ksp_retail, 256, hexd);
    assert(!strcmp(hexd, "fa7d578d1eb5ef2fb9f934c19cf5d6155a754c69db7d7c88394c5dadb9be831a"));
    sha_hex(openpak_ksp_openpak, 256, hexd);
    assert(!strcmp(hexd, "fc53edd13cffb435f3b5e66c915b04260637b411f103429c1a0cd4d4eaf601be"));
    assert(memcmp(openpak_ksp_retail, openpak_ksp_openpak, 256));
    assert(openpak_ksp_retail[0] & 0x80 && openpak_ksp_retail[255] & 1);
    assert(openpak_ksp_openpak[0] & 0x80 && openpak_ksp_openpak[255] & 1);

    // Firmware gate: exactly the two versions whose FS the sections are cut for.
    assert(openpak_ksp_firmware("23.0.0") && openpak_ksp_firmware("23.0.1"));
    assert(!openpak_ksp_firmware("22.5.0") && !openpak_ksp_firmware("23.0.2") && !openpak_ksp_firmware("23.1.0"));
    assert(!openpak_ksp_firmware("") && !openpak_ksp_firmware(NULL) && !openpak_ksp_firmware("23.0"));

    // --- patches.ini: add to nothing.
    assert(patches(NULL, true, &o) == 1);
    assert(strstr(o, "[FS:34383ee799926340]\n.openpak_ksp=1:0x5450:0x40:AAE5704D"));
    assert(strstr(o, "[FS:fdaf163288e10805]\n.openpak_ksp=1:0x5450:0x40:AAE5704D"));
    int lines = 0;
    for (char *p = o, *e; *p; p = e + 1) {
        e = strchr(p, '\n'); assert(e);
        assert(e - p <= 510);                                   // hekate reads 511 with the '\n'
        assert(p[0] == '#' || p[0] == '[' || !strncmp(p, ".openpak_ksp=1:0x", 17));
        ++lines;
    }
    assert(lines == 3 + 2 * 5);
    char *block_copy = strdup(o);
    // hekate applies it: both FS sections, four chunks each, retail → OpenPak, nothing else moves.
    for (int h = 0; h < 2; ++h) {
        uint8_t *sec[3];
        for (int i = 0; i < 3; ++i) { sec[i] = malloc(0x10000); memset(sec[i], 0x5a + i, 0x10000); }
        memcpy(sec[1] + 0x5450, openpak_ksp_retail, 256);
        uint8_t *before = malloc(0x10000); memcpy(before, sec[1], 0x10000);
        const char *hash = h ? "fdaf163288e10805" : "34383ee799926340";
        assert(hekate_apply(block_copy, hash, "openpak_ksp", sec) == 4);
        assert(!memcmp(sec[1] + 0x5450, openpak_ksp_openpak, 256));
        assert(!memcmp(sec[1], before, 0x5450) && !memcmp(sec[1] + 0x5550, before + 0x5550, 0x10000 - 0x5550));
        for (int i = 0; i < 3; i += 2) for (int j = 0; j < 0x10000; ++j) assert(sec[i][j] == 0x5a + i);
        assert(hekate_apply(block_copy, hash, "openpak_ksp", sec) == 4);   // already patched: accepted
        memset(sec[1] + 0x5490, 0, 4);                                      // neither: hekate stops
        assert(hekate_apply(block_copy, hash, "openpak_ksp", sec) == -1);
        assert(hekate_apply(block_copy, "0011223344556677", "openpak_ksp", sec) == 0);   // other FS: skipped
        for (int i = 0; i < 3; ++i) free(sec[i]);
        free(before);
    }
    // Idempotent; remove takes the file back to nothing.
    assert(patches(o, true, &o2) == 0); free(o2);
    assert(patches(o, false, &o2) == 1 && !strcmp(o2, "")); free(o2);
    assert(patches("", false, &o2) == 0); free(o2);
    free(o);

    // --- patches.ini: somebody's sigpatches for the same FS, another KIP, comments, CRLF.
    const char *user =
        "# sigpatches\r\n"
        "[FS:34383ee799926340]\r\n"
        ".nosigchk=0:0x194A0:0x4:BA090094,E0031F2A\r\n"
        "\r\n"
        "[Loader:0123456789abcdef]\r\n"
        ".noacidsigchk=0:0x100:0x2:0000,FFFF\r\n";
    assert(patches(user, true, &o) == 1);
    assert(!strncmp(o, user, strlen(user)));                            // every byte of theirs first
    assert(strstr(o + strlen(user), "\r\n[FS:fdaf163288e10805]\r\n.openpak_ksp="));
    for (const char *p = o; (p = strchr(p, '\n')); ++p) assert(p[-1] == '\r');   // CRLF kept throughout
    assert(patches(o, true, &o2) == 0); free(o2);                       // idempotent
    assert(patches(o, false, &o2) == 1 && !strcmp(o2, user)); free(o2); // byte for byte back
    // Both sets apply under hekate's merge of the two [FS:3438…] sections.
    {
        uint8_t *sec[3];
        for (int i = 0; i < 3; ++i) sec[i] = calloc(1, 0x20000);
        memcpy(sec[1] + 0x5450, openpak_ksp_retail, 256);
        memcpy(sec[0] + 0x194A0, "\xBA\x09\x00\x94", 4);
        assert(hekate_apply(o, "34383ee799926340", "openpak_ksp", sec) == 4);
        assert(hekate_apply(o, "34383ee799926340", "nosigchk", sec) == 1);
        assert(!memcmp(sec[1] + 0x5450, openpak_ksp_openpak, 256) && !memcmp(sec[0] + 0x194A0, "\xE0\x03\x1F\x2A", 4));
        for (int i = 0; i < 3; ++i) free(sec[i]);
    }
    // A user who wrote under our last header after we did keeps the header and those lines.
    char *appended = malloc(strlen(o) + 64);
    sprintf(appended, "%s.nogc=0:0x10:0x1:00,01\r\n", o);
    assert(patches(appended, false, &o2) == 1);
    assert(!strncmp(o2, user, strlen(user)) && !strcmp(o2 + strlen(user), "[FS:fdaf163288e10805]\r\n.nogc=0:0x10:0x1:00,01\r\n"));
    free(o2); free(appended);
    // Our block moved into the middle of a file and the rest edited: still found, the rest kept.
    char *middle = malloc(strlen(o) + 64);
    sprintf(middle, "%s[FS:aaaaaaaaaaaaaaaa]\r\n.x=0:0x0:0x1:00,01\r\n", o);
    assert(patches(middle, false, &o2) == 1);
    assert(!strncmp(o2, user, strlen(user)) && !strcmp(o2 + strlen(user), "[FS:aaaaaaaaaaaaaaaa]\r\n.x=0:0x0:0x1:00,01\r\n"));
    free(o2);
    assert(patches(middle, true, &o2) == 1);                            // re-add: ours goes back to the end
    assert(strstr(o2, ".x=0:0x0:0x1:00,01\r\n# OpenPak Save Data Cloud"));
    free(o2); free(middle); free(o);
    // An [FS:3438…] header of theirs with nothing under it is not ours to remove.
    assert(patches("[FS:34383ee799926340]\n# mine\n", false, &o2) == 0); free(o2);
    // A file without a final newline: one is added before our block, and remove leaves it.
    assert(patches("[X:0000000000000000]\n.a=0:0x0:0x1:00,01", true, &o) == 1);
    assert(!strncmp(o, "[X:0000000000000000]\n.a=0:0x0:0x1:00,01\n# OpenPak Save Data Cloud", 63));
    assert(patches(o, false, &o2) == 1 && !strcmp(o2, "[X:0000000000000000]\n.a=0:0x0:0x1:00,01\n"));
    free(o); free(o2);

    // --- patches.ini: malformed files are refused, untouched.
    {
        uint8_t nul[] = "[FS:34383ee799926340]\n\0\n", *x = NULL; size_t xn = 0;
        assert(openpak_ksp_patches_build(nul, sizeof(nul) - 1, true, &x, &xn) == -1);
        assert(openpak_ksp_patches_build(nul, sizeof(nul) - 1, false, &x, &xn) == -1);
        char *big = malloc(65538); memset(big, '#', 65537); big[65537] = 0;
        for (int i = 100; i < 65537; i += 100) big[i] = '\n';
        assert(patches(big, true, &o) == -1); free(o); free(big);
        char longl[600]; memset(longl, 'x', 511); longl[0] = '#'; strcpy(longl + 511, "\n");
        assert(patches(longl, true, &o) == -1); free(o);
        longl[510] = '\n'; longl[511] = 0;                              // 510 + '\n': hekate reads it whole
        assert(patches(longl, true, &o) == 1); free(o);
        // 13 sets of theirs on one FS + nogc + ours = 15: fits; 14 would overflow hekate's table.
        char sets[4096] = "[FS:fdaf163288e10805]\n";
        for (int i = 0; i < 13; ++i) sprintf(sets + strlen(sets), ".p%d=0:0x0:0x1:00,01\n.p%d=0:0x1:0x1:00,01\n", i, i);
        assert(patches(sets, true, &o) == 1); free(o);
        strcat(sets, ".p13=0:0x0:0x1:00,01\n");
        assert(patches(sets, true, &o) == -1); free(o);
        assert(patches(sets, false, &o) == 0); free(o);                 // nothing of ours: nothing to do
    }
    printf("ksp: patches.ini add, hekate-applied, idempotent, remove byte-for-byte, others' sections kept, malformed refused\n");

    // --- hekate_ipl.ini.
    const char *ipl =
        "[config]\n"
        "autoboot=1\n"
        "\n"
        "{--- Custom Firmware ---}\n"
        "[CFW (emuMMC)]\n"
        "pkg3=atmosphere/package3-openpak\n"
        "kip1patch=nogc\n"
        "emummcforce=1\n"
        "icon=bootloader/res/icon_payload.bmp\n"
        "\n"
        "[CFW (sysMMC)]\n"
        "fss0=atmosphere/package3-openpak\n"
        "emummc_force_disable=1\n"
        "\n"
        "[Stock]\n"
        "pkg3=atmosphere/package3\n"
        "stock=1\n"
        "emummc_force_disable=1\n"
        "\n"
        "[Elsewhere]\n"
        "#pkg3=atmosphere/package3\n"
        "pkg3=atmosphere/package3\n"
        "\n"
        "[Payload]\n"
        "payload=bootloader/payloads/x.bin\n";
    // On emuMMC with emuMMC enabled: only the emuMMC entry (the commented-out key ends [Elsewhere]).
    assert(boot(ipl, true, true, true, &o) == 1);
    {
        char *want = malloc(strlen(ipl) + 64);
        const char *at = strstr(ipl, "package3-openpak\nkip1patch=nogc") + 17;
        sprintf(want, "%.*skip1patch=openpak_ksp\n%s", (int)(at - ipl), ipl, at);
        assert(!strcmp(o, want));
        free(want);
    }
    assert(boot(o, true, true, true, &o2) == 0); free(o2);              // idempotent
    assert(boot(o, false, true, true, &o2) == 1 && !strcmp(o2, ipl)); free(o2);
    free(o);
    // On sysMMC: the sysMMC CFW entry; never a stock one, which is the way back to Nintendo.
    assert(boot(ipl, true, false, true, &o) == 1);
    assert(strstr(o, "fss0=atmosphere/package3-openpak\nkip1patch=openpak_ksp\nemummc_force_disable=1\n"));
    assert(!strstr(o, "[Stock]\npkg3=atmosphere/package3\nkip1patch"));
    assert(!strstr(o, "[CFW (emuMMC)]\npkg3=atmosphere/package3-openpak\nkip1patch=openpak_ksp"));
    assert(boot(o, false, false, true, &o2) == 1 && !strcmp(o2, ipl)); free(o2);
    free(o);
    // emuMMC switched off in emummc.ini: every entry boots sysMMC.
    assert(boot(ipl, true, true, false, &o) == -1); free(o);           // NRO on emuMMC, no entry boots it
    assert(boot(ipl, true, false, false, &o) == 1);
    assert(strstr(o, "[CFW (emuMMC)]\npkg3=atmosphere/package3-openpak\nkip1patch=openpak_ksp\n")); free(o);
    // No OpenPak/Atmosphère entry at all.
    assert(boot("[config]\nautoboot=0\n[P]\npayload=x.bin\n", true, true, true, &o) == -1); free(o);
    // CRLF, and a pkg3 line that is the last line of the file.
    assert(boot("[A]\r\nemummcforce=1\r\npkg3=atmosphere/package3", true, true, true, &o) == 1);
    assert(!strcmp(o, "[A]\r\nemummcforce=1\r\npkg3=atmosphere/package3\r\nkip1patch=openpak_ksp"));
    assert(boot(o, false, true, true, &o2) == 1 && !strcmp(o2, "[A]\r\nemummcforce=1\r\npkg3=atmosphere/package3\r\n"));
    free(o); free(o2);
    // Remove: ours taken out of a merged list, theirs kept; a line of only ours removed; case and blanks.
    assert(boot("[A]\npkg3=atmosphere/package3\nkip1patch=nogc, OpenPak_KSP\nkip1patch= openpak_ksp \n", false, true, true, &o) == 1);
    assert(!strcmp(o, "[A]\npkg3=atmosphere/package3\nkip1patch=nogc\n")); free(o);
    assert(boot("[A]\npkg3=atmosphere/package3\nkip1patch=openpak_ksp_old\n", false, true, true, &o) == 0); free(o);
    // An entry that already asks for it is left alone; key spelling hekate would not read is not ours.
    assert(boot("[A]\npkg3=atmosphere/package3\nkip1patch=nogc,openpak_ksp\n", true, true, true, &o) == 0); free(o);
    assert(boot("[A]\npkg3 = atmosphere/package3\n", true, true, true, &o) == -1); free(o);
    {
        uint8_t nul[] = "[A]\npkg3=atmosphere/package3\n\0", *x = NULL; size_t xn = 0;
        assert(openpak_ksp_boot_build(nul, sizeof(nul) - 1, true, true, true, &x, &xn) == -1);
    }
    printf("ksp: hekate_ipl.ini kip1patch on the entries that boot this MMC only, idempotent, removed cleanly\n");

    // --- emuMMC/emummc.ini as hekate reads it.
    const char *emu = "[emummc]\nenabled=1\nsector=0x70db8000\nid=0x0000\n";
    assert(openpak_ksp_emummc_enabled((const uint8_t *)emu, strlen(emu)));
    const char *emu4 = "[emummc]\r\nenabled=4\r\npath=emuMMC/SD00\r\n";
    assert(openpak_ksp_emummc_enabled((const uint8_t *)emu4, strlen(emu4)));
    const char *off = "[emummc]\nenabled=0\n[other]\nenabled=1\n";
    assert(!openpak_ksp_emummc_enabled((const uint8_t *)off, strlen(off)));
    assert(!openpak_ksp_emummc_enabled(NULL, 0));

    // --- the log's diff.
    char log[512];
    const char *a = "x\ny\n", *b = "x\nz\ny\n";
    openpak_ksp_describe((const uint8_t *)a, strlen(a), (const uint8_t *)b, strlen(b), log, sizeof(log));
    assert(!strcmp(log, "+ z\n"));
    openpak_ksp_describe((const uint8_t *)b, strlen(b), (const uint8_t *)a, strlen(a), log, sizeof(log));
    assert(!strcmp(log, "- z\n"));
    openpak_ksp_describe(NULL, 0, (const uint8_t *)a, strlen(a), log, 5);
    assert(strlen(log) == 4);
    printf("ksp: emummc.ini, log diff\n");

    // --- owner-local: the real 23.0.x FS and OpenPak's key file.
    size_t n = 0;
    uint8_t *mod = NULL;
    const char *home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/openpak-saves-private/ksp-modulus.bin", home ? home : "");
    if ((mod = slurp(path, &n))) {
        assert(n == 256 && !memcmp(mod, openpak_ksp_openpak, 256));
        printf("ksp: OpenPak modulus matches %s\n", path);
        free(mod);
    } else printf("SKIP: OpenPak modulus file (none at %s)\n", path);
    const char *fw[2][2] = {{"0100000000000819", "34383ee799926340"}, {"010000000000081b", "fdaf163288e10805"}};
    for (int h = 0; h < 2; ++h) {
        size_t kn = 0, bn = 0;
        snprintf(path, sizeof(path), "../../scratch/home/openpak-firmware-audit-23.0.x/fs/%s/kips/FS.kip1", fw[h][0]);
        uint8_t *kip = slurp(path, &kn);
        snprintf(path, sizeof(path), "../../scratch/home/openpak-firmware-audit-23.0.x/fs/%s/FS.bin", fw[h][0]);
        uint8_t *bin = slurp(path, &bn);
        if (!kip || !bin) { printf("SKIP: FS %s dumps (not in the workspace)\n", fw[h][1]); free(kip); free(bin); continue; }
        sha_hex(kip, kn, hexd);
        assert(!strncmp(hexd, fw[h][1], 16));                           // hekate's KIP id for this section
        // FS.bin: the KIP header, then each section decompressed, back to back.
        uint32_t size[3];
        for (int i = 0; i < 3; ++i) memcpy(&size[i], bin + 0x20 + i * 0x10 + 4, 4);
        assert(bn >= 0x100u + size[0] + size[1] + size[2]);
        uint8_t *sec[3] = {bin + 0x100, bin + 0x100 + size[0], bin + 0x100 + size[0] + size[1]};
        assert(!memcmp(sec[1] + 0x5450, openpak_ksp_retail, 256));     // the src bytes are the KIP's
        uint8_t *copy = malloc(bn); memcpy(copy, bin, bn);
        assert(hekate_apply(block_copy, fw[h][1], "openpak_ksp", sec) == 4);
        size_t changed = 0;
        for (size_t i = 0; i < bn; ++i) changed += bin[i] != copy[i];
        assert(!memcmp(sec[1] + 0x5450, openpak_ksp_openpak, 256));
        assert(!memcmp(bin, copy, 0x100 + size[0] + 0x5450) &&
               !memcmp(bin + 0x100 + size[0] + 0x5550, copy + 0x100 + size[0] + 0x5550, bn - (0x100 + size[0] + 0x5550)));
        printf("ksp: FS %s: KIP id matches, src = the KIP's bytes, hekate-applied: %zu bytes changed, all inside ro 0x5450+0x100\n",
               fw[h][1], changed);
        free(copy); free(kip); free(bin);
    }
    free(block_copy);
    printf("ksp: all checks passed\n");
    return 0;
}
