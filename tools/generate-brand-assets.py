#!/usr/bin/env python3
"""Regenerate every remaining brand raster from the neon "M" master logo.

Why this exists
---------------
The app still shipped the retired yellow/green cube in seven places: icon.png (the notification
small icon and the Android TV channel logo), banner.png (the Android TV banner) and the two web
listing icons. This script rebuilds all of them from drawable-nodpi/hireslogo.png using the same
retrowave gradient family as the launcher icon and the splash backdrop.

The gradient constants must stay in sync with drawable/ic_launcher_background.xml.

Run from anywhere:  python3 tools/generate-brand-assets.py
Exit status is non-zero if a written asset still carries the retired cube palette, or if a round
asset is not round.
"""

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
LOGO = REPO_ROOT / "app/src/main/res/drawable-nodpi/hireslogo.png"
FOREGROUND = REPO_ROOT / "app/src/main/res/mipmap-xxxhdpi/ic_launcher_foreground.png"
BOLD_FONT = Path("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf")

# 108 dp adaptive canvas and the 72 dp region a launcher actually shows, at the logo master's scale.
REF_PX = 432
VISIBLE_PX = round(REF_PX * 72 / 108)

# Must match drawable/ic_launcher_background.xml.
BASE_START, BASE_END = "#0E0C1C", "#06050F"
GLOWS = (
    (0.32, 0.30, 0.50, "#7F00FF", 0x33),
    (0.72, 0.74, 0.46, "#00BFFF", 0x29),
    (0.62, 0.24, 0.34, "#FF00BF", 0x1F),
)
WORDMARK = ("M64PLUS", "FZ")


def rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def gradient(width, height):
    """The background layer: one diagonal base plus the off-centre linear-ramp glows."""
    start, end = rgb(BASE_START), rgb(BASE_END)
    layer = Image.new("RGBA", (width, height))
    pixels = layer.load()
    for y in range(height):
        for x in range(width):
            t = (x / max(1, width - 1) + y / max(1, height - 1)) / 2
            pixels[x, y] = tuple(int(start[i] + (end[i] - start[i]) * t) for i in range(3)) + (255,)

    span = min(width, height)
    for cx, cy, radius_fraction, colour, peak in GLOWS:
        colour = rgb(colour)
        radius = span * radius_fraction
        centre_x, centre_y = width * cx, height * cy
        glow = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        glow_pixels = glow.load()
        for y in range(height):
            for x in range(width):
                distance = math.hypot(x - centre_x, y - centre_y)
                if distance < radius:
                    alpha = max(0, min(255, round(peak * (1 - distance / radius))))
                    if alpha:
                        glow_pixels[x, y] = colour + (alpha,)
        layer = Image.alpha_composite(layer, glow)
    return layer


def glyph():
    """The logo master cropped to its visible bounds."""
    logo = Image.open(LOGO).convert("RGBA")
    box = logo.getchannel("A").getbbox()
    return logo.crop(box)


