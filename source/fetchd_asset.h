// SPDX-License-Identifier: AGPL-3.0-only
// Written by tools/build-fetchd.sh; do not edit by hand. system.c refuses to install a
// romfs:/system/openpak-fetchd.nsp whose sha256 is not this one, and tools/test-system.py fails
// when the two disagree, so a binary swapped without the script cannot ship.
// SOURCE_SHA256 is over console/openpak-fetchd's Makefile, main.json and source/*.{c,h}.
#define OPENPAK_FETCHD_SHA256 "3558026eef9e014b2f02aff4b16c29a7f6e3eb2318a180ec0cd7e733d0acf92f"
#define OPENPAK_FETCHD_SOURCE_SHA256 "6056294c39aafaacafb9379c87efa41c91d2b2921ef3a2cfad6209d11a42ce9d"
