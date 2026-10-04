#include "installtrust.h"

#include <stdio.h>
#include <string.h>
#ifndef OPENPAK_HOST_TEST
#include <switch.h>
#endif

bool openpak_firmware_is_supported(const char *display_version) {
    return display_version && strcmp(display_version, OPENPAK_FIRMWARE) == 0;
}

bool openpak_firmware_supported(char *ver, int verlen) {
#ifdef OPENPAK_HOST_TEST
    // Host build has no setsys; a test injects the version through ver.
    return openpak_firmware_is_supported(ver);
#else
    char display[0x40] = "";
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            snprintf(display, sizeof(display), "%s", fw.display_version);
        setsysExit();
    }
    if (ver) snprintf(ver, verlen, "%s", display[0] ? display : "unknown");
    return openpak_firmware_is_supported(display);
#endif
}