def glyph_sized(size, fill):
    """The glyph scaled so its longest side is `fill` of `size`, centred, transparent behind."""
    art = glyph()
    scale = (size * fill) / max(art.size)
    art = art.resize((max(1, round(art.width * scale)), max(1, round(art.height * scale))), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(art, ((size - art.width) // 2, (size - art.height) // 2))
    return canvas


def visible_tile():
    """What a masking launcher shows: the central 72 dp of the 108 dp canvas, gradient plus glyph.

    The glyph layer is the generated 108 dp adaptive foreground, which already places the logo at
    the launcher's scale. Pasting the raw master here would clip it: Pillow's in-place
    alpha_composite() silently crops a larger source instead of raising, which leaves only a centre
    fragment of the logo inside the 72 dp window.
    """
    canvas = gradient(REF_PX, REF_PX)
    foreground = Image.open(FOREGROUND).convert("RGBA")
    if foreground.size != canvas.size:
        raise SystemExit(f"ERROR: {FOREGROUND} is {foreground.size}, expected {canvas.size}")
    canvas.alpha_composite(foreground)
    offset = (REF_PX - VISIBLE_PX) // 2
    return canvas.crop([offset, offset, offset + VISIBLE_PX, offset + VISIBLE_PX])


def round_tile(size):
    """A round launcher-style tile: circle of gradient plus glyph, transparent corners."""
    tile = visible_tile().resize((size, size), Image.LANCZOS)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, size - 1, size - 1], fill=255)
    tile.putalpha(mask)
    return tile


def fit_font(text, max_width, max_size):
    for size in range(max_size, 7, -1):
        if BOLD_FONT.is_file() and ImageFont.truetype(str(BOLD_FONT), size).getlength(text) <= max_width:
            return ImageFont.truetype(str(BOLD_FONT), size)
    return None


def cube_share(path):
    """Fraction of opaque pixels in the retired cube's yellow-green family."""
    image = Image.open(path).convert("RGBA")
    if image.width * image.height > 4_000_000:
        image = image.resize((512, 512))
    total = yellow_green = 0
    for r, g, b, a in image.get_flattened_data():
        if a < 200:
            continue
        total += 1
        h, s, v = __import__("colorsys").rgb_to_hsv(r / 255, g / 255, b / 255)
        if 55 <= h * 360 <= 105 and s > 0.35 and v > 0.35:
            yellow_green += 1
    return (yellow_green / total) if total else 0.0


def main():
    if not LOGO.is_file():
        print(f"ERROR: logo master not found: {LOGO}", file=sys.stderr)
        return 2

    written = []

    # Notification small icon + Android TV channel logo: the glyph alone, transparent behind.
    for density, size in (("ldpi", 32), ("mdpi", 48), ("hdpi", 72), ("xhdpi", 96)):
        out = REPO_ROOT / f"app/src/main/res/drawable-{density}/icon.png"
        glyph_sized(size, 0.92).save(out, optimize=True)
        written.append(("icon.png", density, out))

    # Android TV banner: the gradient plus the glyph and the wordmark, matching the old layout.
    width, height = 320, 180
    banner = gradient(width, height)
    art = glyph_sized(height, 0.66)
    banner.alpha_composite(art, (26, (height - art.height) // 2))
    text_left = 26 + art.width + 18
    available = width - text_left - 18
    font = fit_font(WORDMARK[0], available, 34)
    if font is not None:
        draw = ImageDraw.Draw(banner)
        heights = [font.getbbox(word)[3] for word in WORDMARK]
        gap = 2
        y = (height - (sum(heights) + gap)) // 2
        for word, line_height in zip(WORDMARK, heights):
            draw.text((text_left, y), word, font=font, fill=rgb("F5EAF8"))
            y += line_height + gap
    else:
        print(f"ERROR: bold font unavailable at {BOLD_FONT}; banner would be written without its wordmark", file=sys.stderr)
        return 2
    banner_path = REPO_ROOT / "app/src/main/res/drawable-xhdpi/banner.png"
    banner.convert("RGB").save(banner_path, optimize=True)
    written.append(("banner.png", "xhdpi", banner_path))

    # Web listing icons: the round launcher tile; the pro variant keeps its badge.
    web = round_tile(512)
    web_path = REPO_ROOT / "app/src/main/ic_launcher-web.png"
    web.save(web_path, optimize=True)
    written.append(("ic_launcher-web.png", "-", web_path))

    pro = round_tile(512)
    if BOLD_FONT.is_file():
        draw = ImageDraw.Draw(pro)
        font = fit_font("PRO", 90, 34)
        if font is not None:
            left, top, right, bottom = font.getbbox("PRO")
            pad = 10
            box_w, box_h = right - left + 2 * pad, bottom - top + 2 * pad
            box_x, box_y = (512 - box_w) // 2, 352
            badge = Image.new("RGBA", (box_w, box_h), (0, 0, 0, 0))
            ImageDraw.Draw(badge).rounded_rectangle([0, 0, box_w - 1, box_h - 1], radius=8, fill=(12, 8, 22, 205))
            pro.alpha_composite(badge, (box_x, box_y))
            draw.text((box_x + pad - left, box_y + pad - top), "PRO", font=font, fill=rgb("F5EAF8"))
    else:
        print(f"ERROR: bold font unavailable at {BOLD_FONT}; pro web icon would be written without its badge", file=sys.stderr)
        return 2
    pro_path = REPO_ROOT / "app/src/main/ic_launcher_pro-web.png"
    pro.save(pro_path, optimize=True)
    written.append(("ic_launcher_pro-web.png", "-", pro_path))

    print()
    failures = []
    for name, density, path in written:
        image = Image.open(path).convert("RGBA")
        cube = cube_share(path)
        if name in ("ic_launcher-web.png", "ic_launcher_pro-web.png"):
            alpha = image.getchannel("A")
            size = image.width
            corners = max(alpha.getpixel(p) for p in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1)))
            coverage = sum(alpha.get_flattened_data()) / (255.0 * size * size)
            # The glyph is 47.5 dp of the 72 dp visible window, so it must span about two thirds of
            # the tile. A centre crop of the raw master would still pass the coverage check but blow
            # this one up to ~1.0, which is the defect this guards against.
            bright = [i for i, (r, g, b) in enumerate(image.convert("RGB").get_flattened_data())
                      if (r + g + b) / 3 > 90]
            columns = [i % size for i in bright]
            span = (max(columns) - min(columns) + 1) / size if columns else 0.0
            shape = f"corner alpha {corners:3d} coverage {coverage:.3f} glyph span {span:.2f}"
            ok = corners == 0 and 0.74 < coverage < 0.83 and 0.55 < span < 0.78
        else:
            shape = "opaque tile" if name == "banner.png" else "glyph, transparent behind"
            ok = True
        ok = ok and cube < 0.02
        if not ok:
            failures.append(f"{density}/{name}")
        print(f"{name:24s} {image.size[0]:3d}x{image.size[1]:<3d} cube-palette {100 * cube:5.2f}%  {shape}  {'ok' if ok else 'FAIL'}")

    print()
    if failures:
        print("FAILED: " + ", ".join(failures), file=sys.stderr)
        return 1
    print("OK: no written asset carries the retired cube palette, and both web icons are round.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
