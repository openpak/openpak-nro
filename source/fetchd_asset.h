// SPDX-License-Identifier: AGPL-3.0-only
// Written by tools/build-fetchd.sh; do not edit by hand. system.c refuses to install a
// romfs:/system/openpak-fetchd.nsp whose sha256 is not this one, and tools/test-system.py fails
// when the two disagree, so a binary swapped without the script cannot ship.
// SOURCE_SHA256 is over console/openpak-fetchd's Makefile, main.json and source/*.{c,h}.
#define OPENPAK_FETCHD_SHA256 "85af6a5b39b81708f2624e53bb0333bb3f1b758e72af76d253d9af494eb26b11"
#define OPENPAK_FETCHD_SOURCE_SHA256 "b931e866a2287f0817a1c25f19d39ab29c12d4f22437918c2c9335f9796a37e1"
