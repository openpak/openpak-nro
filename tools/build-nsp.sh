#!/bin/sh
# Build OpenPak Switch Patcher and wrap it as the NSP consoles install (title 01FE000000000000),
# on the workstation: packaging needs the console keys (~/.switch/hbp.keys, read by hbstore-tools'
# hacBrewPack; never copied or printed here), which never go to CI.
#
#   tools/build-nsp.sh [--out DIR] [--version X.Y.Z]
#
# The NSP lands in DIR (default ~/REPOS/Openpak/scratch/switch-patcher) as
# OpenPak-Switch-Patcher-<version>.nsp. This is a development build: it carries the romfs as
# checked in, not the registry-verified network bundle a tagged CI build has. Release NSPs wrap the
# CI build instead (hbstore-tools: hb2nsp.py openpak-nsp <tag>).
set -eu
cd "$(dirname "$0")/.."
OUT="$HOME/REPOS/Openpak/scratch/switch-patcher"
VERSION=""
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        *) echo "usage: $0 [--out DIR] [--version X.Y.Z]" >&2; exit 2 ;;
    esac
done
HBSTORE_TOOLS="${HBSTORE_TOOLS:-$HOME/REPOS/Openpak/clients/hbstore-tools}"
[ -r "$HOME/.switch/hbp.keys" ] || { echo "no ~/.switch/hbp.keys: NSPs are packaged on the workstation only" >&2; exit 1; }
[ -x "$HBSTORE_TOOLS/hacBrewPack/hacbrewpack" ] || { echo "hbstore-tools not set up at $HBSTORE_TOOLS (see its README)" >&2; exit 1; }
# The build container has no git, so the version is worked out here.
[ -n "$VERSION" ] || VERSION=$(git describe --tags --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)
[ -n "$VERSION" ] || VERSION=dev
make clean   # the version is compiled in and in the NACP; nothing tracks it as a dependency
nice -n 10 podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkita64 make APP_VERSION="$VERSION"
mkdir -p "$OUT"
python3 "$HBSTORE_TOOLS/hb2nsp.py" openpak-nsp-file switch-patcher.nro "$OUT/OpenPak-Switch-Patcher-$VERSION.nsp"
