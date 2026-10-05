# Store installs — console trust for OpenPak store titles

*Firmware 22.5.0 and 23.0.x, Atmosphère 1.12.0 (1.11.2 until v0.3.17), hekate →
`pkg3=atmosphere/package3-openpak` (fusee).
Shipped in v0.3.15. Proven on hardware 2026-10-04: the package this NRO builds
(`package3-openpak-store`, sha256 `4e2ac49b…19530e2`) booted tobagin's console (emuMMC, through
a hekate test entry), and a store title (JKSV) installed from the OpenPak eShop and ran.*

## What a store title is

The OpenPak eShop serves homebrew as self-built NSPs: program ids
`0x01FE000000000000`–`0x01FEFFFFFFFFFFFF`, **no rights ID** (standard key-area crypto), so
there is no ticket and ES is never involved. Inspected with hactool: the program NCA's
fixed-key header signature (sig1) is all zeros, and so is the NPDM's ACID signature. The
NCA/NPDM sig2 is hacBrewPack's own, consistent with the key it writes into the ACID.

`0x01FE…` lies inside the application range (`0x0100000000010000`–`0x01FFFFFFFFFFFFFF`), so ns,
ncm and nim treat these as ordinary applications; their content hashes are self-consistent and
pass. Two checks stop such a title, and OpenPak changes exactly those two.

## 1. Loader / ACID: a scoped Loader change

Atmosphère's Loader **does** enforce the NPDM ACID signature on retail consoles from 10.0.0
(`ValidateAcidSignature` in `stratosphere/loader/source/ldr_meta.cpp`).
`IsEnabledProgramVerification()` is permanently true there: it can only be switched off when
`spl::IsDevelopment()`, which is false on retail. A self-signed ACID is therefore rejected with
`ResultInvalidAcidSignature`: without this change the title installs but does not launch.
(An earlier version of this page said the opposite. It was wrong, and so was the matching
comment in the NRO: community sigpatch sets ship no Loader patch because they boot
Nintendo-signed content, whose ACID is valid.)

OpenPak's Loader accepts an invalid ACID signature **only** when the ACID's program id range lies
within `0x01FE000000000000`–`0x01FEFFFFFFFFFFFF`:

```cpp
const bool is_openpak_store_title = meta->acid->program_id_min.value >= 0x01FE000000000000ul &&
                                    meta->acid->program_id_max.value <= 0x01FEFFFFFFFFFFFFul;
R_UNLESS(is_signature_valid || is_openpak_store_title || !IsEnabledProgramVerification(), ldr::ResultInvalidAcidSignature());
meta->check_verification_data = is_signature_valid;
```

`check_verification_data` stays the real verdict (false for store titles), so no sig2 check is
forced; every structural check is unchanged, and every other program is verified as before.

## 2. FS / NCA header signature: a compiled-in fusee patch

FS verifies each NCA header's RSA-2048-PSS signature with the fixed header key. FS is the stock
Nintendo KIP, which Atmosphère keeps, so the change is a patch fusee applies to it at boot.

| field | value |
|---|---|
| FS KIP (as fusee hashes it) | `536d938469fe73be3c76da0333b289c0ed29f10c2a8afdff8e466142c4277359` = fusee's `FsVersion_22_5_0` |
| instruction | FS text `0x26838`: `tbz w0,#0` to the sig1-failure path, right after the header-1 RSA verify |
| patch offset (0x100 KIP header + text address) | `0x26938` |
| original → new | `E0 1B 00 36` → `1F 20 03 D5` (`nop`) |

The verify result is consumed by that one branch only; all NCA reads go through the same
reader, so the one patch covers install and launch. Found from the unique anchor of the retail
header-1 modulus (FS `.rodata` `0x1df784`, one ADRP+ADD at `0x267fc`); the offset convention
matches Atmosphère's own nogc patches for 22.5.0.

**It is not an SD IPS.** Atmosphère's fusee (1.11.2 and 1.12.0) reads no `/atmosphere/kip_patches`: the
only KIP patches it applies are compiled in (`AddPatch`, as for nogc); the IPS patcher covers
NSOs only. So the patch is compiled into OpenPak's fusee, one offset per FS it identifies:

| fusee `FsVersion` | text address | `AddPatch` offset | original → new |
|---|---|---|---|
| `22_5_0`, `22_5_0_Exfat` | `0x26838` | `0x26938` | `E0 1B 00 36` → `1F 20 03 D5` |
| `23_0_0` (`34383ee7…`) | `0x278A8` | `0x279A8` | `00 24 00 36` → `1F 20 03 D5` |
| `23_0_0_Exfat` (`fdaf1632…`) | `0x278B8` | `0x279B8` | `00 24 00 36` → `1F 20 03 D5` |

