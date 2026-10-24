#!/usr/bin/env python3
"""Build the OpenPak boot package SD zip: step 1 of a first install.

A stock Atmosphère console cannot launch the Switch Patcher NSP: its program id is in the
0x01FE… store range with a self-signed ACID and NCA header, which only OpenPak's boot package
(Loader + FS trust, docs/install-trust.md) accepts. This zip puts that package on the card
ahead of the app, plus a hekate entry to boot it:

  atmosphere/package3-openpak          the same bytes the app's own OpenPak selection writes
  bootloader/ini/openpak.ini           hekate "More configs": OpenPak (emuMMC) / (sysMMC)
  switch/openpak/boot-package/         SOURCE.txt and Atmosphère's licence

The package is built by source/system.c's own builder (openpak_package_build) from the official
Atmosphère 1.12.1 package3, whose SHA-256 that builder pins, so the app later recognises the file
as its own and hekate_ipl.ini is never touched here.

usage: build-boot-zip.py --out <zip> [--package3 <official package3>]
Without --package3 the official Atmosphère 1.12.1 release zip is downloaded (gh, or HTTPS).
"""
import argparse
import ctypes as c
import hashlib
import io
import subprocess
import tempfile
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ATMOSPHERE_ZIP = ("https://github.com/Atmosphere-NX/Atmosphere/releases/download/1.12.1/"
                  "atmosphere-1.12.1-master-6b0c077b4+hbl-2.4.5+hbmenu-3.6.1.zip")
# hekate reads every bootloader/ini/*.ini as extra boot entries. No emuMMC choice is made for
# the user: one entry per system, the way hekate's own examples are written.
HEKATE_INI = """\
; OpenPak boot package, from the OpenPak Switch Patcher boot zip. Boot one of these once to
; start OpenPak Switch Patcher; selecting OpenPak in it points your own hekate entries at the
; same package. Safe to delete afterwards.
[OpenPak (emuMMC)]
pkg3=atmosphere/package3-openpak
emummcforce=1

[OpenPak (sysMMC)]
pkg3=atmosphere/package3-openpak
emummc_force_disable=1
"""


def official_package3():
    data = urllib.request.urlopen(ATMOSPHERE_ZIP, timeout=120).read()
    return zipfile.ZipFile(io.BytesIO(data)).read("atmosphere/package3")


def build_package(official):
    with tempfile.TemporaryDirectory(prefix="openpak-boot-") as d:
        stub = Path(d) / "root.c"
        stub.write_text('const char *openpak_root = "";\n')
        lib = Path(d) / "system.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-DOPENPAK_HOST_TEST", str(REPO / "source/system.c"),
                        str(REPO / "source/ksp.c"), str(stub), "-lcrypto", "-o", str(lib)], check=True)
        api = c.CDLL(str(lib))
        api.openpak_package_build.restype = c.c_bool
        api.openpak_package_build.argtypes = [c.c_void_p, c.c_size_t] * 4
        system = REPO / "romfs/system"
        kip = (system / "ams_mitm-1.12.1.kip").read_bytes()
        loader = (system / "loader-1.12.1.kip").read_bytes()
        fusee = (system / "fusee-1.12.1.bin").read_bytes()
        buf = c.create_string_buffer(official, len(official))
        if not api.openpak_package_build(buf, len(official), kip, len(kip), loader, len(loader), fusee, len(fusee)):
            raise SystemExit("not the official Atmosphère 1.12.1 package3 (or the bundled modules do not fit it)")
        return buf.raw[:len(official)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--package3", type=Path)
    a = ap.parse_args()
    official = a.package3.read_bytes() if a.package3 else official_package3()
    package = build_package(official)
    files = [("atmosphere/package3-openpak", package),
             ("bootloader/ini/openpak.ini", HEKATE_INI.encode()),
             ("switch/openpak/boot-package/SOURCE.txt", (REPO / "romfs/system/SOURCE.txt").read_bytes()),
             ("switch/openpak/boot-package/LICENSE.Atmosphere", (REPO / "romfs/system/LICENSE.Atmosphere").read_bytes())]
    a.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files:  # fixed timestamps: the same inputs give the same zip
            z.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data, zipfile.ZIP_DEFLATED)
    print(f"{a.out}: package3-openpak sha256 {hashlib.sha256(package).hexdigest()}")


if __name__ == "__main__":
    main()
