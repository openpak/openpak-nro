#!/usr/bin/env bash
set -euo pipefail
# Rebuild the open-source Atmosphere components bundled in the NRO: ams_mitm (the SSL overlay),
# Loader and fusee (store trust). ams_mitm builds in the devkitpro/devkita64 image as it always
# has. Loader and fusee need the devkitPro the shipped files came from, devkitARM r68 and
# devkitA64 r30 (GCC 16.1), installed natively under $DEVKITPRO: the image still carries GCC
# 15.2, whose fusee has different MTC overlays from the official package3 (checked below), and
# python3-lz4 must be importable for fusee. Builds go under $TMPDIR: point it at the workspace,
# not the shared /tmp, and run this under `flock /tmp/openpak-build.lock`.
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d -t openpak-atmosphere-XXXXXX)
trap 'rm -rf -- "$build_dir"' EXIT
git clone --depth 1 --branch 1.11.2 https://github.com/Atmosphere-NX/Atmosphere.git "$build_dir/source"
git -C "$build_dir/source" apply "$repo_dir/tools/atmosphere-ssl.patch" "$repo_dir/tools/atmosphere-store-trust.patch"
# Loader and fusee shipped from a tree whose git stamp read -06ed0f6-dirty: these same sources, in
# OpenPak's Atmosphere clone. With the stamp pinned fusee comes out byte for byte; Loader differs
# only in its 20-byte GNU build ID, which hashes the absolute build path held in the debug info.
pin=(ATMOSPHERE_GIT_BRANCH= ATMOSPHERE_GIT_REVISION=-06ed0f6-dirty ATMOSPHERE_GIT_HASH=06ed0f6ae4ac213e)
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=$DEVKITPRO/devkitARM PATH=$DEVKITPRO/tools/bin:$PATH
for tool in devkitARM/bin/arm-none-eabi-gcc:16.1 devkitA64/bin/aarch64-none-elf-gcc:16.1; do
    "$DEVKITPRO/${tool%:*}" --version | head -n1 | grep -q " ${tool#*:}" || { echo "need GCC ${tool#*:}: $DEVKITPRO/${tool%:*}" >&2; exit 1; }
done
python3 -c 'import lz4.block'
# Its own copy of the tree: the container's library objects record /src paths the native build
# cannot use, and the other way round.
cp -a "$build_dir/source" "$build_dir/mitm"
podman run --rm -v "$build_dir/mitm:/src" -w /src/stratosphere/ams_mitm docker.io/devkitpro/devkita64 make -j4
make -C "$build_dir/source/stratosphere/loader" -j4 "${pin[@]}"
make -C "$build_dir/source/fusee" -j4 "${pin[@]}"
# package3 keeps fusee's two MTC overlays outside fusee.bin. They must still be the official ones,
# or a fusee built from these sources would run against tables it was not built with.
python3 - "$build_dir/source/fusee/program/out/nintendo_nx_arm_armv4t/release/program.bin" <<'EOF'
import hashlib, sys
program = open(sys.argv[1], "rb").read()
official = ["2aa383a692f6cc307ba509ae70eb40f2d19bdc6f19f1a29f9fb3c63bf2b9efca",
            "e0b1784a0ac881d22de8a085baad421bed98f916f35893e920bf3bedc2927fc5"]
for i, want in enumerate(official, 1):
    ovl = program[0x2B000 + 0x14000 * i:0x2B000 + 0x14000 * (i + 1)]
    stored = ovl[:-4] + hashlib.sha256(ovl[:-4]).digest()[:4]   # as package3 holds it
    assert hashlib.sha256(stored).hexdigest() == want, f"MTC overlay {i} differs from Atmosphere 1.11.2's"
EOF
cp "$build_dir/mitm/stratosphere/ams_mitm/out/nintendo_nx_arm64_armv8a/release/ams_mitm.kip" "$repo_dir/romfs/system/ams_mitm-1.11.2.kip"
cp "$build_dir/source/stratosphere/loader/out/nintendo_nx_arm64_armv8a/release/loader.kip" "$repo_dir/romfs/system/loader-1.11.2.kip"
cp "$build_dir/source/fusee/out/nintendo_nx_arm_armv4t/release/fusee.bin" "$repo_dir/romfs/system/fusee-1.11.2.bin"
sha256sum "$repo_dir"/romfs/system/{ams_mitm,loader}-1.11.2.kip "$repo_dir/romfs/system/fusee-1.11.2.bin"
echo "tools/test-system.py pins the package these build (STORE_SHA): new bytes need a new hardware test."
