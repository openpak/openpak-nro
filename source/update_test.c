// Host-side check for the two decisions the self-update makes before it touches the SD card:
// is that release newer, and is that URL really this repository's asset.
#include "update.h"
#include <assert.h>
#include <stdio.h>

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

    puts("PASS: release ordering, development builds never behind, asset URL pinned to the repository");
    return 0;
}
