#!/usr/bin/env bash
set -euo pipefail
# Rebuild openpak-fetchd (console/openpak-fetchd, the boot2 hosts-updater sysmodule, title
# 01FE000000000F00) and record it for the NRO: copies the ExeFS to romfs/system/openpak-fetchd.nsp and
# writes its sha256 plus the sha256 of the source tree it came from into source/fetchd_asset.h.
# system.c refuses an nsp that is not the recorded one and tools/test-system.py checks the pair.
#
#   tools/build-fetchd.sh           build under devkitA64 (run under `flock /tmp/openpak-build.lock`,
#                                   with TMPDIR in the workspace scratch) and record
#   tools/build-fetchd.sh --check   no build: the nsp matches the recorded hash (always), and the
#                                   sources match the recorded tree hash (a warning; --strict fails)
#
# A new binary is a new hardware test: it is not the one proven on 2026-10-15 until it has been.
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
src=${FETCHD_SRC:-$repo/../openpak-fetchd}
asset=$repo/romfs/system/openpak-fetchd.nsp
header=$repo/source/fetchd_asset.h
tree_sha() {   # the files that decide the binary, in a fixed order
    (cd "$src" && cat Makefile main.json $(ls source/*.c source/*.h | LC_ALL=C sort) | sha256sum | cut -d' ' -f1)
}
recorded() { sed -n "s/^#define $1 \"\(.*\)\"/\1/p" "$header"; }
case "${1:-}" in
--check|--strict)
    have=$(sha256sum "$asset" | cut -d' ' -f1)
    [ "$have" = "$(recorded OPENPAK_FETCHD_SHA256)" ] || { echo "romfs/system/openpak-fetchd.nsp ($have) is not the binary recorded in fetchd_asset.h" >&2; exit 1; }
    now=$(tree_sha)
    if [ "$now" != "$(recorded OPENPAK_FETCHD_SOURCE_SHA256)" ]; then
        echo "warning: $src differs from the tree the recorded binary was built from ($(recorded OPENPAK_FETCHD_SOURCE_SHA256))" >&2
        [ "$1" = --strict ] && exit 1
    fi
    echo "openpak-fetchd.nsp matches its recorded hash"; exit 0;;
"") ;;
*) echo "usage: $0 [--check|--strict]" >&2; exit 2;;
esac
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
# Built in place, not in a copy: the compile directory ends up in the ELF debug info, so the NSO's
# build id (and with it the nsp bytes) depends on the path. The hardware-proven binary was built
# in console/openpak-fetchd itself, so building there reproduces it byte for byte.
src=$(cd -P -- "$src" && pwd -P)
trap 'make -C "$src" clean >/dev/null' EXIT
make -C "$src" clean >/dev/null
make -C "$src" -j4
cp "$src/openpak-fetchd.nsp" "$asset"
sha=$(sha256sum "$asset" | cut -d' ' -f1)
cat > "$header" <<HDR
// SPDX-License-Identifier: AGPL-3.0-only
// Written by tools/build-fetchd.sh; do not edit by hand. system.c refuses to install a
// romfs:/system/openpak-fetchd.nsp whose sha256 is not this one, and tools/test-system.py fails
// when the two disagree, so a binary swapped without the script cannot ship.
// SOURCE_SHA256 is over console/openpak-fetchd's Makefile, main.json and source/*.{c,h}.
#define OPENPAK_FETCHD_SHA256 "$sha"
#define OPENPAK_FETCHD_SOURCE_SHA256 "$(tree_sha)"
HDR
echo "recorded $sha; boot it on a console before a release ships it"
