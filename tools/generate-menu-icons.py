#!/usr/bin/env python3
"""Generate the vector icons drawn for the in-game menu's lower items.

Why this exists
---------------
The menu's other icons are white PNGs at 80% opacity on a 64 px canvas at xhdpi
(drawable-xhdpi/ic_*.png): outlines about 3.5 px wide, solid shapes with cut-outs, and glyphs
about 44 px across. These icons are drawn on the same 64-unit grid, so one unit is one pixel
at xhdpi, and written as vector drawables so they stay sharp at any density.

Each icon is one filled path in #CCFFFFFF. A hole is a contour wound against the contour
around it, because API 23 (minSdk) ignores android:fillType and fills with the nonZero rule,
and a single path keeps overlapping parts from doubling the opacity. Arcs are written as
cubic Beziers.

Run from anywhere:  python3 tools/generate-menu-icons.py [--check]
With --check it writes nothing and exits non-zero if a drawable differs from what it would
write.
"""

import math
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DRAWABLE = REPO_ROOT / "app/src/main/res/drawable"

STROKE = 3.5  # outline width of ic_box, ic_clock, ic_picture and ic_phone at xhdpi


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def signed_area(pts):
    """Positive for a contour that runs clockwise on screen, where y grows downwards."""
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))


class Glyph:
    def __init__(self):
        self.contours = []

    def poly(self, pts, positive=True):
        if (signed_area(pts) > 0) != positive:
            pts = pts[::-1]
        self.contours.append("M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in pts) + "Z")

    def rect(self, x0, y0, x1, y1, positive=True):
        self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], positive)

    def shape(self, segments, positive=True):
        """segments: ('L', x, y) points and ('A', cx, cy, r, a0, a1) arcs, with angles in
        degrees on screen (0 = right, 90 = down); arcs become Beziers of at most 90 degrees."""
        cmds, pts = [], []
        for seg in segments:
            if seg[0] == "L":
                _, x, y = seg
                cmds.append(("M" if not cmds else "L") + f"{fmt(x)},{fmt(y)}")
                pts.append((x, y))
                continue
            _, cx, cy, r, a0, a1 = seg
            n = max(1, math.ceil(abs(a1 - a0) / 90 - 1e-9))
            step = (a1 - a0) / n
            start = (cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0)))
            cmds.append(("M" if not cmds else "L") + f"{fmt(start[0])},{fmt(start[1])}")
            pts.append(start)
            for i in range(n):
                t0 = math.radians(a0 + i * step)
                t1 = math.radians(a0 + (i + 1) * step)
                k = 4 / 3 * math.tan((t1 - t0) / 4)
                p0 = (cx + r * math.cos(t0), cy + r * math.sin(t0))
                p3 = (cx + r * math.cos(t1), cy + r * math.sin(t1))
                p1 = (p0[0] - k * r * math.sin(t0), p0[1] + k * r * math.cos(t0))
                p2 = (p3[0] + k * r * math.sin(t1), p3[1] - k * r * math.cos(t1))
                cmds.append("C" + " ".join(f"{fmt(a)},{fmt(b)}" for a, b in (p1, p2, p3)))
                for j in range(1, 9):
                    t = t0 + (t1 - t0) * j / 8
                    pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
        if (signed_area(pts) > 0) != positive:
            raise ValueError("contour winds the wrong way")
        self.contours.append("".join(cmds) + "Z")

    def round_rect(self, x0, y0, x1, y1, r, positive=True):
        if positive:
            self.shape([("A", x1 - r, y0 + r, r, 270, 360), ("A", x1 - r, y1 - r, r, 0, 90),
                        ("A", x0 + r, y1 - r, r, 90, 180), ("A", x0 + r, y0 + r, r, 180, 270)])
        else:
            self.shape([("A", x0 + r, y0 + r, r, 270, 180), ("A", x0 + r, y1 - r, r, 180, 90),
                        ("A", x1 - r, y1 - r, r, 90, 0), ("A", x1 - r, y0 + r, r, 360, 270)],
                       positive=False)

    def path_data(self):
        return "".join(self.contours)


