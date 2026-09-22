# Changelog — openpak-nro

Generated from git history on 2026-09-15. `git log` stays the source
of truth; this file is the readable summary.

## Unreleased


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
