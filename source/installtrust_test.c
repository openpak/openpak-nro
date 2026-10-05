// Host-side checks for the firmware-version gate. Build and run on a PC, no console:
//   cc -DOPENPAK_HOST_TEST -o it_test source/installtrust.c source/installtrust_test.c && ./it_test
#include "installtrust.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    // Firmware gate: only an exact supported version passes.
    assert(openpak_firmware_is_supported("22.5.0"));
    assert(openpak_firmware_is_supported("23.0.0"));
    assert(openpak_firmware_is_supported("23.0.1"));
    assert(!openpak_firmware_is_supported(OPENPAK_FIRMWARE));
    assert(!openpak_firmware_is_supported("22.4.0"));
    assert(!openpak_firmware_is_supported("22.5.1"));
    assert(!openpak_firmware_is_supported("23.0.2"));
    assert(!openpak_firmware_is_supported("23.0"));
    assert(!openpak_firmware_is_supported(""));
    assert(!openpak_firmware_is_supported(NULL));

    // Host build of openpak_firmware_supported() takes the version through ver.
    char ver[] = "22.5.0";
    assert(openpak_firmware_supported(ver, sizeof(ver)));
    char bad[] = "22.4.0";
    assert(!openpak_firmware_supported(bad, sizeof(bad)));

    printf("install-trust: all checks passed\n");
    return 0;
}
