// The firmware gate. OpenPak targets exactly one system firmware: the hosts, the CA, the News
// patches and the store-trust package (system.c: OpenPak's own Loader and fusee in
// package3-openpak, docs/install-trust.md) are all derived from it.
#pragma once
#include <stdbool.h>

// Installing on another version is refused outright. Removal is allowed on any firmware, so a
// console updated after installing can always clean up.
#define OPENPAK_FIRMWARE "22.5.0"

// Pure predicate on a display version string ("22.5.0"): host-testable, no
// system calls. True only for the exact supported version.
bool openpak_firmware_is_supported(const char *display_version);

// Reads the running system firmware. Fills ver (if non-NULL) with the console's
// display version, e.g. "22.5.0". Returns openpak_firmware_is_supported(ver).
bool openpak_firmware_supported(char *ver, int verlen);
