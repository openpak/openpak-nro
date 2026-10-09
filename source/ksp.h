// SPDX-License-Identifier: AGPL-3.0-only
// Save Data Cloud: firmware 23.0.x's FS accepts key-seed packages signed with OpenPak's key
// (docs/save-data-cloud.md). hekate patches the FS KIP at every boot from bootloader/patches.ini
// when the launch entry asks for it with kip1patch=openpak_ksp; that works the same under stock
// Atmosphère, which is why this route is the durable one.
//
// Everything here is a pure transformation of file contents, so the host tests (ksp_test.c) cover
// it; the SD card side lives in system.c with the rest of the boot configuration.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENPAK_KSP_PATCH "openpak_ksp"     // the hekate patch-set name (lowercase: hekate compares it so)
#define OPENPAK_KSP_FIRMWARE "23.0.0 or 23.0.1"

// FS .rodata, the prod KeySeedPackage-signing modulus (RSA-2048, e = 65537, big endian): retail,
// as both 23.0.x FS builds carry it, and OpenPak's. Both are public keys.
extern const uint8_t openpak_ksp_retail[256];
extern const uint8_t openpak_ksp_openpak[256];

// The firmwares whose FS this patch is cut for: 23.0.0 and 23.0.1 ship the same two FS KIPs
// (FAT 34383ee799926340, exFAT fdaf163288e10805 — SHA-256 of the KIP, first 8 bytes).
bool openpak_ksp_firmware(const char *display_version);

// bootloader/patches.ini. enable: OpenPak's two [FS:…] sections at the end of the file, any earlier
// copy of them taken out first; disable: OpenPak's lines taken out, with any [FS:…] header that held
// only them. Every other line keeps its bytes. source may be NULL (no file).
// 1: *out is the new file (may be empty); 0: already so; -1: refused, the file is not one hekate
// reads as written (a NUL, a line over 511 bytes, over 64 KiB) or our set would overflow hekate's
// 16 patch sets per KIP; -2: out of memory.
int openpak_ksp_patches_build(const uint8_t *source, size_t size, bool enable, uint8_t **out, size_t *out_size);

// bootloader/hekate_ipl.ini. enable: kip1patch=openpak_ksp on the line after pkg3=/fss0=
// atmosphere/package3[-openpak] in every launch entry that boots the MMC this runs on (emummc: the
// NRO runs on emuMMC; emummc_enabled: emuMMC/emummc.ini enables it; an entry with
// emummc_force_disable=1 boots sysMMC; a stock=1 entry is never touched). disable: every
// openpak_ksp request removed.
// 1: *out is the new file; 0: nothing to change; -1: refused (not text, over 64 KiB, or enable found
// no such entry); -2: out of memory.
int openpak_ksp_boot_build(const uint8_t *source, size_t size, bool enable, bool emummc, bool emummc_enabled,
                           uint8_t **out, size_t *out_size);

// emuMMC/emummc.ini as hekate reads it: [emummc] enabled= non-zero.
bool openpak_ksp_emummc_enabled(const uint8_t *source, size_t size);

// For the log: "- line" for each line only a has, "+ line" for each only b has. Returns the length
// written (truncated to fit, always NUL-terminated).
size_t openpak_ksp_describe(const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size, char *out, size_t len);
