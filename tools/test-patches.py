#!/usr/bin/env python3
# Host-side checks for the shipped IPS patch artifacts and the Store-installs
# payload state. No console needed. Run from the repo root (make test).
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BCAT_BUILD = "6D9772A733E2370B3F401EFEB7CA8F664309BD66000000000000000000000000"
FS_HASH = "536d938469fe73be3c76da0333b289c0ed29f10c2a8afdff8e466142c4277359"

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

# 3. Store-installs FS payload is PENDING: no .ips shipped, so the step installs
#    nothing until a hardware-verified patch is dropped in.
payload = os.path.join(ROOT, "romfs/patches/kip_patches/openpak_fs_no_ncasig")
ips_in_payload = [f for f in os.listdir(payload) if f.endswith(".ips")] if os.path.isdir(payload) else []
check(ips_in_payload == [], f"FS payload must be empty of .ips (pending), found {ips_in_payload}")

# 4. The FS candidate lives OUTSIDE the payload, is gated on the FS hash, and is
#    deliberately not a .ips so no copy logic can ship it.
cand_dir = os.path.join(ROOT, "docs/install-trust")
cand = os.path.join(cand_dir, FS_HASH + ".ips.pending")
check(os.path.exists(cand), "FS candidate marker missing")
check(not any(f.endswith(".ips") for f in os.listdir(cand_dir)) if os.path.isdir(cand_dir) else True,
      "no raw .ips may sit in docs/install-trust (candidate only)")

if fails:
    print("patch/payload checks FAILED:")
    for f in fails:
        print("  -", f)
    sys.exit(1)
print("patch artifacts and Store-installs payload: all checks passed")
