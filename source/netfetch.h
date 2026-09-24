// The online half of the signed ceiling: when the tool opens, fetch the ceiling and the
// Switch profile from openpak.org and refresh the SD-card copies (ceiling.h). It never
// touches the hosts files or anything else the console runs on; the user applies a change
// by selecting OpenPak again.
#pragma once

// Tries both fetches (short timeouts, no retries) and reloads the policy. note gets a
// one-line summary for the interface ("" when everything went through). Reasons are also
// written to /switch/openpak/network.log.
void openpak_network_refresh(char *note, int notelen);
