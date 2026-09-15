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

## NAT check (2026-09-11)

The two Pia NAT-check names are in the managed list: `nncs1-lp1.n.n.srv.nintendo.net` goes
to the server and `nncs2-lp1.n.n.srv.nintendo.net` to the second responder on its own
address (the console requires two different public IPs). Both are answered by `nn-nncs`.

## News (2026-09-10)

Selecting **OpenPak** also installs the two News patches the News probe proved on hardware
(`news/tools/news-patch`): the BCAT verification key becomes OpenPak's, and the News catalog
download skips the Nintendo edge token. Both are keyed by the BCAT build id (HOS 22.5.0
today), so a console on another firmware simply ignores them. Selecting **Nintendo** removes
them. The News host `bcat-topics-lp1.cdn.nintendo.net` is in the managed host list. Re-select
OpenPak with this build, even if already active, then reboot. Whether the console shows
channels depends on the news service publishing them; the console side is done here.

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

## Hosts we researched but do not serve

Several Switch titles never touch a Nintendo host at all — they go straight to Demonware,
Epic, EA or Xbox Live. Those names are in the table too, but written **commented out**:

```
# researched, not served -- uncomment only once something answers on the other side:
# 10.0.0.7 lavender-switch-auth3.prod.demonware.net
# 10.0.0.7 api.epicgames.dev
...
```

They ship inert on purpose: nothing of ours answers on those addresses yet, so turning one on
trades an authentication failure for a connection failure.

Note what this is *not*. It is not "don't break a working title" — OpenPak is for banned
consoles and emulators, and a third party that asks Nintendo to vouch for the console's token
(Epic certainly, Demonware probably) refuses an OpenPak console regardless, because our
identity is not Nintendo's and a banned console cannot obtain Nintendo's. For most of these
titles the online half is already gone before this tool runs. They are recorded here because the
inventory belongs with the tool that would use it, and because each was observed on a dated
run rather than guessed. Turn one on by flipping its `redirect` flag in `source/hosts.c`
once something answers on the other side; uncommenting the line in the hosts file works too,
but only until the next enable, which regenerates the block.

For the same reason we do not comment out anyone *else's* redirect for these names, the way
we do for the hosts we serve — we only claim what we answer for.

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

## System certificate support (0.3.1)

Select **OpenPak**, accept the reboot, and launch the game normally. Selecting
**Nintendo** restores the original system setup and offers the same reboot.
Stardew's executable is unchanged. Native Stardew online was confirmed working
with this system change; the revised installer needs a hardware round-trip.

This build supports the official **Atmosphere 1.11.2** package at
`/atmosphere/package3` with standard `pkg3` or `fss0` entries in
`/bootloader/hekate_ipl.ini`. Enable verifies the original package's complete
SHA-256, builds `/atmosphere/package3-openpak` with the replacement `ams_mitm`,
and updates those entries automatically. The original package and autoboot
selection remain untouched. No extra installation or manual boot selection is
required. The active package can remain open while the NRO changes the boot
configuration. Unknown configurations produce a setup error.

The NRO reads the console's certificates through SSL and constructs a
certificate-store file, replacing expired certificate 1033 with the bundled
OpenPak CA. No Nintendo certificate archive is bundled. The original boot
configuration and any pre-existing certificate overlay are backed up in
`/switch/openpak/system`. Disable restores them. The inactive OpenPak package
is retained because it may still be open by the current boot; re-enable reuses
an identical package without rewriting it. Independently changed configuration
or certificate files are preserved, with setup stopped for review. Writes are
staged and read back before activation. Do not delete the backup folder while
OpenPak is enabled.

Version 0.3.0 attempted to replace the active package and failed on this console.
Version 0.3.1 replaces that activation method. Existing recovery copies from
0.3.0 can remain on SD; they are not used to select the boot package.

The game-invitation service `*.five.nintendo.net` is included in the managed hosts
list. Earlier builds omitted it, preventing the native invitation applet from
reaching OpenPak in configurations that block Nintendo hosts.

Build the bundled open-source component with `bash tools/build-system-module.sh`.
Its source revision and local patch are listed in `romfs/system/SOURCE.txt`.
Run `make test` for host-side checks, and use
`python3 tools/test-system.py --package3 /path/to/official/package3` for the full
install/reapply/rollback and interrupted-activation tests. That input is local and
is not distributed in this repository.
