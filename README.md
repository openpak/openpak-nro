# openpak.nro

Switches a CFW Nintendo Switch between Nintendo's servers and OpenPak's, and back.

**Enable** writes a marked block of `dns_mitm` host rules into
`/atmosphere/hosts/default.txt` and `/atmosphere/hosts/emummc.txt`, pointing the domain
families OpenPak claims at your server. **Disable** removes that block and leaves
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
OpenPak with this build, even if already active, then reboot.

Selecting OpenPak now also subscribes the News module to OpenPak's four channels and asks
it to receive (`SetSubscriptionStatus`, `RequestImmediateReception` on news:a). Patches and
host rules alone are not enough: the module only fetches channels it follows. Following a
channel sends subscription status **2** (subscribed) — on 22.5.0, status 1 means
*unsubscribed*, so the module never fetched our topics and refused to store their records.
Selecting Nintendo forgets those topics (status 0) and nothing else — the console's Nintendo
channels are never touched, and `ClearSubscriptionStatusAll` is never called.

The filter these commands take is a bare topic id (`[A-Za-z0-9_]{1,31}`); it is probed
read-only first, and only a filter naming an `openpak_` topic is ever written. If it is not
accepted the rest of the setup still applies.

A third News patch, `openpak_news_list_no_dauth`, joins the pair above: it lets the module's
list and titles/topics fetches proceed without a Nintendo edge token, the same way the
existing patch does for the memory download. All three are keyed by BCAT build id.
Untested on hardware as of 2026-09-23.

## Crash Team Racing key (2026-09-23)

Selecting **OpenPak** also installs `exefs_patches/openpak_ctr_key`: Crash Team Racing
Nitro-Fueled checks its Demonware login replies against a key in its own code, which no
console-level trust reaches, so this data-only IPS swaps that key for OpenPak's (the matching
private key signs on `servers/demonware`). It is the only game patch OpenPak ships, keyed to
the final update's build id (`1C689518406930512C13DDF4217E7676`); details and evidence in
`servers/demonware/tools/ctr-key-patch`. Selecting Nintendo removes it. A patched CTR
rejects real Demonware's replies, so it only makes sense once the Demonware hostnames point at
OpenPak. Untested on hardware as of 2026-09-23.

## Diablo II: Resurrected (2026-09-20)

Four names join the managed list: `geo.battle.net` picks the region,
`telemetry-in.battle.net` takes the client's events, `account.battle.net` is the web login
that issues the session, and `*.actual.battle.net` is the bgs gateway on port 1119. The
wildcard covers both regions on purpose — geo decides which one the title dials, so a console
that geolocates to EU would otherwise reach nothing.

`prod.depot.battle.net` is the content depot, which the title resolves on every boot and
OpenPak serves nothing on. It used to be listed and deliberately off. The family covers it
now, so those requests fail to connect instead of failing against Blizzard — the same
trade every other unserved name under a claimed family makes.

Nothing else is needed on the console. The certificate this NRO already installs into the ssl
sysmodule covers geo, telemetry, account and the gateway, because the title puts all four
through `nn::ssl`. It does **not** cover the game-server connection: that one leg is handled
inside the game by bgs-sdk's own bundled OpenSSL with its own CA list, which no console-side
certificate can extend. The server sides that one as `ws://` instead, so no certificate is
involved — see `servers/battlenet/docs/d2r-protocol.md`.

## Store installs and the firmware gate (0.3.7)

OpenPak targets exactly one system firmware, **22.5.0**. Everything it installs —
host rules, the browser/system CA, the News patches and the store-install patch —
is derived from that firmware, so selecting OpenPak on any other version is
refused before a single file is written, with a message naming the version the
console actually runs. Selecting **Nintendo** removes cleanly on *any* firmware,
so a console updated after installing can always clean up; a console left on
OpenPak after an update is warned at launch and pointed at that fix.

Selecting OpenPak also installs the console-trust patch that lets an OpenPak-store
title install and launch. The store serves only homebrew and its updates, all
packaged **without a rights ID** (key-area crypto, no tickets), so no ES/ticket
patch is involved, and no loader/ACID patch either — Atmosphère replaces the
stock loader with its own, which does not enforce the NPDM ACID signature. The
only console patch needed is an **FS NCA-header-signature** kip patch.

That FS patch is gated by the FS KIP hash so it can never apply to another
firmware, and it ships **only once verified on hardware**. Until then the step is
*pending*: it writes nothing for FS and says so. See
[`docs/install-trust.md`](docs/install-trust.md) for the derivation, delivery
(kip_patches via fusee, since this console boots hekate → `pkg3=`), the candidate
analysis, and the exact hardware test and SD-reader recovery procedure.

## Updating itself

At launch, while the network is up for the ceiling fetch, the tool asks GitHub for the newest
release of this repository. If it is ahead of the running build the message line offers it:
**A** Update, **B** Not now. Accepting downloads that release's `openpak.nro` (or
`openpak-console.nro` — each build updates to its own asset), checks that what arrived is a whole NRO (header, `ASET` header, last asset ending at the end
of the file), writes it over the file hbmenu loaded, and hands the console straight back to it, so the
new build is running seconds later without touching the SD card in a PC. A build that cannot
be handed over says so and asks to be reopened.