def page(g, dog=10):
    """Document outline, box 14..50 x 9..55, with a solid folded corner."""
    x0, y0, x1, y1 = 14, 9, 50, 55
    g.poly([(x0, y0), (x1 - dog, y0), (x1, y0 + dog), (x1, y1), (x0, y1)])
    fold_x, fold_y = x1 - dog - STROKE, y0 + dog + STROKE
    g.poly([(x0 + STROKE, y0 + STROKE), (fold_x, y0 + STROKE), (fold_x, fold_y),
            (x1 - STROKE, fold_y), (x1 - STROKE, y1 - STROKE), (x0 + STROKE, y1 - STROKE)],
           positive=False)


def arrow(g, cx, tip, base, end, head=9.5, shaft=3):
    """Vertical arrow from its tip to its head's base, then its shaft to end."""
    g.poly([(cx, tip), (cx + head, base), (cx + shaft, base), (cx + shaft, end),
            (cx - shaft, end), (cx - shaft, base), (cx - head, base)])


def file_open():
    """Load from file: a document with an arrow pointing up."""
    g = Glyph()
    page(g)
    arrow(g, 32, 24.5, 34.5, 47.5)
    return g


def file_save():
    """Save to file: a document with an arrow pointing down."""
    g = Glyph()
    page(g)
    arrow(g, 32, 47.5, 37.5, 25.5)
    return g


def history(cx=34, cy=32, outer=20.5, inner=17, head=7.5):
    """Load from auto save: clock hands in a ring that ends in a backward arrow, unlike
    ic_clock's closed ring."""
    g = Glyph()
    end = 180 + 315
    g.shape([("A", cx, cy, outer, 180, end), ("A", cx, cy, inner, end, 180)])
    hx = cx - (outer + inner) / 2
    g.poly([(hx - head, cy - 0.5), (hx + head, cy - 0.5), (hx, cy + 8)])
    g.rect(cx - 1.5, cy - 11.5, cx + 1.5, cy + 1.5)
    d = 1.5 / math.sqrt(2)
    ex, ey = cx + 7.5, cy + 7.5
    g.poly([(cx + d, cy - d), (ex + d, ey - d), (ex - d, ey + d), (cx - d, cy + d)])
    return g


def keyboard():
    """Change IME: a keyboard outline with two rows of keys and a space bar."""
    g = Glyph()
    x0, y0, x1, y1 = 9, 17, 55, 47
    g.round_rect(x0, y0, x1, y1, 3.5)
    g.round_rect(x0 + STROKE, y0 + STROKE, x1 - STROKE, y1 - STROKE, 0.5, positive=False)
    for row in (22.75, 29.75):
        for col in (15.75, 22.75, 29.75, 36.75, 43.75):
            g.rect(col, row, col + 4.5, row + 4.5)
    g.rect(15.75, 36.75, 20.25, 41.25)
    g.rect(22.75, 36.75, 41.25, 41.25)
    g.rect(43.75, 36.75, 48.25, 41.25)
    return g


def pak():
    """A player's Controller Pak, drawn as a memory card with contacts and a label."""
    g = Glyph()
    g.round_rect(15, 9, 49, 55, 3)
    for col in (20, 26, 32, 38):
        g.rect(col, 13, col + 2.5, 21, positive=False)
    g.rect(19.5, 26, 44.5, 50.5, positive=False)
    return g


ICONS = {
    "ic_file_open": file_open,
    "ic_file_save": file_save,
    "ic_history": history,
    "ic_keyboard": keyboard,
    "ic_pak": pak,
}


def vector_xml(path_data):
    return ('<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    android:width="32dp"\n'
            '    android:height="32dp"\n'
            '    android:viewportWidth="64.0"\n'
            '    android:viewportHeight="64.0">\n'
            '    <path\n'
            '        android:fillColor="#CCFFFFFF"\n'
            f'        android:pathData="{path_data}"/>\n'
            '</vector>\n')


def main() -> int:
    check = "--check" in sys.argv[1:]
    stale = []
    for name, draw in ICONS.items():
        target = DRAWABLE / f"{name}.xml"
        xml = vector_xml(draw().path_data())
        if check:
            if not target.exists() or target.read_text() != xml:
                stale.append(target)
        else:
            target.write_text(xml)
            print(f"wrote {target.relative_to(REPO_ROOT)}")
    for target in stale:
        print(f"differs from the generator: {target.relative_to(REPO_ROOT)}", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
