# openpak.nro

Switches a CFW Nintendo Switch between Nintendo's servers and OpenPak's, and back.

**Enable** writes a marked block of `dns_mitm` host rules into
`/atmosphere/hosts/default.txt` and `/atmosphere/hosts/emummc.txt`, pointing every name the
OpenPak Switch adapter answers for at your server. **Disable** removes that block and leaves
the files exactly as they were — anything you had in them is preserved, and running enable
twice replaces the block instead of stacking copies. Both writes go through a temp file and
a rename, so an interrupted write can never strand the console between networks.

The server address lives in `/switch/openpak/server.txt` and is editable on the console with
the system keyboard (X). Changes take effect after a reboot, since `dns_mitm` reads the hosts
files at boot.

## Controls

| Button | Action |
|---|---|
| A | Switch to OpenPak |
| B | Back to Nintendo |
| X | Change server address |
| + | Exit |

## Building

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make
make test    # host-side check of the toggle logic, no console needed
```

Written from scratch on libnx + SDL2 + SDL2_ttf; it ships no font, using the console's own
shared font instead. AGPL-3.0-only.
