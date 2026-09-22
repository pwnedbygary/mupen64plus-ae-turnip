#!/usr/bin/env python3
"""Generate the adaptive-icon foreground layer from the high-resolution source logo.

Why this exists
---------------
An adaptive icon's foreground layer must be authored on a 108x108 dp canvas, but a launcher
only ever shows the central 72x72 dp region inside its mask (a circular mask on the Retroid
Pocket 6 launcher). Artwork that fills the whole canvas therefore has its corners cut off by
the mask, and art authored at the legacy 48 dp size is additionally upscaled 2.25x and looks
soft. This script fixes both: it scales the source glyph so that its *entire* bounding box --
including the diagonal -- fits inside the 72 dp mask circle, then centres it on a real 108 dp
canvas at every density.

Geometry
--------
The binding constraint is the diagonal, not the width. For a content box of width W and
height H = W / aspect, the rotated extent is W * hypot(1, 1 / aspect). Setting that equal to
the usable diameter (the mask circle minus the safety margin) gives the width to use.

Run from anywhere:  python3 tools/generate-launcher-icon.py
Exit status is non-zero if any written layer would still be clipped by the mask.
"""

import math
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE = REPO_ROOT / "app/src/main/res/drawable-nodpi/hireslogo.png"

# Adaptive icon geometry, in dp. The mask circle diameter is what must contain all artwork.
CANVAS_DP = 108.0
MASK_VISIBLE_DP = 72.0
# Keep this fraction of the circle diameter clear so the glyph is visibly inside the mask
# rather than tangent to it.
SAFETY_MARGIN = 0.08

DENSITIES = {
    "mdpi": 1.0,
    "hdpi": 1.5,
    "xhdpi": 2.0,
    "xxhdpi": 3.0,
    "xxxhdpi": 4.0,
}


def main() -> int:
    if not SOURCE.is_file():
        print(f"ERROR: source logo not found: {SOURCE}", file=sys.stderr)
        return 2

    source = Image.open(SOURCE).convert("RGBA")
    bbox = source.getchannel("A").getbbox()
    if bbox is None:
        print(f"ERROR: source logo is fully transparent: {SOURCE}", file=sys.stderr)
        return 2
    content = source.crop(bbox)
    aspect = content.width / content.height

    usable_dp = MASK_VISIBLE_DP * (1.0 - SAFETY_MARGIN)
    width_dp = usable_dp / math.hypot(1.0, 1.0 / aspect)
    height_dp = width_dp / aspect

    print(f"source           : {SOURCE.relative_to(REPO_ROOT)} {source.width}x{source.height}")
    print(f"content bbox     : {bbox} -> {content.width}x{content.height} (aspect {aspect:.4f})")
    print(f"mask circle      : {MASK_VISIBLE_DP:.0f} dp visible of {CANVAS_DP:.0f} dp canvas")
    print(f"usable diameter  : {usable_dp:.2f} dp (margin {SAFETY_MARGIN:.0%})")
    print(f"glyph size       : {width_dp:.2f} x {height_dp:.2f} dp")
    print()

    failures = []
    for density, scale in DENSITIES.items():
        canvas_px = round(CANVAS_DP * scale)
        target = (round(width_dp * scale), round(height_dp * scale))
        glyph = content.resize(target, Image.LANCZOS)

        layer = Image.new("RGBA", (canvas_px, canvas_px), (0, 0, 0, 0))
        layer.alpha_composite(glyph, ((canvas_px - target[0]) // 2, (canvas_px - target[1]) // 2))

        out_path = REPO_ROOT / f"app/src/main/res/mipmap-{density}/ic_launcher_foreground.png"
        out_path.parent.mkdir(parents=True, exist_ok=True)
        layer.save(out_path, optimize=True)

        # Verify from the written bytes, not from the in-memory layer.
        written = Image.open(out_path).convert("RGBA")
        w_bbox = written.getchannel("A").getbbox()
        w_px, h_px = w_bbox[2] - w_bbox[0], w_bbox[3] - w_bbox[1]
        diagonal_dp = math.hypot(w_px, h_px) / scale
        clipped = diagonal_dp > MASK_VISIBLE_DP
        if clipped:
            failures.append((density, diagonal_dp))
        print(
            f"{density:8s} canvas {canvas_px:3d}px  glyph {w_px:3d}x{h_px:3d}px  "
            f"diagonal {diagonal_dp:6.2f} dp  {'CLIPPED' if clipped else 'inside circle'}"
        )

    print()
    if failures:
        print("FAILED: glyph exceeds the mask circle for: "
              + ", ".join(f"{d} ({v:.2f} dp)" for d, v in failures), file=sys.stderr)
        return 1

    print(f"OK: every layer keeps its whole glyph inside the {MASK_VISIBLE_DP:.0f} dp mask circle.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
