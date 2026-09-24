# Changelog — openpak-nro

Generated from git history on 2026-09-15. `git log` stays the source
of truth; this file is the readable summary.

## v0.3.9 — 2026-09-24

- hosts: override lines (a name with its own address, i.e. the second NAT
  responder nncs2-lp1) are written after the family wildcards. dns.mitm takes
  the last matching line, so since the `*.nintendo.net` family landed (0.3.7)
  nncs2 resolved to the server box too, both NAT probes hit one address and
  every P2P title failed its NAT check at once (Mario Golf 2618-0006).
  Re-run "OpenPak" on the console after updating so the hosts file is rewritten.

## Unreleased

- failure reports: when an install or remove fails (hosts, CA, patch or system
  writes), the display will not start, or the NRO crashes, a small report is
  saved under /switch/openpak/reports (at most 8) and the user is asked before
  anything is sent to openpak.org: Send / Don't send / Always / Never, with the
  last two remembered in /switch/openpak/reports.txt. Reports from an earlier
  run (e.g. a crash) are offered at the next launch. Sent with libcurl over the
  console's ssl service; no account token is sent.
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
  supported Atmosphere 1.11.2 package path
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