Nothing is downloaded until that yes. The download URL comes off the network, so it is used
only when it is a release asset of this repository under
`https://github.com/openpak/openpak-nro/releases/download/`; a development build
(`git describe`, not a tag) is never offered an update. The binary itself carries no
signature yet — it is trusted because the transfer is HTTPS from the pinned repository and
the bytes parse as an NRO. The two decisions (is that release newer, is that URL ours) are
`source/update.c`, checked by `make test`.

## Failure reports

When this tool fails — an install or remove that stopped (hosts, CA, patch or system
writes), a display that will not start, a crash of the NRO itself — it saves a small
report under `/switch/openpak/reports/` (at most 8; the oldest go first) and asks before
sending it to `https://openpak.org/api/v1/crash-reports`: **A** Send, **B** Don't send,
**X** Always, **Y** Never. Always/Never are remembered in `/switch/openpak/reports.txt`
(`always`, `never` or `ask`). A report saved by an earlier run, e.g. before a crash, is
offered at the next launch. Nothing is ever sent without that yes.

A report is the NRO version, firmware and Atmosphère version, what was being done and the
error text (for a crash: the exception type and the faulting offsets). No account token or
console identifier is sent. The pure half (JSON, multipart body, queue, setting) is
`source/report.c`, checked by `make test`; the console half is `source/crash.c`.


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

## Where the host list comes from

Not from this repository any more. The block is generated from the signed v2 platform
bundle the release pipeline fetches and verifies, and the console reads it at runtime:
a downloaded bundle on the SD card if there is one, otherwise the bundle this NRO
shipped with, otherwise a short list compiled in as a last resort. Adding a forwarder
is a server deploy, not a new NRO on everybody's SD card.

`source/hosts.c` still carries that last resort, and it is deliberately frozen. It
mirrors what the bundle emits so that a console falling back to it does not quietly
behave differently from one that did not.

### Signed redirect ceiling (live profile)

When the tool opens with a connection it fetches, from `openpak.org` only, the signed
redirect ceiling (`/api/v1/network/ceiling`) and the Switch network profile
(`/api/v1/network/profile?platform=switch`); the contract is
[`docs/signed-ceiling.md`](docs/signed-ceiling.md). The ceiling is verified with Ed25519
against the pinned key (TweetNaCl, verify only, `source/ed25519.c`), cached under
`/switch/openpak/ceiling.json` exactly as received, and accepted only if its version is at
least the highest one ever accepted (`ceiling.version`). Every profile name outside the
verified ceiling is left out on its own and logged to `/switch/openpak/network.log`; the
rest become the rule set that selecting OpenPak installs, written the same way as a
bundle's (`*.family` plus the apex, overrides last). Without a verified ceiling the
families of the frozen list stand in for it.

Order of sources: the saved profile (just fetched, or from an earlier launch), then the
downloaded bundle, the bundled one, the frozen list. Nothing on the console changes in
the background: when OpenPak is installed and the effective set it would install differs
from the installed one (a digest recorded at install in `redirects.digest`, or read back
from the hosts block), the tool shows "OpenPak updated the Switch's network redirects.
Re-apply OpenPak and reboot to use them." Consoles using OpenPak DNS need none of this.

A bundle this build cannot fully apply is refused whole rather than applied in part —
an unknown schema version, another console's projection, an unrecognised rule action, a
capability this release does not implement. The interface names the source and revision
of the rules in force, so a console running the frozen fallback does not look current.

## What gets redirected

Everything the console reaches, as whole families with their apexes:

```
  Nintendo      .nintendo.net  .nintendo.com  .nintendowifi.net
  Served        .battle.net  .among.us  .photonengine.io  .exitgames.com
  Not yet       .demonware.net  .epicgames.dev  .ea.com  .xboxlive.com
                .live.com  .mojang.com  .microsoft.com
```

plus the NAT check's second responder on its own address.

**A console on OpenPak reaches OpenPak for all of it** — system updates, the eShop CDN,
telemetry, the browser, the connection test, and every third party a title dials. Names
nothing of ours answers on yet fail to connect rather than reaching a service that would
refuse the console: each of those services asks Nintendo to vouch for a token an OpenPak
console does not have. This tool is for banned, jailbroken and emulated consoles, so that
refusal was certain; the console now never talks to them at all.

The per-host record behind each third-party family — which title, which run, what was
seen — is in [`docs/researched-hosts.md`](docs/researched-hosts.md).

Choosing **OpenPak** also comments out anyone else's redirect for a name in these
families (an `#openpak-off#` prefix), so the choice decides where the console goes.

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
files, and a certificate overlay OpenPak did not write, are preserved, with setup stopped for
review. An overlay that carries the OpenPak CA is OpenPak's own whatever its layout — an
older build wrote it from the live certificate list rather than the CertStore file — so
re-applying replaces it instead of stopping. Writes are
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
