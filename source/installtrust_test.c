// Host-side checks for the firmware-version gate and the Store-installs payload
// logic. Build and run on a PC, no console:
//   cc -DOPENPAK_HOST_TEST -o /tmp/it_test source/installtrust.c source/installtrust_test.c
//   && (cd repo-root && /tmp/it_test)
// Run from the repo root so the romfs payload path resolves.
#include "installtrust.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// installtrust.c references openpak_root (normally from hosts.c); provide it here
// so the test links standalone.
const char *openpak_root = "";

int main(void) {
    // Firmware gate: only the exact supported version passes.
    assert(openpak_firmware_is_supported(OPENPAK_FIRMWARE));
    assert(openpak_firmware_is_supported("22.5.0"));
    assert(!openpak_firmware_is_supported("22.4.0"));
    assert(!openpak_firmware_is_supported("23.0.0"));
    assert(!openpak_firmware_is_supported("22.5.1"));
    assert(!openpak_firmware_is_supported(""));
    assert(!openpak_firmware_is_supported(NULL));

    // Host build of openpak_firmware_supported() takes the version through ver.
    char ver[] = "22.5.0";
    assert(openpak_firmware_supported(ver, sizeof(ver)));
    char bad[] = "22.4.0";
    assert(!openpak_firmware_supported(bad, sizeof(bad)));

    // No verified FS patch ships yet, so the payload is "pending".
    assert(!openpak_store_fs_verified());

    // Install into a temp root: pending means nothing is written and it is not an error.
    char root[] = "/tmp/openpak_it_XXXXXX";
    assert(mkdtemp(root));
    openpak_root = root;
    char msg[160] = "";
    int n = openpak_store_install(msg, sizeof(msg));
    assert(n == 0);
    assert(strstr(msg, "pending") != NULL);
    assert(!openpak_store_installed());

    // Removal is always safe, even with nothing to remove.
    assert(openpak_store_remove() == 0);

    printf("install-trust: all checks passed\n");
    return 0;
}
