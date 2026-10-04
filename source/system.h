#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool openpak_system_install(char *err, int errlen);
bool openpak_system_remove(char *err, int errlen);
bool openpak_exosphere_blank(bool blank, char *err, int errlen);   // blank_prodinfo_emummc := blank, backup kept
// A Loader KIP in /atmosphere/kips that fusee would load ahead of OpenPak's: its file name.
bool openpak_loader_override(char *name, int len);
// Pure transformations used by the installer and host-side regression checks.
// Swaps ams_mitm into the official package3; with loader and fusee (both or neither) also the
// store-trust Loader KIP and fusee. Without them it is the package builds before store trust wrote.
bool openpak_package_build(uint8_t *package, size_t size, const uint8_t *kip, size_t kip_size,
                           const uint8_t *loader, size_t loader_size, const uint8_t *fusee, size_t fusee_size);
bool openpak_store_is_ours(const uint8_t *source, size_t source_size, const uint8_t *ca, size_t ca_size);
// The overlay on the SD card is OpenPak's (or not there at all), so a re-apply may replace it.
bool openpak_store_file_is_ours(void);
bool openpak_store_replace(const uint8_t *source, size_t source_size,
                          const uint8_t *ca, size_t ca_size, uint8_t **out, size_t *out_size);

bool openpak_boot_build(const uint8_t *source, size_t size, uint8_t **out, size_t *out_size);
// /atmosphere/config/override_config.ini: the Album opens the Album, the Homebrew Menu only while
// R is held. 1: *out is the file edited to that (from no file: NULL); 0: it already is;
// -1: its hbl setup is somebody else's choice, left alone; -2: out of memory.
int openpak_album_build(const uint8_t *source, size_t size, uint8_t **out, size_t *out_size);
// Never part of Enable/Disable failing: false leaves the file as it is and says why in note.
bool openpak_album_install(char *note, int len);   // true: the Album opens the Album
bool openpak_album_remove(char *note, int len);    // true: the file is as it was before enable
