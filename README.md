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

## Using it

A list, not a button chart:

| Row | What selecting it does |
|---|---|
| **Network** | Flips between OpenPak and Nintendo, writes the change straight away, then asks whether to reboot |
| **Server address** | Opens the system keyboard |
| **Reboot console** | Reboots, for when you said "later" |

Up/Down move, **A** selects, **B** exits. Every change is written the moment you select it;
the reboot is what makes it live, since `dns_mitm` reads the hosts files at boot.

## Two builds

| Build | Size | Where it runs |
|---|---|---|
| `openpak.nro` (default) | ~1.7 MB | Anywhere — applet mode and title takeover. Drawn straight to the framebuffer with FreeType text: no SDL, no EGL, no mesa, which is what applet mode cannot provide. |
| `openpak-console.nro` (`UI=console`) | ~228 KB | Anywhere. Same toggle, plain text, kept as a fallback. |

## Building

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make UI=console
make test    # host-side check of the toggle logic, no console needed
```

Written from scratch on libnx + SDL2 + SDL2_ttf; it ships no font, using the console's own
shared font instead. AGPL-3.0-only.
