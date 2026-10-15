// Atmosphere system settings the OpenPak server needs so it can tell the console its version
// list changed (firmware 22.x and 23.x):
//
//   sdmc:/atmosphere/config/system_settings.ini, section [ns.notification]
//     enable_version_list              = u8!0x1
//     debug_version_list_waiting_limit = u32!0x0
//
// Many consoles carry a "block Nintendo background tasks" config with every enable_* switched
// off. Only these two keys are touched; the rest of the file stays byte for byte. ns reads its
// settings once at boot, so a change applies after the next reboot.
#pragma once
#include <stddef.h>

// Pure text step: copy in[0..inlen) to out with the two keys ensured. An existing key has its
// value replaced in place; a missing key goes right after the previous ensured key, else after
// the section's last key line (or its header); a missing section is appended at the end. Line
// endings (LF or CRLF) are taken from the file. in may be NULL/0 for a missing file. Returns the
// output length (out is not NUL-terminated), or (size_t)-1 if cap is too small.
// cap of inlen + NSSETTINGS_GROWTH always suffices.
#define NSSETTINGS_GROWTH 256
size_t openpak_nssettings_transform(const char *in, size_t inlen, char *out, size_t cap);

// Ensure the two keys in the console's file (under openpak_root). The first change backs the
// original up to /switch/openpak/nssettings-backup/ and drops a marker. The write is a temp file
// plus rename. Returns 1 when the file was changed, 0 when it was already right, -1 with a
// message in err when nothing was written. Every outcome is appended to
// /switch/openpak/nssettings.log.
int openpak_nssettings_ensure(char *err, size_t errsz);
