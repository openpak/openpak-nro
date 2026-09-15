# Certificate probe

Reads the seven certificate IDs requested by Stardew Valley 1.6.15.13 through
the system SSL service. Prints IDs, status, size and SHA-256 fingerprints; writes
the same output to `/switch/openpak/cert-probe.txt`. Does not change certificates,
game files, or service configuration and does not contact a network server.

Build in this directory:

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make
```

Copy `openpak-cert-probe.nro` to `/switch/`, launch it from Homebrew Menu, and
press + to exit when finished. Reopen FTPD to retrieve the report.

`OpenPak CA: YES` means ID 1033 returned the CA fingerprint bundled with OpenPak
at the time this probe was written. `NO` means it returned different bytes;
inspect the fingerprint before concluding which certificate was returned.
This checks certificate delivery, not Stardew's TLS or multiplayer success.

Provenance: certificate IDs independently read from the user's matching game
binary, documented in the workspace's
`docs/switch/stardew-native-2321-4992-2026-09-12.md`. Service calls and returned
buffer layout use the public
[libnx SSL API](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/ssl.h).
