#!/usr/bin/env bash
set -euo pipefail
# EXPERIMENT, not shipped: builds ams_mitm 1.12.0 with the four shipped patches plus
# tools/atmosphere-update-probe.patch (a read-only content probe: ncm, lr and fs reads of the OpenPak title, no network,
# logging to /atmosphere/openpak-probe.log). Output goes to scratch/probe/out, never to romfs/.
#   TMPDIR=<workspace>/scratch/tmp flock /tmp/openpak-build.lock bash tools/build-update-probe.sh
# Toolchain checks are the same as build-system-module.sh (devkitA64 GCC 16.1, libnx with pgl shell events).
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
out_dir=${PROBE_OUT:-$repo_dir/../../scratch/probe/out}
ams=1.12.0
# PROBE_BUILD_DIR=<dir> keeps the tree (libstratosphere takes ~35 min) and only re-applies the probe patch.
if [ -n "${PROBE_BUILD_DIR:-}" ]; then
    build_dir=$PROBE_BUILD_DIR; mkdir -p "$build_dir"
else
    build_dir=$(mktemp -d -t openpak-probe-XXXXXX); trap 'rm -rf -- "$build_dir"' EXIT
fi
if [ -d "$build_dir/source" ]; then
    rm -rf "$build_dir/source/stratosphere/ams_mitm/source/probe_mitm"
    git -C "$build_dir/source" checkout -- stratosphere/ams_mitm/source/amsmitm_module_management.cpp
    git -C "$build_dir/source" apply "$repo_dir/tools/atmosphere-update-probe.patch"
else
    git clone --depth 1 --branch "$ams" https://github.com/Atmosphere-NX/Atmosphere.git "$build_dir/source"
    git -C "$build_dir/source" apply "$repo_dir/tools/atmosphere-ssl.patch" "$repo_dir/tools/atmosphere-store-trust.patch" \
        "$repo_dir/tools/atmosphere-dns-f5cd912.patch" "$repo_dir/tools/atmosphere-dns-openpak.patch" \
        "$repo_dir/tools/atmosphere-update-probe.patch"
fi
pin=(ATMOSPHERE_GIT_BRANCH= ATMOSPHERE_GIT_REVISION=-28d6a2e-dirty ATMOSPHERE_GIT_HASH=28d6a2e11f264007)
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=$DEVKITPRO/devkitARM PATH=$DEVKITPRO/tools/bin:$PATH
"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc" --version | head -n1 | grep -q " 16.1" || { echo "need devkitA64 GCC 16.1" >&2; exit 1; }
grep -q pglEventObserverGetShellEvent "$DEVKITPRO/libnx/include/switch/services/pgl.h" || { echo "need newer libnx" >&2; exit 1; }
make -C "$build_dir/source/stratosphere/ams_mitm" -j4 "${pin[@]}"
mkdir -p "$out_dir"
cp "$build_dir/source/stratosphere/ams_mitm/out/nintendo_nx_arm64_armv8a/release/ams_mitm.kip" "$out_dir/ams_mitm-$ams-content-probe.kip"
sha256sum "$out_dir/ams_mitm-$ams-content-probe.kip"
