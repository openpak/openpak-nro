# Store installs — console trust for OpenPak-store titles

*Firmware 22.5.0 (HOS), Atmosphère 1.11.2. Nothing here has been tested on
hardware; the FS patch below is a **candidate**, not a shipped patch.*

## What a store title needs

The OpenPak store serves only homebrew titles and their updates, all packaged
**without a rights ID** — key-area (NCA key-area) crypto, no tickets. Because
there is no ticket:

- **No ES / ticket-signature patch is involved.** Only add one if the
  no-rights-ID path is ever observed to reach ES on hardware; it should not.

Two console checks otherwise stop such a title from installing and launching:

1. **FS NCA fixed-key header signature.** FS verifies the RSA-2048-PSS
   signature over each NCA header with the fixed header key. A repacked /
   self-built NCA fails it. FS is a **stock KIP** that Atmosphère keeps, so this
   is a **kip_patch**.
2. **Loader NPDM ACID signature.** The program loader verifies the ACID
   signature in the NPDM.

## Loader / ACID: no patch needed on this console

This console boots hekate with `pkg3=atmosphere/package3` (verified in
`atmosphere-active/hekate_ipl.before-ssl-test.ini`), i.e. it chainloads
Atmosphère's **fusee**. Atmosphère's `package3` ships its **own** `KIP1Loader`
and **replaces** the stock `Loader` KIP (the stock `Loader.kip1` in the firmware
is not what runs). Atmosphère's loader does not enforce the NPDM ACID signature
— this is long-standing Atmosphère behaviour and the reason community "sigpatch"
sets ship only `fs` and `es` patches and never a loader patch.

**Therefore no loader/ACID patch is delivered.** (Worth a one-line confirmation
on hardware: a no-rights-ID title that installs with only the FS patch and
launches proves the loader is not enforcing ACID. If a future Atmosphère did
enforce it, a loader patch — or Atmosphère's own ACID-key replacement — would be
needed; that is out of scope here.)

## Delivery

| Piece | Path on SD | Applied by |
|---|---|---|
| FS NCA-header patch | `/atmosphere/kip_patches/openpak_fs_no_ncasig/<fs-hash>.ips` | fusee, at boot |

- Because this console boots hekate → `pkg3=atmosphere/package3`, **fusee**
  loads the INI1 and applies `kip_patches`, so the FS kip_patch runs here.
- A console that instead let **hekate load the KIPs itself** (hekate-native KIP
  loading, not `pkg3=`) would need the patch declared in hekate's own KIP-patch
  configuration; fusee's `kip_patches` are not consulted on that path. This
  console does not boot that way, so only the fusee path is wired; the hekate
  path is documented here for completeness.
- The `.ips` filename must gate on the FS KIP so it can never touch another
  firmware. **FS 22.5.0 KIP sha256:**
  `536d938469fe73be3c76da0333b289c0ed29f10c2a8afdff8e466142c4277359`
  (name `FS`, `KIP1`). The NRO's install step only ever writes files that ship
  in its verified payload, and removal deletes exactly those names.

The NRO ships the payload in `romfs/patches/kip_patches/openpak_fs_no_ncasig/`.
It is **empty of `.ips` today**, so the "Store installs" step shows **Pending**
and installs nothing. Drop a hardware-verified IPS there (named by the FS hash
Atmosphère matches) and rebuild to arm the step.

## Firmware gate and where the step lives

The store patch is **part of the OpenPak experience**, not a separate toggle.
Selecting OpenPak installs it alongside the hosts, CA and News patches; selecting
Nintendo removes exactly the files it wrote, in the same removal path.

Because everything OpenPak installs is derived from one firmware, the whole
install is **refused unless the console runs 22.5.0**: the NRO reads
`setsysGetFirmwareVersion().display_version` and, on any mismatch, writes nothing
and shows "OpenPak requires firmware 22.5.0 — this console runs X.Y.Z". Removal
is allowed on **any** firmware, so a console updated after installing can always
clean up, and a console left on OpenPak after an update is warned at launch.

The FS patch itself carries a second, independent gate: its `.ips` is named by
the FS KIP hash, so Atmosphère applies it only to a matching FS. A 22.5.0 console
with an unexpected (modified) FS therefore never gets a half-applied FS patch —
Atmosphère simply ignores the mismatched IPS. (The NRO cannot cheaply hash the
running FS KIP from homebrew, so the authoritative FS gate is the hash-named IPS
plus the firmware-version refusal above.)

