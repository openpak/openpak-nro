#!/usr/bin/env python3
"""Regenerate romfs/patches from the public Switch patch database.

The community exefs_patches repositories lag behind firmware releases — none of them carried
22.5.0, the version this console runs — while borntohonk/Switch-Ghidra-Guides publishes the
build ids and patch bytes for each one. This turns those rows into the .ips files Atmosphere
loads, so a new firmware is one re-run away rather than a wait for someone else to publish.

    python3 tools/fetch-patches.py

Filenames are the build id: Atmosphere matches an NRO patch on the first 32 hex characters and
an exefs patch on the full 40 (a 64-character zero-padded name is also accepted, and shipped
too, because different loader versions look for different lengths).
"""
import ast
import pathlib
import subprocess
import sys

DB = "repos/borntohonk/Switch-Ghidra-Guides/contents/patch_database/ssl_ips_patches.txt"
ROOT = pathlib.Path(__file__).resolve().parent.parent / "romfs" / "patches"


def rows():
    raw = subprocess.run(["gh", "api", DB, "--jq", ".content"], capture_output=True, text=True, check=True).stdout
    text = __import__("base64").b64decode(raw).decode()
    for line in text.splitlines():
        line = line.strip().rstrip(",")
        if line.startswith("(") and line.endswith(")"):
            yield ast.literal_eval(line)


def main() -> int:
    written = 0
    for firmware, build_id, dest, payload in rows():
        # "patches/atmosphere/exefs_patches/..." -> "exefs_patches/..."
        rel = dest.split("atmosphere/", 1)[1].rstrip("/")
        out_dir = ROOT / rel
        out_dir.mkdir(parents=True, exist_ok=True)
        blob = bytes.fromhex(payload)

        names = [build_id[:32]] if "nro_patches" in rel else [build_id, build_id.ljust(64, "0")]
        for name in names:
            (out_dir / f"{name}.ips").write_bytes(blob)
            written += 1
        print(f"{firmware:>8}  {rel:<46} {names[0]}.ips")
    print(f"\n{written} patch files under {ROOT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
