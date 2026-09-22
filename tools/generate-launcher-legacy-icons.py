#!/usr/bin/env python3
"""Generate the legacy (pre-API-26) launcher icons as round tiles.

Why this exists
---------------
On API 26+ a launcher draws the adaptive icon (mipmap-anydpi-v26 plus the 108 dp foreground
produced by tools/generate-launcher-icon.py) and applies its own circular mask. Everything else -
older launchers, the manifest's android:roundIcon consumer, and tools that read a legacy icon
directly such as file managers or TV surfaces - reads mipmap-*/ic_launcher.png and
mipmap-*/ic_launcher_round.png.

Those files were bare glyphs with a transparent background, so such a caller showed a logo floating
with no tile. This script renders what the launcher shows through its mask - the central 72 dp of the
108 dp canvas - and clips it to a circle, so every code path shows one round icon whose corners are
transparent.

The gradient constants below must stay in sync with drawable/ic_launcher_background.xml.

Run from anywhere:  python3 tools/generate-launcher-legacy-icons.py
Exit status is non-zero if any written icon is not a circle or has opaque corners.
"""

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

REPO_ROOT = Path(__file__).resolve().parent.parent
FOREGROUND = REPO_ROOT / "app/src/main/res/mipmap-xxxhdpi/ic_launcher_foreground.png"

# 108 dp adaptive canvas and the 72 dp region a launcher actually shows, at the foreground's scale.
REF_PX = 432
VISIBLE_PX = round(REF_PX * 72 / 108)

# Legacy icons are 48 dp; densities scale that.
LEGACY_DP = 48
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}

# Must match drawable/ic_launcher_background.xml.
BASE_START = "#0E0C1C"
BASE_END = "#06050F"
GLOWS = (
    (0.32, 0.30, 0.50, "#7F00FF", 0x33),
    (0.72, 0.74, 0.46, "#00BFFF", 0x29),
    (0.62, 0.24, 0.34, "#FF00BF", 0x1F),
)


def rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def build_background():
    """The background layer: one diagonal base plus the off-centre linear-ramp glows."""
    start, end = rgb(BASE_START), rgb(BASE_END)
    layer = Image.new("RGBA", (REF_PX, REF_PX))
    pixels = layer.load()
    for y in range(REF_PX):
        for x in range(REF_PX):
            t = (x + y) / (2 * (REF_PX - 1))
            pixels[x, y] = tuple(int(start[i] + (end[i] - start[i]) * t) for i in range(3)) + (255,)

    for cx, cy, radius_fraction, colour, peak in GLOWS:
        colour = rgb(colour)
        radius = REF_PX * radius_fraction
        centre_x, centre_y = REF_PX * cx, REF_PX * cy
        glow = Image.new("RGBA", (REF_PX, REF_PX), (0, 0, 0, 0))
        glow_pixels = glow.load()
        for y in range(REF_PX):
            for x in range(REF_PX):
                distance = math.hypot(x - centre_x, y - centre_y)
                if distance < radius:
                    alpha = max(0, min(255, round(peak * (1 - distance / radius))))
                    if alpha:
                        glow_pixels[x, y] = colour + (alpha,)
        layer = Image.alpha_composite(layer, glow)
    return layer


def main():
    if not FOREGROUND.is_file():
        print(f"ERROR: foreground not found: {FOREGROUND}", file=sys.stderr)
        return 2

    # Composite the glyph over the background, then take the region a launcher would show.
    visible = Image.alpha_composite(build_background(), Image.open(FOREGROUND).convert("RGBA"))
    offset = (REF_PX - VISIBLE_PX) // 2
    visible = visible.crop([offset, offset, offset + VISIBLE_PX, offset + VISIBLE_PX])

    failures = []
    for density, scale in DENSITIES.items():
        size = round(LEGACY_DP * scale)
        icon = visible.resize((size, size), Image.LANCZOS)
        mask = Image.new("L", (size, size), 0)
        ImageDraw.Draw(mask).ellipse([0, 0, size - 1, size - 1], fill=255)
        icon.putalpha(mask)

        for name in ("ic_launcher.png", "ic_launcher_round.png"):
            out_path = REPO_ROOT / f"app/src/main/res/mipmap-{density}/{name}"
            out_path.parent.mkdir(parents=True, exist_ok=True)
            icon.save(out_path, optimize=True)

            # Verify from the written bytes, not from the in-memory image.
            written = Image.open(out_path).convert("RGBA")
            alpha = written.getchannel("A")
            corners = [alpha.getpixel(p) for p in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1))]
            coverage = sum(alpha.get_flattened_data()) / (255.0 * size * size)
            ok = max(corners) == 0 and 0.74 < coverage < 0.83
            if not ok:
                failures.append((density, name, corners, coverage))
            print(f"{density:8s} {name:19s} {size:3d}px  corner alpha {max(corners):3d}  "
                  f"coverage {coverage:.3f} (circle=0.785)  {'ok' if ok else 'FAIL'}")

    print()
    if failures:
        print("FAILED: not round, or corners not transparent: "
              + ", ".join(f"{d}/{n}" for d, n, _, _ in failures), file=sys.stderr)
        return 1
    print("OK: every legacy icon is a round tile with transparent corners.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
