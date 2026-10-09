# Save Data Cloud — the console's FS trusts OpenPak's key-seed packages

*Firmware 23.0.0 / 23.0.1, hekate (checked against v6.5.4). Not released yet, and not yet run on
hardware. The research behind it is `scratch/harbor-eden/research/olsc-re/fs-transfer.md` §6–§8.*

## Why a console patch

A save-data transfer (`ISaveDataTransferManagerVersion2`) only imports an archive once FS has
accepted a **key-seed package** for it, and FS checks that package's RSA-2048 signature
(e = 65537) against a public modulus in its own `.rodata`: the prod KeySeedPackage-signing key that
config-init copies to `cryptoConfig+0x100`. A server cannot sign with Nintendo's key, so OpenPak
swaps the modulus for its own, the way the News patch swaps the BCAT key. The verifier still runs;
it now trusts what OpenPak's saves server signs. Nintendo's own save cloud stops verifying on a
patched console, the same trade as BCAT under the News key.

| FS KIP (23.0.x) | hekate / fusée id (KIP SHA-256, first 8 bytes) | modulus vaddr | `.rodata` offset |
|---|---|---|---|
| FAT `0100000000000819` | `34383ee799926340` | `0x1e4450` | `0x5450` |
| exFAT `010000000000081b` | `fdaf163288e10805` | `0x1ef450` | `0x5450` |

23.0.0 and 23.0.1 ship the same two FS KIPs. Both builds carry byte-identical moduli at that
offset.

## How it reaches the console: hekate's `bootloader/patches.ini`

hekate rebuilds package2 on every boot and patches KIPs from its built-in table plus
`bootloader/patches.ini` (`pkg2_patch_kips`, `pkg2.c`). A `pkg3=`/`fss0=` boot unpacks only the
KIPs, kernel and exosphère from the package: **fusée never runs on a hekate boot**, so a
compiled-in fusée `AddPatch` does nothing there. patches.ini is therefore the route that works on
this console, and it keeps working if the console goes back to stock Atmosphère. The fusée route in
fs-transfer.md §6.3-interim only covers a standalone fusée boot and is not built.

**Selecting OpenPak** (on 23.0.0/23.0.1) does two things, in this order:

1. Appends OpenPak's block to `bootloader/patches.ini` (creating the file if there is none):

   ```
   # OpenPak Save Data Cloud (openpak.nro): firmware 23.0.x FS trusts OpenPak's save transfer key.
   # OpenPak Save Data Cloud: undo by selecting Nintendo in OpenPak, or delete these lines and every kip1patch=openpak_ksp in bootloader/hekate_ipl.ini.
   [FS:34383ee799926340]
   .openpak_ksp=1:0x5450:0x40:<retail 0x00-0x3F>,<OpenPak 0x00-0x3F>
   .openpak_ksp=1:0x5490:0x40:<retail 0x40-0x7F>,<OpenPak 0x40-0x7F>
   .openpak_ksp=1:0x54D0:0x40:<retail 0x80-0xBF>,<OpenPak 0x80-0xBF>
   .openpak_ksp=1:0x5510:0x40:<retail 0xC0-0xFF>,<OpenPak 0xC0-0xFF>
   [FS:fdaf163288e10805]
   (the same four lines)
   # OpenPak Save Data Cloud ends here.
   ```

   Section `1` is `.rodata`, and the offset counts from the start of the decompressed section.
   There are four 0x40-byte lines because hekate reads 511 bytes per line, and one 0x100-byte line
   would be 1054.
2. Adds `kip1patch=openpak_ksp` on the line after `pkg3=`/`fss0=atmosphere/package3[-openpak]` in
   the launch entries that boot **the same MMC the NRO is running on**. The NRO asks exosphère
   (spl config 65007) whether it is on emuMMC. An entry with `emummc_force_disable=1` boots
   sysMMC; any other entry boots emuMMC when `emuMMC/emummc.ini` has `enabled=` non-zero. An entry
   with `stock=1` is never touched. The line goes into the managed `hekate_ipl.ini` (`boot.managed`)
   that OpenPak already writes, so it is removed together with the package3 switch.

**Selecting Nintendo** restores `hekate_ipl.ini` from `boot.original` as before. Once no entry asks
for `openpak_ksp`, it takes OpenPak's lines out of `patches.ini`, along with any `[FS:…]` header
that held only them. If OpenPak created the file, the file is deleted. If `hekate_ipl.ini` still asks
(somebody edited it, so the restore was refused), `patches.ini` is left alone and the NRO says so.

### What is kept

Every other line of both files keeps its bytes, including CRLF line endings, comments, other KIPs'
sections and other patches for the same FS (sigpatch packs ship `[FS:34383ee799926340]` too:
hekate merges sections with the same id). The only thing OpenPak may add to the user's own text is
a final newline, when the file had none. Before each edit, `patches.ini` is copied to
`/switch/openpak/system/patches.ini.previous`. Re-selecting OpenPak with the block already at the
end writes nothing.

