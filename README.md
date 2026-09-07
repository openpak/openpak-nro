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

Two entries, one choice:

```
  Nintendo                       Active
  OpenPak
```

Up/Down, **A** to pick, **B** to leave. The change is written the moment you select it and
the reboot that applies it is offered right there. The server address is built in — nobody
has to look one up — and can still be overridden with `-DOPENPAK_SERVER="1.2.3.4"` at build
time or a line in `/switch/openpak/server.txt`.

Choosing **OpenPak** writes the rules and comments out anyone else's redirect for the same
hostnames (an `#openpak-off#` prefix), so the choice actually decides where the console goes
instead of falling through to another tool's leftovers.

Choosing **Nintendo** is a full revert: our block goes, every line we commented comes back
exactly as it was, and if the hosts file contained nothing but our own additions it is
deleted. The console is left as if this tool had never run. Nintendo's own addresses are
never written down — they rotate, and DNS resolves them correctly once nothing overrides
them.

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
