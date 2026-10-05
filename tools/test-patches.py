#!/usr/bin/env python3
# Host-side checks for the shipped IPS patch artifacts. No console needed. Run from the repo
# root (make test).
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BCAT_BUILD = "6D9772A733E2370B3F401EFEB7CA8F664309BD66000000000000000000000000"

fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)


def parse_ips(path):
    """Return list of (offset, data) records, asserting a well-formed IPS."""
    b = open(path, "rb").read()
    assert b[:5] == b"PATCH", f"{path}: bad magic"
    assert b[-3:] == b"EOF", f"{path}: no EOF"
    i, recs = 5, []
    while i < len(b) - 3:
        off = int.from_bytes(b[i:i + 3], "big")
        size = int.from_bytes(b[i + 3:i + 5], "big")
        assert size != 0, f"{path}: RLE record unexpected"
        data = b[i + 5:i + 5 + size]
        assert len(data) == size, f"{path}: truncated record"
        recs.append((off, data))
        i += 5 + size
    assert i == len(b) - 3, f"{path}: trailing bytes before EOF"
    return recs


# 1. News list-fetch edge-token patch: two 8-byte records at the documented NSO
#    offsets, replacing the call site with 'strb wzr,[x0]; mov w0,wzr'.
list_ips = os.path.join(
    ROOT, "romfs/patches/exefs_patches/openpak_news_list_no_dauth", BCAT_BUILD + ".ips")
check(os.path.exists(list_ips), "news_list_no_dauth IPS missing")
if os.path.exists(list_ips):
    recs = parse_ips(list_ips)
    NEW = bytes.fromhex("1f00003 9e0031f2a".replace(" ", ""))
    want = {0x114098: NEW, 0x117380: NEW}  # titles/topics, list fetch
    got = {off: data for off, data in recs}
    check(got == want, f"news_list_no_dauth records {got!r} != {want!r}")

# 2. News memory-download patch still there and well-formed (gating unchanged).
dauth_ips = os.path.join(
    ROOT, "romfs/patches/exefs_patches/openpak_news_no_dauth", BCAT_BUILD + ".ips")
check(os.path.exists(dauth_ips), "news_no_dauth IPS missing")
if os.path.exists(dauth_ips):
    recs = parse_ips(dauth_ips)
    check(len(recs) == 1 and recs[0][0] == 0xfc8a4 and len(recs[0][1]) == 8,
          f"news_no_dauth record unexpected: {recs!r}")

# 3. The store-trust FS change is compiled into OpenPak's fusee (romfs/system/fusee-1.12.0.bin,
#    docs/install-trust.md). Atmosphere's fusee (1.11.2, 1.12.0) reads no /atmosphere/kip_patches, so no
#    kip_patches payload may ship: it would look installed and do nothing.
check(not os.path.exists(os.path.join(ROOT, "romfs/patches/kip_patches")),
      "romfs/patches/kip_patches must not ship (fusee never reads it)")

if fails:
    print("patch/payload checks FAILED:")
    for f in fails:
        print("  -", f)
    sys.exit(1)
print("patch artifacts: all checks passed")