## Safety

- **Firmware gate.** Save Data Cloud is set up only when `setsys` reports 23.0.0 or 23.0.1. The
  `[FS:…]` sections only match those two KIP hashes, so on any other FS hekate skips them.
- **hekate checks the bytes it replaces.** Each line carries the retail bytes (`src`). hekate writes
  `dst` only if the KIP holds `src` (or already `dst`). Otherwise it prints "Patch mismatch" and
  stops. A wrong `src` cannot corrupt FS; a wrong `dst` would only break save-cloud verification.
  FS still boots.
- **Only entries for this MMC.** A sysMMC on another firmware never gets the request (see the boot
  stop below).
- **Refusals leave files alone.** OpenPak does not edit a `patches.ini` that has a NUL byte, a line
  over 510 bytes (hekate would read it as two lines), or more than 64 KiB. It also does not edit one
  whose FS sections would go past hekate's 16 patch sets per KIP (hekate does not check that
  bound). In each case OpenPak still goes on, the request is not added, and the NRO shows why.
  None of this ever makes the OpenPak/Nintendo switch fail.
- **Log and dry run.** Every enable, and every remove that has something to do, writes
  `/switch/openpak/system/save-data-cloud.log`. It records the firmware, the MMC, and the lines
  added and removed in both files. With an empty file at `/switch/openpak/save-data-cloud.dry-run`
  on the card, OpenPak works everything out and logs it, but writes neither file.

### The retail bytes: shipped, not read from the console

`src` has to be the retail modulus. There were two ways to get it to the NRO:

- **Read it at run time from the console's FS KIP.** The NRO cannot do this. FS lives in package2,
  which is encrypted with a key the NRO does not have (and must not ship). The other way is to
  attach a debugger to the running FS process, but FS serves the SD card the NRO itself runs from,
  so a debug-attach that suspends it could hang the console. Either option would add key handling
  or a new hang risk to defend a 256-byte public value.
- **Ship it as a public-key constant (chosen).** Atmosphère ships Nintendo's NCA header and ACID
  moduli the same way. hekate compares `src` with the real KIP at every boot before writing, so a
  wrong constant can only stop the boot at hekate's prompt; it cannot patch the wrong bytes. The
  constant is pinned by SHA-256 in `source/ksp.c` and `source/ksp_test.c`. When the owner-local
  decrypted 23.0.x FS dumps are in the workspace, `make test` also checks it against both real KIPs
  and applies the block to them the way hekate does: 256 bytes change, all inside `.rodata`
  0x5450–0x554F.

OpenPak's modulus (`dst`) is its public key; the saves server holds the private half.

## Things to know before relying on it

- **A firmware update while OpenPak is on stops the boot at hekate.** With a new FS the sections no
  longer match, the entry still asks for `openpak_ksp`, and hekate shows *Failed to apply
  'openpak_ksp'! Press POWER to continue*. POWER boots on without the patch. Select **Nintendo**
  before updating the firmware (OpenPak has to be set up again for a new firmware anyway).
- **Replacing `patches.ini` wholesale while OpenPak is on** (some sigpatch packs do) removes the
  sections while the entry still asks for them, which leads to the same prompt. Select OpenPak
  again afterwards: the block is appended to the new file.
- **hekate must know FS 23.0.0** (it is in hekate 6.5.4's built-in table). The emuMMC patch keys off
  the built-in entry, and OpenPak's section merges into it.

## Recovery without the console (PC card reader)

1. In `bootloader/hekate_ipl.ini`, delete every `kip1patch=openpak_ksp` line. If `openpak_ksp` was
   added to a `kip1patch=` line of your own, remove only that name and its comma. Do this first:
   an entry that asks for a set `patches.ini` no longer has stops the boot (see above).
2. In `bootloader/patches.ini`, delete from the first `# OpenPak Save Data Cloud` line through
   `# OpenPak Save Data Cloud ends here.`. If the file is then empty and was not there before
   OpenPak, delete it.
3. Optional: `/switch/openpak/system/patches.ini.previous` is the file as it was before OpenPak's
   last edit, and `boot.original` in the same folder is `hekate_ipl.ini` from before OpenPak.

Nothing here touches NAND. The patch exists only in the package2 that hekate builds in memory at
boot.

## Not yet verified

- On hardware: the console booting with the block, and FS accepting a package signed by OpenPak's
  key (fs-transfer.md §7.1).
- That this console's boot entry is a hekate `pkg3=`/`fss0=` launch and not a chainloaded fusée
  (fs-transfer.md §7.6). With a chainloaded fusée, patches.ini is not read at all.
