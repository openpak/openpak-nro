// The online half of the signed ceiling: when the tool opens, fetch the ceiling and the
// Switch profile from openpak.org and refresh the SD-card copies (ceiling.h). It never
// touches the hosts files or anything else the console runs on; the user applies a change
// by selecting OpenPak again.
#pragma once
#include <stddef.h>

// Tries both fetches (short timeouts, no retries) and reloads the policy. note gets a
// one-line summary for the interface ("" when everything went through). Reasons are also
// written to /switch/openpak/network.log.
void openpak_network_refresh(char *note, int notelen);

// One HTTPS GET with this build's User-Agent, for the pinned origins only (the ceiling and
// profile here, the release check in update.c): the body on 200, else NULL with err filled.
// Caller frees. The socket and curl must already be up, which they are inside a refresh.
char *openpak_http_get(const char *url, size_t max, size_t *len, char *err, int errlen);
