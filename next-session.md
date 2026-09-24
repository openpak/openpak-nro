# Next session — console/openpak-nro

Updated 2026-09-16.

**New and untested on hardware (2026-09-16):** `source/news.[ch]` — selecting
OpenPak now subscribes the News module to the four OpenPak channels
(`SetSubscriptionStatus` 40100) and requests an immediate receive (30300) on
news:a; selecting Nintendo drops only those subscriptions, never
`ClearSubscriptionStatusAll`. The patches and the topics redirect were
already shipped; following the channels was the missing step, so News stayed
empty. The filter argument is undocumented: three spellings are probed
read-only and only a filter naming an `openpak_` topic is written. Both UIs
rebuilt, `make test` passes on the host. Which spelling is real comes from
the News probe v0.21 run — see `../../news/next-session.md`.

The CFW console switcher: enable writes a marked `dns_mitm` hosts block
pointing every OpenPak-served name at the server; disable removes it and
leaves the files exactly as they were. Committed state is v0.2.2 (release
pipeline plus Among Us matchmaker, NAT-check, News-patch and scsi save-data
redirects). The uncommitted 0.3.x work adds system-certificate support —
the OpenPak CA in the SSL cert store via a replacement `ams_mitm` package —
and native Stardew online is already confirmed working with it on hardware.

## Where things stand

- Committed (v0.1.0..v0.2.2): the toggle NRO (applet + console builds),
  hosts research incl. inert third-party inventory, News patches keyed
  by BCAT build id (HOS 22.5.0), both NAT-check names, save-data cloud
  (`*.scsi.srv.nintendo.net`), Among Us matchmaker names; release
  pipeline resolves the bundle from the signed registry via a
  hash-pinned fetch tool on the self-hosted runner.
- Uncommitted (15 dirty paths — the 0.3.x system-certificate work):
  - `source/system.[ch]`, `main.c`/`main_console.c`/`Makefile`: enable
    verifies the official `/atmosphere/package3` SHA-256, builds
    `package3-openpak` with the replacement `ams_mitm`, updates hekate
    `pkg3`/`fss0` entries; disable restores originals.
  - Cert-store overlay: `tools/build-certstore-overlay.py` reads the
    console's certificates, replaces expired cert 1033 with the bundled
    OpenPak CA (`romfs/ca.der`). Backups in `/switch/openpak/system`.
  - `romfs/system/` (`ams_mitm-1.11.2.kip` + SOURCE.txt), built by
    `tools/build-system-module.sh` from `tools/atmosphere-ssl.patch`
    (source: `../atmosphere-ssl-overlay`); `tools/cert-probe/` NRO;
    `tools/test-system.py` install/reapply/rollback tests.
  - `hosts.c`: `*.five.nintendo.net` (game invitations) added; plus
    untracked `CHANGELOG.md`, `docs/`, `prds/` stubs.
- README documents 0.3.1 as current: 0.3.0's active-package replacement
  failed on-console; 0.3.1 swaps the activation method. Native Stardew
  online confirmed working with the system change; the revised
  installer still needs a hardware round-trip.

## Next steps

1. Hardware round-trip of the revised 0.3.1 installer: enable → reboot →
   launch the game normally; disable → original setup restored. Run
   `python3 tools/test-system.py --package3 <local official package3>`
   first (full install/reapply/rollback + interrupted-activation).
2. After the round-trip: review and commit the 0.3.x tree (decide which
   built artifacts — kip, cert-probe binaries — belong in the repo), tag
   v0.3.1.
3. Keep `tools/atmosphere-ssl.patch` in lockstep with the Atmosphère
   version pinned (1.11.2) — see `../atmosphere-ssl-overlay`.
4. Remember the standing user instruction: re-select OpenPak with this
   build even if already active, then reboot.
5. Once the News probe reports which subscription filter the service
   accepts, drop the other two candidates from `source/news.c`.

## Pointers

- `README.md` — the full behaviour contract (hosts policy, News patches,
  system-certificate support, two builds, build commands).
- `source/hosts.c` + `source/hosts_test.c` — the managed host list;
  `make test` is the host-side check.
- `romfs/system/SOURCE.txt` — ams_mitm source revision and local patch.
- `../atmosphere-ssl-overlay` — the one-file upstream patch this builds
  from.

## Scratch (research and throwaway work)

Decompiles, Ghidra projects, dumps, exefs/romfs extracts, packet captures,
strace and emulator logs, probe harnesses: put them in
`~/REPOS/Openpak/scratch/<topic>`. That folder is a local mount of the media pool,
outside every repository, so nothing in it is committed. Never use `/tmp` (a
shared 15 GB RAM disk) or elsewhere on `/home` for this. Keys and signing
material never go there. Rule: `docs/playbooks/conventions.md` in the workspace.
