# Next session — console/openpak-nro

Updated 2026-09-26.

**v0.3.13 / v0.3.14 (2026-09-26):** two changes on top of v0.3.12, both host-tested, neither on
hardware yet.

1. `source/system.c` — the certificate-overlay guard. Re-applying OpenPak failed with
   "Certificate overlay changed; existing file preserved" on a console whose overlay is
   OpenPak's own but not byte-identical to `/switch/openpak/system/certificate.managed`
   (an older build wrote it from the live 63-entry list; v0.3.12 builds from the 127-entry
   CertStore file). `openpak_store_file_is_ours()` now yields when slot 1033 carries the
   OpenPak CA, or when the overlay is simply not there; a store OpenPak did not write is
   still preserved and still stops setup. `tools/test-system.py` covers ownership without a
   package3; the replace-on-reapply paths need
   `python3 tools/test-system.py --package3 <official package3>`, which has not been run
   (no local package3 on this machine).
2. `source/update.[ch]` + `netfetch.c` + both mains — self-update. The release check rides
   on the launch fetch; the offer is a prompt ahead of the reboot question; applying
   downloads the build's own release asset over the NRO hbmenu loaded and
   `envSetNextLoad`s it. No signature on the binary yet: HTTPS from the pinned repository
   plus an NRO header check. If a signed release manifest ever exists, verify it in
   `openpak_update_apply` with `ed25519.c`.

Both NROs build in the devkitA64 image. What needs hardware: the failing re-apply on the
console that reported it, and one update round-trip (offer, download, relaunch). The round-trip
cannot be tried from a console running v0.3.13 or earlier's predecessors — copy v0.3.13 across
by hand once, then the next tag is the first real test of the updater.

Current status 2026-09-26: latest tag v0.3.14, no tracked changes. The repository is public
as of 2026-09-26 — the self-update reads GitHub's release API, which answers nothing to an
anonymous console on a private repository. v0.3.13's NRO validation rejected every real
release (the header length excludes the appended assets); v0.3.14 fixes it, so v0.3.13 is the
last build that cannot update itself.

Earlier status 2026-09-24: latest tag v0.3.10 (a15d981), no tracked changes. Since
the doc was last right: the 0.3.x system-certificate tree was committed
(1c7fc5f) and released in v0.3.5 with the News subscription and NEX / Diablo
II hosts; v0.3.7 hosts from the signed policy bundle, News status 2 + list
no-dauth patch, firmware-gated store-install trust; v0.3.8 consented failure
reports; v0.3.9 overrides after family wildcards + CTR key patch; v0.3.10
signed redirect ceiling + live Switch profile. Makefile `APP_VERSION` is
still 0.3.9 in the v0.3.10 tag.

**Shipped in v0.3.5 (2026-09-16 work):** `source/news.[ch]` — selecting
OpenPak now subscribes the News module to the four OpenPak channels
(`SetSubscriptionStatus` 40100) and requests an immediate receive (30300) on
news:a; selecting Nintendo drops only those subscriptions, never
`ClearSubscriptionStatusAll`. The patches and the topics redirect were
already shipped; following the channels was the missing step, so News stayed
empty. v0.3.7 settled the filter: only the bare topic-id spelling is used
(the two `topic_id='…'` forms always returned 0x47d on 22.5.0), subscription
status is 2 (1 means unsubscribed on 22.5.0), and Nintendo resets our
topics to 0. See `../../news/next-session.md`.

The CFW console switcher: enable writes a marked `dns_mitm` hosts block
pointing every OpenPak-served name at the server; disable removes it and
leaves the files exactly as they were. Released state is v0.3.10; the 0.3.x
line added system-certificate support — the OpenPak CA in the SSL cert store
via a replacement `ams_mitm` package — and native Stardew online is already
confirmed working with it on hardware.

## Where things stand

- Committed (v0.1.0..v0.2.2): the toggle NRO (applet + console builds),
  hosts research incl. inert third-party inventory, News patches keyed
  by BCAT build id (HOS 22.5.0), both NAT-check names, save-data cloud
  (`*.scsi.srv.nintendo.net`), Among Us matchmaker names; release
  pipeline resolves the bundle from the signed registry via a
  hash-pinned fetch tool on the self-hosted runner.
- Committed in 1c7fc5f (2026-09-15), released in v0.3.5 — the 0.3.x
  system-certificate work:
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
  - `hosts.c`: `*.five.nintendo.net` (game invitations) added; plus the
    `CHANGELOG.md`, `docs/`, `prds/` stubs (2026-09-15 docs pass).
- v0.3.7..v0.3.10 (2026-09-23/24): hosts block generated from the signed
  policy bundle (compiled fallback mirrors its families; 32 KB hosts refused);
  22.5.0 firmware gate; store-install FS trust patch (pending hardware
  verification); consented failure reports; overrides written after family
  wildcards (nncs2 fix); CTR key patch; signed redirect ceiling + live
  Switch profile. See CHANGELOG.md.
- README documents 0.3.1 as current: 0.3.0's active-package replacement
  failed on-console; 0.3.1 swaps the activation method. Native Stardew
  online confirmed working with the system change; the revised
  installer still needs a hardware round-trip.

## Next steps

1. Hardware round-trip of the revised 0.3.1 installer: enable → reboot →
   launch the game normally; disable → original setup restored. Run
   `python3 tools/test-system.py --package3 <local official package3>`
   first (full install/reapply/rollback + interrupted-activation).
2. ~~Review and commit the 0.3.x tree, tag v0.3.1~~ — committed in 1c7fc5f,
   released as v0.3.5 (no v0.3.1 tag); now at v0.3.10.
3. Keep `tools/atmosphere-ssl.patch` in lockstep with the Atmosphère
   version pinned (1.11.2) — see `../atmosphere-ssl-overlay`.
4. Remember the standing user instruction: re-select OpenPak with this
   build even if already active, then reboot.
5. ~~Drop the other two News filter candidates~~ — done in v0.3.7 (1196c8a).
6. Bump `APP_VERSION` in the Makefile before the next tag (v0.3.10 still
   reports 0.3.9).

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
