#!/usr/bin/env python3
"""Verify the packaged ARM64 diagnostic core, not merely the source tree."""
import argparse
import hashlib
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", help="APK built using your existing signing setup")
    args = parser.parse_args()
    try:
        with zipfile.ZipFile(args.apk) as apk:
            core = apk.read("lib/arm64-v8a/libmupen64plus-core.so")
        markers = (b"DDDIAG v1", b"DDDIAG_COUNTS", b"DDDIAG_END")
        missing = [marker.decode() for marker in markers if marker not in core]
        if missing:
            parser.exit(1, "FAIL: packaged ARM64 core lacks: " + ", ".join(missing) + "\n")
        digest = hashlib.sha256()
        with open(args.apk, "rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        print("PASS: packaged ARM64 core contains N64DD diagnostic markers")
        print("APK SHA-256:", digest.hexdigest())
        print("This does not verify signing compatibility or native boot behavior.")
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()