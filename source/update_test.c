// Host-side check for the two decisions the self-update makes before it touches the SD card:
// is that release newer, and is that URL really this repository's asset.
#include "update.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A file laid out like elf2nro's output: the NRO image, then the "ASET" header, then the assets,
// the last of which ends at the end of the file.
static const char *nro_file(uint32_t nro, uint32_t romfs, const char *magic, const char *aset_magic,
                            long extra) {
    static char path[] = "/tmp/openpak_update_test.nro";
    uint8_t *b = calloc(1, nro + 0x38 + romfs + (extra > 0 ? (size_t)extra : 0));
    assert(b);
    memcpy(b + 0x10, magic, 4);
    uint32_t total = nro + 0x38 + romfs;
    for (int i = 0; i < 4; ++i) b[0x18 + i] = (uint8_t)(nro >> (i * 8));
    memcpy(b + nro, aset_magic, 4);
    // The romfs entry, relative to the header; elf2nro leaves it zero when there is no romfs.
    uint64_t off = romfs ? 0x38 : 0, len = romfs;
    for (int i = 0; i < 8; ++i) b[nro + 0x28 + i] = (uint8_t)(off >> (i * 8));
    for (int i = 0; i < 8; ++i) b[nro + 0x30 + i] = (uint8_t)(len >> (i * 8));
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(b, 1, total + (extra > 0 ? (size_t)extra : 0), f) == total + (extra > 0 ? (size_t)extra : 0));
    fclose(f);
    free(b);
    return path;
}

#define ASSET_URL(tag, name) OPENPAK_ASSET_PREFIX tag "/" name

int main(void) {
    assert(openpak_update_newer("v0.3.13", "0.3.12"));
    assert(openpak_update_newer("0.4.0", "0.3.99"));
    assert(openpak_update_newer("1.0", "0.9.9"));
    assert(!openpak_update_newer("v0.3.12", "0.3.12"));
    assert(!openpak_update_newer("v0.3.11", "0.3.12"));
    // A local build describes itself as the tag it is past; it is not behind that tag.
    assert(!openpak_update_newer("v0.3.12", "0.3.12-4-gabc1234"));
    assert(openpak_update_newer("v0.3.13", "0.3.12-4-gabc1234"));
    // Nothing to compare against: a development build is never nagged, and junk never updates.
    assert(!openpak_update_newer("v0.3.13", "dev"));
    assert(!openpak_update_newer("latest", "0.3.12"));
    assert(!openpak_update_newer(NULL, "0.3.12"));
    assert(!openpak_update_newer("v0.3.13", NULL));

    assert(openpak_update_asset_ok(ASSET_URL("v0.3.13", "openpak.nro"), "openpak.nro"));
    assert(openpak_update_asset_ok(ASSET_URL("v0.3.13", "openpak-console.nro"), "openpak-console.nro"));
    // The other build's NRO, another repository, a deeper path, a redirect target, no tag.
    assert(!openpak_update_asset_ok(ASSET_URL("v0.3.13", "openpak-console.nro"), "openpak.nro"));
    assert(!openpak_update_asset_ok("https://github.com/evil/openpak-nro/releases/download/v1/openpak.nro", "openpak.nro"));
    assert(!openpak_update_asset_ok(ASSET_URL("v0.3.13", "sub/openpak.nro"), "openpak.nro"));
    assert(!openpak_update_asset_ok("https://elsewhere.example/openpak.nro", "openpak.nro"));
    assert(!openpak_update_asset_ok(ASSET_URL("", "openpak.nro"), "openpak.nro"));
    assert(!openpak_update_asset_ok(OPENPAK_ASSET_PREFIX "v0.3.13", "openpak.nro"));
    assert(!openpak_update_asset_ok(NULL, "openpak.nro"));

    assert(openpak_update_is_nro(nro_file(0x2000, 0x400, "NRO0", "ASET", 0)));
    // An error page, a build whose own length is nonsense, no asset header, and bytes past the
    // end of the last asset — a transfer that did not land whole.
    assert(!openpak_update_is_nro(nro_file(0x2000, 0x400, "html", "ASET", 0)));
    assert(!openpak_update_is_nro(nro_file(0x40, 0x400, "NRO0", "ASET", 0)));
    assert(!openpak_update_is_nro(nro_file(0x2000, 0x400, "NRO0", "junk", 0)));
    assert(!openpak_update_is_nro(nro_file(0x2000, 0x400, "NRO0", "ASET", 16)));
    assert(!openpak_update_is_nro(nro_file(0x2000, 0, "NRO0", "ASET", 0)));      // no romfs: no CA
    assert(!openpak_update_is_nro("/nonexistent/openpak.nro"));
    remove("/tmp/openpak_update_test.nro");

    puts("PASS: release ordering, development builds never behind, asset URL pinned to the repository, a whole NRO on the card");
    return 0;
}
