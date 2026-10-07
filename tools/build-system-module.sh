#!/usr/bin/env bash
set -euo pipefail
# Rebuild the open-source Atmosphere components bundled in the NRO: ams_mitm (the SSL overlay),
# Loader and fusee (store trust). All three build natively under $DEVKITPRO with devkitARM r68 and
# devkitA64 r30 (GCC 16.1); the devkitpro/devkita64 image carries GCC 15.2, whose fusee has
# different MTC overlays from the official package3 (checked below). Atmosphere 1.12.0 also needs
# a libnx newer than the 4.12.0 release (pgl's shell event API, libnx a13cd19): build libnx master
# as of the Atmosphere release (146c3d1) and install it into a copy of $DEVKITPRO whose other
# entries link to the system one. python3-lz4 must be importable for fusee. Builds go under $TMPDIR: point it at the workspace,
# not the shared /tmp, and run this under `flock /tmp/openpak-build.lock`.
repo_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d -t openpak-atmosphere-XXXXXX)
ams=1.12.0   # the Atmosphere release these are built from; system.c pins its package3
trap 'rm -rf -- "$build_dir"' EXIT
git clone --depth 1 --branch "$ams" https://github.com/Atmosphere-NX/Atmosphere.git "$build_dir/source"
# atmosphere-dns-f5cd912.patch is upstream's own fix, Atmosphere-NX/Atmosphere f5cd91260 (hexkyz,
# 2026-10-06, "dns.mitm: implement mitm for the new c-ares resolver"): firmware 23 resolves through new
# sfdnsres commands (100/102/104) that 1.12.0's dns.mitm does not hook. Drop it once a release has it.
# atmosphere-dns-openpak.patch, on top of it: command 100 forwards the redirect target as a numeric literal and
# keeps the real query id; 102/104 are no longer overridden (upstream's version left the real queries uncollected).
git -C "$build_dir/source" apply "$repo_dir/tools/atmosphere-ssl.patch" "$repo_dir/tools/atmosphere-store-trust.patch" \
    "$repo_dir/tools/atmosphere-dns-f5cd912.patch" "$repo_dir/tools/atmosphere-dns-openpak.patch"
# The git stamp is pinned to the release commit so two builds of these sources match: fusee byte
# for byte; Loader only differs in its 20-byte GNU build ID, which hashes the absolute build path
# held in the debug info.
pin=(ATMOSPHERE_GIT_BRANCH= ATMOSPHERE_GIT_REVISION=-28d6a2e-dirty ATMOSPHERE_GIT_HASH=28d6a2e11f264007)
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=$DEVKITPRO/devkitARM PATH=$DEVKITPRO/tools/bin:$PATH
for tool in devkitARM/bin/arm-none-eabi-gcc:16.1 devkitA64/bin/aarch64-none-elf-gcc:16.1; do
    "$DEVKITPRO/${tool%:*}" --version | head -n1 | grep -q " ${tool#*:}" || { echo "need GCC ${tool#*:}: $DEVKITPRO/${tool%:*}" >&2; exit 1; }
done
python3 -c 'import lz4.block'
grep -q pglEventObserverGetShellEvent "$DEVKITPRO/libnx/include/switch/services/pgl.h" ||
    { echo "need libnx with pglEventObserverGetShellEvent in $DEVKITPRO/libnx (see above)" >&2; exit 1; }
make -C "$build_dir/source/stratosphere/ams_mitm" -j4 "${pin[@]}"
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
    assert hashlib.sha256(stored).hexdigest() == want, f"MTC overlay {i} differs from the official package3's"
EOF
cp "$build_dir/source/stratosphere/ams_mitm/out/nintendo_nx_arm64_armv8a/release/ams_mitm.kip" "$repo_dir/romfs/system/ams_mitm-$ams.kip"
cp "$build_dir/source/stratosphere/loader/out/nintendo_nx_arm64_armv8a/release/loader.kip" "$repo_dir/romfs/system/loader-$ams.kip"
cp "$build_dir/source/fusee/out/nintendo_nx_arm_armv4t/release/fusee.bin" "$repo_dir/romfs/system/fusee-$ams.bin"
sha256sum "$repo_dir"/romfs/system/{ams_mitm,loader}-$ams.kip "$repo_dir/romfs/system/fusee-$ams.bin"
echo "tools/test-system.py pins the package these build (STORE_SHA): new bytes need a new hardware test."