When the payload has **no verified `.ips`** (today), the OpenPak install still
succeeds for everything else and the store piece is reported as pending —
"FS patch pending hardware verification" — writing nothing for FS. Once an IPS is
dropped into the payload, the same OpenPak selection copies it to
`/atmosphere/kip_patches/openpak_fs_no_ncasig/` and the reboot applies it.

## FS NCA-header-signature patch — candidate (NOT shipped)

Build ids / identity:

- FS is program `0100000000000000` in the INI1, delivered as `KIP1` `FS`.
- FS KIP sha256 `536d9384…c4277359`; the reconstructed ELF is `m0/FS.elf`
  (segments: text@0, ro@0x1db000, data@0x244000; KIP1 header 0x100). Ghidra
  project `m0/ghidra_proj` (`FSm0`), fully analysed.

Anchor found by static analysis:

- The NCA header processor is the function at FS vaddr **0x26570**
  (`sub sp,#0x580` — a large frame holding a decrypted 0xC00 header). It
  compares the header magic against `NCA3` (`0x3341434E`, built at 0x26754 and
  0x26794: `mov w9,#0x434e ; movk w9,#0x3341,lsl#16 ; cmp w8,w9`) and calls the
  version-magic helper at **0x16e3a0** (accepts `NCA0/1/2`, else aborts via the
  `R_ABORT`-style logger at **0x54df0**).
- The fixed-key **RSA-2048-PSS-SHA256 header signature** verification is within
  or just above this processor (in `NcaReader` init / its caller). It has **no
  string anchor** (release build, error strings stripped), so the exact
  conditional branch to patch was **not isolated from raw disassembly**.

Proposed method (standard FS sigpatch shape): in `NcaReader` init, force the
branch that acts on the header-signature verify result to always take the
success path (NOP the conditional branch to the signature-error handler, or
substitute a success result), leaving structural checks intact — analogous to
`news-*-no-dauth`, which forces a helper's result to success and lets the caller
proceed.

**Why no candidate `.ips` bytes are given yet:** a kip patch to FS is
boot-fatal if wrong, and this one cannot be pinned to exact original/new bytes
from static disassembly alone. The remaining step is to decompile 0x26570 and
its caller in the `FSm0` Ghidra GUI, read the header-signature verify + its
result branch, capture the original bytes and the minimal success-forcing edit,
then validate on hardware per the procedure below. Only then does an IPS get
named by the FS hash and dropped into the payload. Producing invented bytes now
would be worse than shipping nothing.

## Hardware test procedure (for the owner)

Do this only after the candidate above is turned into real bytes.

1. **Back up first.** Copy the whole SD `/atmosphere/` directory, and at least
   `/atmosphere/kip_patches/` and `/bootloader/`, to a PC. A bad FS kip patch
   makes the console **fatal at boot** — recoverable only by editing the SD in a
   PC card reader.
2. **Place the patch.** Put the verified IPS at
   `sd:/atmosphere/kip_patches/openpak_fs_no_ncasig/<fs-hash>.ips`, or select
   **Store installs** in this NRO (once armed) and accept the reboot. The file
   must be named by the FS KIP hash Atmosphère matches, so it is ignored on any
   other firmware.
3. **Reboot through the same boot entry** (hekate → `pkg3=atmosphere/package3`).
4. **Success looks like:** the console boots normally to Home, and a
   no-rights-ID OpenPak-store title **installs and launches**. Confirm the
   loader is not enforcing ACID by the launch succeeding with only this patch.
5. **Failure looks like:** an Atmosphère **fatal error** screen at boot (an
   error report is written to `/atmosphere/fatal_errors/`), or the title fails
   to install/launch with an NCA / signature error. Either means the patch bytes
   or offset convention are wrong.
6. **Recover:** power off; in a PC card reader, delete
   `sd:/atmosphere/kip_patches/openpak_fs_no_ncasig/` (or the whole
   `kip_patches` you added); restore the backup if needed; boot again. Removing
   the file fully reverts — the patch is applied only at boot and changes nothing
   on the NAND.

## Provenance

Derived only from the owner-local 22.5.0 binaries under
`/home/tobagin/openpak-firmware-audit-22.5.0` (`m0/FS.elf`, `fs-extract/`,
`atmosphere-active/`). No decompiled code is copied. Clean-room: facts only.
