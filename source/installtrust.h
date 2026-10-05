// The firmware gate. OpenPak targets the system firmwares its hosts, CA, News patches and
// store-trust package (system.c: OpenPak's own Loader and fusee in package3-openpak,
// docs/install-trust.md) are derived from. 23.0.1 differs from 23.0.0 only in sdb.
#pragma once
#include <stdbool.h>

// Installing on another version is refused outright. Removal is allowed on any firmware, so a
// console updated after installing can always clean up.
#define OPENPAK_FIRMWARE "22.5.0, 23.0.0 or 23.0.1"   // for messages

// Pure predicate on a display version string ("22.5.0"): host-testable, no
// system calls. True only for an exact supported version.
bool openpak_firmware_is_supported(const char *display_version);

// Reads the running system firmware. Fills ver (if non-NULL) with the console's
// display version, e.g. "22.5.0". Returns openpak_firmware_is_supported(ver).
bool openpak_firmware_supported(char *ver, int verlen);
