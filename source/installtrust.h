// "Store installs" — the console-side patches that let an OpenPak-store title
// (homebrew, packaged WITHOUT a rights ID) install and launch on 22.5.0.
//
// A no-rights-ID title carries no ticket, so no ES/ticket patch is involved. Two
// checks stand in the way:
//   * FS verifies the NCA fixed-key header signature. FS is a stock KIP that
//     Atmosphere keeps, so this is a kip_patch (fusee applies it; this console
//     boots hekate -> pkg3=atmosphere/package3, i.e. fusee).
//   * The loader verifies the NPDM ACID signature. Atmosphere REPLACES the stock
//     Loader with its own, which does not enforce the ACID signature, so no
//     loader patch is needed here (see docs/install-trust.md).
//
// This step is explicit and removable; it is never bundled with the network
// choice. It refuses to install the FS patch until a hardware-verified IPS is
// present in the romfs payload, and says so.
#pragma once
#include <stdbool.h>

// OpenPak targets exactly one system firmware. Everything it installs — hosts,
// CA, News patches, the store-install patch — is derived from that firmware, so
// installing on another version is refused outright. Removal is allowed on any
// firmware, so a console updated after installing can always clean up.
#define OPENPAK_FIRMWARE "22.5.0"

// Pure predicate on a display version string ("22.5.0"): host-testable, no
// system calls. True only for the exact supported version.
bool openpak_firmware_is_supported(const char *display_version);

// Reads the running system firmware. Fills ver (if non-NULL) with the console's
// display version, e.g. "22.5.0". Returns openpak_firmware_is_supported(ver).
bool openpak_firmware_supported(char *ver, int verlen);

// True when a verified FS kip patch is shipping in the NRO's romfs payload.
// Until one is, the step is "pending": it installs nothing and reports that.
bool openpak_store_fs_verified(void);

// True when this tool's store-install patches are currently on the SD card.
bool openpak_store_installed(void);

// Copies the verified payload into /atmosphere/kip_patches. Returns the number
// of files written; fills msg with a human-readable result (including the
// "pending" case, which is not an error and returns 0).
int openpak_store_install(char *msg, int msglen);

// Removes exactly the files this tool would have written, by name. Returns the
// number removed.
int openpak_store_remove(void);
