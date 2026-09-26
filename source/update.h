// The tool updates itself. At launch, while the network is already up for the ceiling fetch, it
// asks GitHub — where the release workflow publishes the two NROs — for the newest tag. If that
// tag is ahead of this build the interface offers it; accepting downloads the release over the
// running file and queues it as the next program to run, so leaving the tool starts the new
// build. Nothing is downloaded or replaced without the user saying yes.
//
// ponytail: the download is trusted because it is HTTPS from the pinned repository and the bytes
// parse as an NRO — nothing more. The release publishes no signed manifest; when it does, verify
// it here with ed25519.c the way ceiling.c verifies the redirect ceiling.
#pragma once
#include <stdbool.h>

#define OPENPAK_RELEASE_URL  "https://api.github.com/repos/openpak/openpak-nro/releases/latest"
#define OPENPAK_ASSET_PREFIX "https://github.com/openpak/openpak-nro/releases/download/"
#ifndef OPENPAK_ASSET
#define OPENPAK_ASSET "openpak.nro"          // the Makefile passes the build's own asset name
#endif

// Pure halves, checked by `make test`.
// Is tag ("v0.3.13", "0.3.13") a later release than mine? Up to three numbers, missing ones 0,
// anything after them ignored — a local build is "0.3.12-4-gabc1234". A version that is not
// numbers at all ("dev") never has an update: a development build is not behind a release.
bool openpak_update_newer(const char *tag, const char *mine);
// The download URL comes off the network, so it is used only when it is this repository's
// release asset for this build: <prefix>/<tag>/<name>, nothing deeper, nowhere else.
bool openpak_update_asset_ok(const char *url, const char *name);

// Where the running NRO came from (argv[0]); call once at launch.
void openpak_update_self(const char *argv0);
// Asks for the newest release and remembers it. Called by netfetch.c with the socket and curl
// already up; note gets one line for /switch/openpak/network.log.
void openpak_update_check(char *note, int notelen);
// "" when there is nothing newer, else the release's version ("0.3.13").
const char *openpak_update_tag(void);
// Downloads that release over the running NRO. 1: installed, the console restarts into it when
// the tool exits. 0: installed, but this launch cannot hand over — the user reopens it. -1:
// nothing was replaced. msg always gets the line to show.
int openpak_update_apply(char *msg, int msglen);