The 22.5.0 address was derived from the decrypted KIP as above (exFAT's is the same per the public
FS patch database). The 23.0.0 addresses are the same branch per the public FS patch database
(`borntohonk/Switch-Ghidra-Guides`, `fs_kip_patches.txt`), whose FS hashes are the ones
Atmosphère 1.12.0 identifies; firmware 23 needs `master_key_16`, which the workspace does not have
yet, so they are not yet checked against the decrypted KIP, nor on hardware. 23.0.1 ships the
same FS as 23.0.0. fusee identifies FS by the KIP's hash, so on any other FS nothing is patched
and store titles do not mount.
The other way, a whole pre-patched `FS.kip1` in `/atmosphere/kips`, is ruled out twice: it is
Nintendo code, not distributable, and fusee cannot identify a modified FS by hash, which is
boot-fatal ("Failed to identify FS!") with emuMMC.

Both source changes are `tools/atmosphere-store-trust.patch`; `tools/build-system-module.sh`
builds them (with the SSL `ams_mitm`) and `romfs/system/SOURCE.txt` records what ships.

## Delivery: one boot package

Everything above arrives through the NRO's **OpenPak** selection, like the SSL `ams_mitm`. On
enable, `openpak_system_install` (source/system.c) reads the official `/atmosphere/package3`,
refuses anything but the exact Atmosphère 1.12.0 release (sha256 `3cc9d6ca…`; 1.11.2's
`f162a419…` until v0.3.17), and builds
`/atmosphere/package3-openpak` from it:

- `ams_mitm` swapped for `romfs/system/ams_mitm-1.12.0.kip`;
- `Loader` swapped for `romfs/system/loader-1.12.0.kip` (KIP1, name `Loader`, program id
  `0x0100000000000001`, the original's header fields and capabilities);
- `fusee` swapped for `romfs/system/fusee-1.12.0.bin`.

Loader is not the last KIP and the new one is 0x40 bytes larger, so the KIP region is repacked
the way `fusee/build_package3.py` lays it out: emummc, then the KIPs in table order, 16-byte
aligned from `0x100000`, at most `0x400000`, `0xCC` between and after; each content-table entry
gets its new offset and size, each KIP meta (meta 0 = emummc) its offset, size and SHA-256. fusee
sits at `0x7C0000`, at most `0x20000`, zero padded, its size field updated. Every meta hash, the
existing layout and every input are checked before a byte changes. fusee's two MTC overlays
stay as they are: a build from these sources produces identical ones. `make test` builds the
package from the official one and requires sha256 `4e2ac49b…`, byte for byte the package that
booted (`tools/test-system.py`; the official package and that reference are local inputs from
the workspace, skipped where absent).

`hekate_ipl.ini`'s standard `pkg3=`/`fss0=` entry is pointed at the new file; the official
`package3` is never touched (`ams_mitm` keeps it open while HOS runs).

**Upgrading.** Every console enabled before v0.3.15 has the `ams_mitm`-only package at
`package3-openpak`. Enable recognises that file — it rebuilds it from the same official package
— and replaces it. Any other file there is somebody else's: it is kept and setup stops with
"OpenPak boot package changed; existing file preserved". Re-applying finds the file already
right and does not rewrite it.

**Another Loader on the card.** fusee loads `/atmosphere/kips/*.kip` and `*.kip1` before the
package and keeps the first KIP for each program id, so a Loader there (Horizon OC's `hoc.kip`
is one) replaces OpenPak's and store titles stop launching. After a successful enable the NRO
looks for one, the way fusee would, and names it in a warning; setup still succeeds and the file
is never touched.

## What was retired

v0.3.7–v0.3.14 carried a "Store installs" step that would copy an FS IPS into
`/atmosphere/kip_patches/openpak_fs_no_ncasig/`. Its payload never held an `.ips` (it always
reported *pending*), and this fusee would not have read one anyway. The step, its payload folder
and the candidate marker are gone. No release ever wrote a file there, so there is nothing to
clean up; anything in that folder is not OpenPak's and is left alone.

## Firmware gate

Everything OpenPak installs is derived from the firmwares it knows, so the whole OpenPak
selection is refused unless the console runs **22.5.0, 23.0.0 or 23.0.1**
(`setsysGetFirmwareVersion`), before a file is
written. Selecting Nintendo works on any firmware. Inside the boot package the FS patch has its
own gate, the FS hash above; the Loader change applies only to the `0x01FE…` range.

## Recovery

Selecting **Nintendo** points hekate back at the official `package3`; the inactive
`package3-openpak` stays on the card and is reused if identical. If a console does not boot
through OpenPak's entry, edit `bootloader/hekate_ipl.ini` in a PC card reader and change
`pkg3=atmosphere/package3-openpak` back to `pkg3=atmosphere/package3` (OpenPak's backup of the
original is `/switch/openpak/system/boot.original`). Nothing here changes the NAND.

Not yet seen on hardware: a sysMMC boot through this package, and the upgrade from an
`ams_mitm`-only `package3-openpak` performed by the NRO itself (the package was put in place
through a hekate test entry).

## Provenance

The 22.5.0 FS offset comes from owner-local 22.5.0 binaries (`openpak-firmware-audit-22.5.0`),
the 23.0.0 ones from the public FS patch database (above), and the Loader reasoning from the
Atmosphère source (the ACID check reads the same in 1.11.2 and 1.12.0); workspace notes in
`scratch/hbstore/trust/FINDINGS.md`. No Nintendo code is copied or shipped: the bundled files are
Atmosphère (GPL) builds, and the FS change is four bytes fusee writes at boot.
