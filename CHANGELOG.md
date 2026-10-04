# Changelog — openpak-nro

Generated from git history on 2026-09-15. `git log` stays the source
of truth; this file is the readable summary.

## v0.3.16 — 2026-10-04 (the Album opens the Album)

- system: selecting OpenPak makes the Album open the Album again and keeps the Homebrew Menu one
  button away: hold R while opening the Album. OpenPak is a HOME title from its own eShop now,
  so hbmenu is no longer the everyday launcher. `override_key_0` in `[hbl_config]` of
  `/atmosphere/config/override_config.ini` goes from Atmosphère's default `!R` to `R` (checked
  against 1.11.2's `cfg_override.board.nintendo_nx.inc`: a leading `!` means "hbl unless held").
  Only that key is touched; a missing file is created with the Album and `R`. The file as found,
  or its absence, is saved under `/switch/openpak/system/album.*`, and selecting Nintendo
  restores those bytes or deletes the file. Writes go through the same temp-file, rename and
  recovery as the boot entry.
- system: an hbl setup somebody chose (another title in slot 0, the Album in another slot, a
  key other than `!R`/`R`) or a file changed after OpenPak edited it is left as it is, with a
  note on the message line; it never fails Enable or Disable. The file is read with inih's
  rules; `tools/test-system.py` covers absent, template, other sections and CRLF, re-apply,
  foreign setups, restore and interrupted writes, and the parser was checked against
  Atmosphère's own inih on 6000 generated files.
- ui: after enable both builds say "Album now opens the Album — hold R on Album for the
  Homebrew Menu" (a plain dash in the text build).

## v0.3.15 — 2026-10-04 (store trust)

- system: selecting OpenPak now makes the OpenPak eShop's titles launch. These are self-built
  NSPs with no rights ID and program ids `0x01FE…`, and two console checks stopped them; the boot
  package `/atmosphere/package3-openpak` now changes both. Its Loader
  (`romfs/system/loader-1.11.2.kip`) accepts an invalid NPDM ACID signature for the `0x01FE…`
  range only; Atmosphère's Loader does enforce ACID on retail, which v0.3.7's notes denied. Its
  fusee (`romfs/system/fusee-1.11.2.bin`) applies one compiled-in patch to the stock FS 22.5.0
  KIP: `0x26938` `E0 1B 00 36` → `1F 20 03 D5`, skipping the NCA header-1 signature verdict.
  Both are Atmosphère 1.11.2 builds with `tools/atmosphere-store-trust.patch`;
  `tools/build-system-module.sh` builds them and `romfs/system/SOURCE.txt` records what ships.
  Proven on hardware 2026-10-04: the package this build makes booted the console (emuMMC) and
  JKSV, installed from the OpenPak eShop, ran.
- system: the package is built from the official 1.11.2 `package3` as before, with Loader and
  fusee swapped in after `ams_mitm`. The new Loader is 0x40 bytes larger and not the last KIP, so
  the KIP region is repacked as Atmosphère's `build_package3.py` lays it out, with every offset,
  size and hash updated; every input and every existing hash is checked before a byte changes.
  `tools/test-system.py` requires the result to be byte for byte the package that booted
  (sha256 `4e2ac49b…`) when the official package is available locally.
- system: an existing `package3-openpak` from an earlier build (the `ams_mitm`-only package) is
  recognised and replaced, so re-applying OpenPak after the update is all a console needs. Any
  other file there is still kept and still stops setup ("OpenPak boot package changed").
