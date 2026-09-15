#!/usr/bin/env python3
"""Build an experimental SSL data overlay from the user's own certificate store.

No firmware data is distributed. Replace only expired DST Root CA X3 (1033),
which Stardew 1.6.15.13 requests, with the supplied CA. All other entries remain
byte-for-byte intact. This does not modify a game executable.
"""

import argparse
from pathlib import Path
import struct
import subprocess


def replace_expired_root(store: bytes, certificate: bytes) -> bytes:
    if len(store) < 8:
        raise ValueError("Truncated certificate store")
    magic, count = struct.unpack_from("<II", store)
    if magic != 0x546C7373 or count > (len(store) - 8) // 16:
        raise ValueError("Invalid certificate store header")
    target = None
    seen = set()
    for index in range(count):
        position = 8 + index * 16
        cert_id, status, size, offset = struct.unpack_from("<IIII", store, position)
        if cert_id in seen or offset < count * 16 or offset + size > len(store) - 8:
            raise ValueError("Invalid certificate store entry")
        seen.add(cert_id)
        if cert_id == 1033:
            if status != 1:
                raise ValueError("Expected certificate 1033 to have trusted status")
            original = store[8 + offset:8 + offset + size]
            result = subprocess.run(
                ["openssl", "x509", "-inform", "DER", "-noout", "-subject", "-enddate"],
                input=original, capture_output=True, check=True,
            ).stdout
            if b"DST Root CA X3" not in result or b"2021 GMT" not in result:
                raise ValueError("Certificate 1033 is not the expected expired root")
            target = position
    if target is None:
        raise ValueError("Certificate 1033 is missing")
    if not certificate:
        raise ValueError("Empty replacement certificate")
    output = bytearray(store)
    output.extend(b"\0" * (-len(output) % 4))
    struct.pack_into("<II", output, target + 8, len(certificate), len(output) - 8)
    output.extend(certificate)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cert-store", type=Path, required=True)
    parser.add_argument("--ca", type=Path, required=True, help="OpenPak CA in PEM format")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    certificate = subprocess.run(
        ["openssl", "x509", "-in", str(args.ca), "-outform", "DER"],
        capture_output=True, check=True,
    ).stdout
    details = subprocess.run(
        ["openssl", "x509", "-inform", "DER", "-noout", "-text"],
        input=certificate, capture_output=True, check=True,
    ).stdout
    if b"CA:TRUE" not in details:
        parser.error("Replacement must be a CA certificate")
    output = replace_expired_root(args.cert_store.read_bytes(), certificate)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as stream:
        stream.write(output)
    print(f"Wrote {args.output}: replaced certificate 1033 only")


if __name__ == "__main__":
    main()
