#!/usr/bin/env python3
"""Generate a Sparkle 2.x appcast.xml for one Compact Phone release.

The appcast is uploaded as a release asset alongside the DMG / MSI /
AppImage.
UpdateChecker.cpp fetches the per-OS file from
`https://github.com/marwain91/compact-phone/releases/latest/download/`,
so /latest/ always redirects to the newest tag and clients see the
new version automatically.

Each invocation generates ONE entry. The release workflows produce a
platform-specific file:
appcast-macos.xml / appcast-windows.xml / appcast-linux.xml.

Usage:
    generate-appcast.py --version 0.1.1 \\
                        --asset-url https://.../Compact-Phone-macOS.dmg \\
                        --asset-file dist/Compact-Phone-macOS.dmg \\
                        --os macos \\
                        --output dist/appcast-macos.xml

Optional Ed25519 signing:
  Set SPARKLE_ED25519_PRIVATE_KEY in the environment to the base64-
  encoded raw 32-byte ed25519 private key (the format Sparkle 2's
  `generate_keys` tool emits). The signature is added as
  sparkle:edSignature on the <enclosure>. CompactPhone's current
  in-tree UpdateChecker does not verify signatures, but adding them
  is forward-compatible with switching to the real Sparkle framework
  later and is cheap.

Requires the `cryptography` package on the path when signing is
enabled. Pure-stdlib otherwise.
"""

import argparse
import base64
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from xml.etree.ElementTree import Element, ElementTree, SubElement, register_namespace

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
register_namespace("sparkle", SPARKLE_NS)


def sign_with_ed25519(file_bytes: bytes, key_bytes: bytes) -> str:
    """Return base64-encoded Ed25519 signature of file_bytes."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )
    except ImportError:
        print(
            "error: cryptography package required for signing — "
            "pip install cryptography",
            file=sys.stderr,
        )
        sys.exit(1)
    key = Ed25519PrivateKey.from_private_bytes(key_bytes)
    sig = key.sign(file_bytes)
    return base64.b64encode(sig).decode("ascii")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--version", required=True,
                   help="Semantic version (e.g. 0.1.1), no leading v")
    p.add_argument("--asset-url", required=True,
                   help="Public URL where the release asset is downloadable")
    p.add_argument("--asset-file", required=True,
                   help="Local path to the release asset (for size + signing)")
    p.add_argument("--os", choices=("macos", "windows", "linux"),
                   required=True)
    p.add_argument("--min-system-version", default=None,
                   help="Override the default min OS version (12.0 mac / "
                        "10.0 win / glibc 2.39 linux)")
    p.add_argument("--output", required=True)
    args = p.parse_args()

    asset_path = Path(args.asset_file)
    if not asset_path.exists():
        print(f"error: asset file not found: {asset_path}", file=sys.stderr)
        return 1
    length = asset_path.stat().st_size

    if args.os == "macos":
        mime = "application/x-apple-diskimage"
        default_min = "12.0"
    elif args.os == "windows":
        mime = "application/x-msi"
        default_min = "10.0"
    else:
        mime = "application/vnd.appimage"
        default_min = "glibc 2.39"
    min_system = args.min_system_version or default_min

    # register_namespace() above already arranges for ElementTree to
    # emit xmlns:sparkle when it serialises any {SPARKLE_NS}-prefixed
    # name, so we don't set it explicitly here (doing so would emit it
    # twice).
    rss = Element("rss", {"version": "2.0"})
    channel = SubElement(rss, "channel")
    SubElement(channel, "title").text = "Compact Phone"
    SubElement(channel, "link").text = "https://compactphone.app/"
    SubElement(channel, "description").text = "Compact Phone update feed"
    SubElement(channel, "language").text = "en"

    item = SubElement(channel, "item")
    SubElement(item, "title").text = f"Compact Phone {args.version}"
    SubElement(item, "pubDate").text = datetime.now(timezone.utc).strftime(
        "%a, %d %b %Y %H:%M:%S +0000"
    )

    enc_attrs = {
        "url": args.asset_url,
        "length": str(length),
        "type": mime,
        f"{{{SPARKLE_NS}}}version": args.version,
        f"{{{SPARKLE_NS}}}shortVersionString": args.version,
        f"{{{SPARKLE_NS}}}os": args.os,
    }

    key_b64 = os.environ.get("SPARKLE_ED25519_PRIVATE_KEY", "").strip()
    if key_b64:
        try:
            key_bytes = base64.b64decode(key_b64)
        except Exception as e:
            print(f"error: SPARKLE_ED25519_PRIVATE_KEY is not valid "
                  f"base64: {e}", file=sys.stderr)
            return 1
        with open(asset_path, "rb") as f:
            file_bytes = f.read()
        sig = sign_with_ed25519(file_bytes, key_bytes)
        enc_attrs[f"{{{SPARKLE_NS}}}edSignature"] = sig
        print("signed appcast entry with SPARKLE_ED25519_PRIVATE_KEY")
    else:
        print("note: SPARKLE_ED25519_PRIVATE_KEY not set — appcast "
              "entry is unsigned (still parsed by our checker, but a "
              "real Sparkle client would reject)")

    SubElement(item, "enclosure", enc_attrs)
    SubElement(item, f"{{{SPARKLE_NS}}}minimumSystemVersion").text = min_system

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    ElementTree(rss).write(args.output, xml_declaration=True, encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