- system: after enable, a Loader KIP in `/atmosphere/kips` (Horizon OC's `hoc.kip`, for one) is
  named in a warning: fusee loads it ahead of OpenPak's, so store titles will not launch while it
  is there. Setup still succeeds and the file is not touched.
- store installs: the v0.3.7 step that would copy an FS IPS into `/atmosphere/kip_patches` is
  gone, with its empty payload, its "pending" note and the candidate under
  `docs/install-trust/`. Atmosphère 1.11.2's fusee never reads `kip_patches`, and no release
  ever wrote a file there, so nothing needs cleaning up. The firmware 22.5.0 gate stays.
- docs: `docs/install-trust.md` rewritten to what ships and why. Not covered: 22.5.0's exFAT FS
  (a different KIP, left unpatched), and a sysMMC boot, not yet tried on hardware.
- ci: the release runner image is pinned to ubuntu-24.04, with checkout@v5 [86bb33b]

## v0.3.14 — 2026-09-26

- system: OpenPak's own overlay is never recorded as the file that was on the card first. A
  console whose `/switch/openpak/system` folder was deleted while OpenPak was on would have
  backed the active overlay up as the original, and selecting Nintendo would have restored it —
  leaving the OpenPak CA trusted by a console that asked to go back. It now records that there
  was no overlay, so disable takes it away. Covered by
  `tools/test-system.py --package3 <official package3>`, which also now reproduces the re-apply
  failure above: reverting the ownership check fails the suite with that exact message.
- update: what arrives is accepted only when it is a whole NRO. v0.3.13 compared the length in
  the NRO header against the file size, but elf2nro appends the icon, the NACP and the romfs
  behind an `ASET` header after that length, so every real release failed the check and no update
  could ever install. The header, the `ASET` header at the length it declares, and the last
  asset ending exactly at the end of the file are what is checked now, against a build laid out
  like elf2nro's output in `make test`.

## v0.3.13 — 2026-09-26

- system: re-applying OpenPak no longer stops with "Certificate overlay changed; existing file
  preserved" when the overlay on the card is OpenPak's own in a different layout — the one an
  older build wrote from the live certificate list (63 of the 127 entries, re-laid out), or the
  file read back through LayeredFS. Slot 1033 carrying the OpenPak CA is what identifies it as
  ours; a missing overlay has nothing to preserve either. A store this tool did not write is
  still kept and still stops setup, now saying so in those words.
- update: the tool updates itself. At launch, while the network is already up for the ceiling
  fetch, it asks GitHub for the newest release; if that tag is ahead of the running build the
  message line offers it (**A** Update, **B** Not now). Accepting downloads this build's asset,
  checks it is an NRO, writes it over the file hbmenu loaded and queues it as the next program
  to run, so leaving the tool starts the new build. Nothing is downloaded without that yes, and
  the URL is used only when it is a release asset of this repository. `source/update.c`;
  release ordering and the URL check are in `make test`.

## v0.3.12 — 2026-09-25

- hosts: the `.nintendo.net` family is written as its services (`*.s.n.srv`, `*.ndas.srv`,
  `*.cdn`, …) instead of one `*.nintendo.net` line, and any override for the NAT-check names is
  dropped. Pia asks for `nncs1-%.n.n.srv.nintendo.net` with the placeholder unexpanded; when
  dns.mitm answers that lookup itself the game unregisters within the second, sends no probe and
  shows 2618-0006. Reaching the real resolver makes the NAT check run (console, Golf and Kirby,
  2026-09-25). Test asserts nothing in the block matches those names.
- system: enable clears `blank_prodinfo_emummc=1` in `/exosphere.ini` (Prelude's Nintendo mode
  sets it; without the device certificate nn.account fails 2123-0011 and nothing signs in). Disable
  sets it to 1, the safe state for a console about to talk to Nintendo again (what Prelude's
  Nintendo mode does). A copy of the file as found goes to `/switch/openpak/system/exosphere.previous`.

- system: the certificate overlay is built from the CertStore file itself (the archive is
  mounted and read as the ssl service reads it: 127 entries on 22.5.0, original layout) with
  only slot 1033 repointed at the OpenPak CA. The live certificate list, which exposes 63 of
  those entries and re-lays the file out, is now the fallback only. An overlay that already
  carries the CA is kept as it is on re-apply instead of being patched again.

## v0.3.11 — 2026-09-24

- build: the version is the release tag. The workflow passes it to make and a local build takes
  it from `git describe`, so it can no longer drift: v0.3.10 reported itself as 0.3.9 in crash
  reports, the About screen and its User-Agent. No other change.

## v0.3.10 — 2026-09-24 (signed redirect ceiling)

- network: on opening, the NRO fetches the signed redirect ceiling
  (`/api/v1/network/ceiling`, Ed25519, pinned key per docs/signed-ceiling.md)
  and the Switch network profile from openpak.org, and uses the profile,
  filtered name by name through the verified ceiling, as the rule set it
  installs. Adding a family on the server no longer needs a new NRO. Fallback
  order: saved profile, downloaded bundle, bundled policy, frozen list.
- The ceiling envelope is cached on the SD card as received, and the highest
  accepted version is recorded: an older signed ceiling is refused.
- When OpenPak is installed and the set it would install now differs from the
  installed one, the tool says so ("Re-apply OpenPak and reboot"). Nothing on
  the console changes until the user selects OpenPak again. [b4d52d8]
- ci: build scratch in the runner temp, not the checkout or the shared /tmp
  [1c98d37, a15d981]. The Makefile still says `APP_VERSION := 0.3.9`.

## v0.3.9 — 2026-09-24

- hosts: override lines (a name with its own address, i.e. the second NAT
  responder nncs2-lp1) are written after the family wildcards. dns.mitm takes
  the last matching line, so since the `*.nintendo.net` family landed (0.3.7)
  nncs2 resolved to the server box too, both NAT probes hit one address and
  every P2P title failed its NAT check at once (Mario Golf 2618-0006).
  Re-run "OpenPak" on the console after updating so the hosts file is rewritten.
- Install the CTR key patch with the other OpenPak patch sets [092100e]
- ci: build only on v*.*.* tags [55f37f6]

## v0.3.8 — 2026-09-23

- failure reports: when an install or remove fails (hosts, CA, patch or system
  writes), the display will not start, or the NRO crashes, a small report is
  saved under /switch/openpak/reports (at most 8) and the user is asked before
  anything is sent to openpak.org: Send / Don't send / Always / Never, with the
  last two remembered in /switch/openpak/reports.txt. Reports from an earlier
  run (e.g. a crash) are offered at the next launch. Sent with libcurl over the
  console's ssl service; no account token is sent. [f130cd8]

## v0.3.7 — 2026-09-23

- firmware gate: OpenPak refuses to install unless the console runs 22.5.0.
  Everything it installs is derived from that firmware, so another version is
  refused before a single file is written ("OpenPak requires firmware 22.5.0 —
  this console runs X.Y.Z"). Removal (selecting Nintendo) still works on any
  firmware, and a console that is on OpenPak but has been updated off 22.5.0 is
  warned at launch and pointed at the fix.
- store installs: selecting OpenPak now also installs the console-trust patch
  that lets an OpenPak-store title (homebrew, packaged without a rights ID)
  install and launch. It is part of the OpenPak experience, not a separate
  toggle; selecting Nintendo removes exactly the files it wrote. No ES/ticket
  patch (no rights ID) and no loader/ACID patch (Atmosphère's own loader does
  not enforce ACID) are involved — only an FS NCA-header-signature kip patch.
- store installs: the FS patch is gated by the FS KIP hash (536d9384…) so it can
  never apply to another firmware, and ships only once verified on hardware.
  Until then the step is pending: it writes nothing for FS and says
  "FS patch pending hardware verification". See docs/install-trust.md and the
  candidate under docs/install-trust/.

- news: follow OpenPak channels with SetSubscriptionStatus value 2 (subscribed)
  instead of 1. On 22.5.0 status 1 means unsubscribed, so the module never
  fetched our topics and refused to store their records (0xd47d). Selecting
  Nintendo now forgets our topics (value 0) rather than leaving them listed.
- news: only the bare topic-id filter is used now; the two `topic_id='…'`
  spellings always returned 0x47d against the 22.5.0 module and are dropped.
- news: new exefs patch `openpak_news_list_no_dauth` skips the Nintendo edge
  token before the News list fetch (bcat module 0x117284) and the titles/topics
  auto-subscribe fetch (0x113f9c), the same way `openpak_news_no_dauth` skips it
  for the memory download. Both otherwise abort against OpenPak, which mints no
  token. Keyed by BCAT build id, so other firmwares ignore it.
- ca: corrected the browser-CA comment — 22.5.0's BrowserDll has no numbered
  `romfs/0/browser` directory; its roots are `browser/RootCaEtc.pem` and
  `browser/RootCaSdkAdditional.pem`. Both path layouts are still written
  (harmless where the directory is absent).

- hosts: the managed block is generated from the signed v2 platform bundle
  (PRD universal-tls-forwarding/05 SETUP-001) instead of a table compiled into
  the NRO — SD cache, then the bundle this NRO shipped with, then a frozen
  compiled fallback. Adding a forwarder becomes a server deploy.
- hosts: a bundle this build cannot fully apply is refused whole and reported,
  never applied in part; a bundle that is merely absent is not an error
- hosts: names researched but never served are no longer written into the
  block as inert comments; the inventory moved to docs/researched-hosts.md
- ui: both builds name the source and revision of the rules in force, so a
  console running the compiled fallback no longer looks current
- release: a tagged build that resolves no bundle fails instead of publishing
  the fallback
- hosts: the compiled fallback mirrors the shape the published bundle emits.
  It named twenty-seven individual services while the bundle redirects whole
  families, so a console falling back to it behaved measurably differently
  from one that did not, with nothing on screen to explain why.
- hosts: a console on OpenPak no longer reaches Nintendo at all — the claimed
  families cover system updates, the eShop CDN and the browser, not only the
  services OpenPak answers for. Intended for this project's audience, and the
  reason choosing Nintendo is a complete revert. See README.md.
- hosts: third-party families are claimed too — .nintendowifi.net (the connection
  test), .among.us, .photonengine.io, .exitgames.com, .demonware.net, .epicgames.dev,
  .ea.com, .xboxlive.com, .live.com, .mojang.com, .microsoft.com. The fallback mirrors
  switch/stable sequence 5, published 2026-09-23.
- hosts: enable refuses, before writing anything, a hosts file of 32 KB or more.
  ams_mitm aborts at boot on one that size, and a console that fatals before the
  menu can only be fixed from a PC; the parser allowed bundles large enough to
  get there.


## v0.3.5 — 2026-09-22

- install the OpenPak CA into the system SSL certificate store through the
  supported Atmosphere 1.11.2 package path (the 0.3.x system-certificate tree:
  `source/system.[ch]`, cert-store overlay, `romfs/system/` ams_mitm kip,
  `tools/test-system.py`; committed in 1c7fc5f)
- redirect all served NEX game hosts, the Diablo II: Resurrected Battle.net
  hosts, and the Animal Crossing web API to OpenPak
- news: selecting OpenPak subscribes the News module to OpenPak's channels
  and requests an immediate receive; selecting Nintendo drops only those
  subscriptions [e10bf17]
- release: the build report names the channel it fetched [3bab250]
- openpak: pin the provisioned trust root [6e5d77b]
- release: fetch the stable channel as the production-approved pilot [32a5224]
- release: the pinned tool materializes nested payload paths (website v0.41.4) [f4d2780]
- release: the variable contract covers authenticate too [dfd18fe]
- release: the pinned tool gains the build-session exchange (website v0.41.3) [d86835a]
- release: rename the pinned tool after verification [b945d74]
- release: pin the fetch tool by hash, mirror over an authenticated path [f826e53]
- release: resolve the bundle from the signed registry at build time [77df6d0]


## v0.2.1 — 2026-09-11



## v0.2.0 — 2026-09-10



## v0.1.1 — 2026-09-10



## v0.1.0 — 2026-09-10



## v0.1.0 — 2026-09-10

- Build both NROs on every push; publish them on a v* tag [0d66179]
- README: note the News routing change of 2026-09-09 [c96a4e7]
- hosts: redirect the save-data cloud (*.scsi.srv.nintendo.net) to OpenPak [21c505c]
- hosts: the audience is banned, jailbroken and emulated consoles [c283960]
- hosts: correct why the third-party names are inert [e5e1430]
- hosts: carry the third-party inventory, written inert [0d69ac2]
- Generate the patch set, including the firmware this console runs [3a7d5d2]
- Ship the Atmosphere patches, so the link page loads on a stock console [07f3001]
- Report what was written, and whether the browser patch is there [b1f3c38]
- Write the CA where the browser actually reads it [89c1204]
- Pack the icon, title and romfs into the NRO [0c06ba8]
- Install the browser CA, without which the link page never loads [270bb9c]
- Draw the OpenPak logo, and a host renderer to check it [fd0d2f9]
- Switching back to Nintendo reverts everything we wrote [81dcaca]
- Two choices, a built-in address, and a restore that actually restores [cdd12ad]
- Draw to the framebuffer instead of through SDL [9208636]
- Redirect Photon's name server too [f547fc2]
- Drop the invented default address [a97c8e5]
- Read the pad through libnx: SDL's A is physically B on a Switch [010f132]
- List-driven UI, applies on select, then offers the reboot [e3feb43]
- Add a text-mode build that loads in applet mode [935f8c6]
- openpak.nro: switch the console between Nintendo and OpenPak [77cddea]
