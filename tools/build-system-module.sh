#!/usr/bin/env bash
set -euo pipefail
# Rebuild the open-source system component bundled in the NRO.
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d -t openpak-atmosphere-XXXXXX)
trap 'rm -rf -- "$build_dir"' EXIT
git clone --depth 1 --branch 1.11.2 https://github.com/Atmosphere-NX/Atmosphere.git "$build_dir/source"
git -C "$build_dir/source" apply "$repo_dir/tools/atmosphere-ssl.patch"
podman run --rm -v "$build_dir/source:/src" -w /src/stratosphere/ams_mitm docker.io/devkitpro/devkita64 make -j4
cp "$build_dir/source/stratosphere/ams_mitm/out/nintendo_nx_arm64_armv8a/release/ams_mitm.kip" "$repo_dir/romfs/system/ams_mitm-1.11.2.kip"
