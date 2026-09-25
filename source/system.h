#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool openpak_system_install(char *err, int errlen);
bool openpak_system_remove(char *err, int errlen);
bool openpak_exosphere_blank(bool blank, char *err, int errlen);   // blank_prodinfo_emummc := blank, backup kept
// Pure transformations used by the installer and host-side regression checks.
bool openpak_package_build(uint8_t *package, size_t size, const uint8_t *kip, size_t kip_size);
bool openpak_store_replace(const uint8_t *source, size_t source_size,
                          const uint8_t *ca, size_t ca_size, uint8_t **out, size_t *out_size);

bool openpak_boot_build(const uint8_t *source, size_t size, uint8_t **out, size_t *out_size);
