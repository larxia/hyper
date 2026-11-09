#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HYPER's small mark: a pixel-built neon "H" (assets/hyper/mark.png; the boot screen now shows the full art, assets/hyper/splash.png).

  gen_hyper_logo.py OUT.png

A 9 x 9 grid of square cells (the "pixel" look of the screen): the H's cells filled with a neon yellow -> electric
green gradient (top to bottom), each with a 1-cell acid-lime rim on its lower right (a cheap extrusion), four
four-pointed sparkles around it, and a thin scan line through the middle. Transparent elsewhere: the screen's
near-black shows. 512 x 512 so gen_splash_image.py scales it down to 144 x 144 like the other pictures.
No outside artwork: everything is drawn here.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 512
YELLOW = (244, 255, 58)      # #F4FF3A
GREEN = (57, 255, 20)        # #39FF14
LIME = (182, 255, 0)         # #B6FF00
WHITE = (245, 245, 245)      # #F5F5F5
DEEP = (18, 60, 10)          # the extrusion's shadow side

# the H on a 9 x 9 grid (1: a cell of the letter)
H = ["11.....11",
     "11.....11",
     "11.....11",
     "11.....11",
     "111111111",
     "11.....11",
     "11.....11",
     "11.....11",
     "11.....11"]


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def sparkle(d, cx, cy, r, col):
    """a four-pointed star (two thin diamonds), the hyperpop glint"""
    w = max(2, r // 5)
    d.polygon([(cx, cy - r), (cx + w, cy), (cx, cy + r), (cx - w, cy)], fill=col + (255,))
    d.polygon([(cx - r, cy), (cx, cy - w), (cx + r, cy), (cx, cy + w)], fill=col + (255,))


def main():
    out = Path(sys.argv[1])
    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cell, gap = 40, 4                       # 9 cells of 40 + gaps: 360 px, centred
    side = 9 * cell
    x0 = (SIZE - side) // 2
    y0 = (SIZE - side) // 2
    for r, row in enumerate(H):             # the extrusion first, one step down and right, under the face
        for c, ch in enumerate(row):
            if ch == "1":
                x, y = x0 + c * cell + 10, y0 + r * cell + 10
                d.rectangle([x, y, x + cell - gap, y + cell - gap], fill=DEEP + (255,))
    for r, row in enumerate(H):
        for c, ch in enumerate(row):
            if ch != "1":
                continue
            x, y = x0 + c * cell, y0 + r * cell
            face = mix(YELLOW, GREEN, r / 8)
            d.rectangle([x, y, x + cell - gap, y + cell - gap], fill=face + (255,))
            d.rectangle([x + cell - gap - 6, y, x + cell - gap, y + cell - gap], fill=LIME + (255,))   # lit rim
            d.rectangle([x, y, x + 8, y + 8], fill=WHITE + (255,))     # the pixel's highlight
    d.rectangle([x0 - 24, y0 + side // 2 - 3, x0 + side + 24, y0 + side // 2 + 1], fill=WHITE + (255,))   # scan line
    sparkle(d, x0 - 28, y0 + 20, 34, YELLOW)
    sparkle(d, x0 + side + 30, y0 + side - 10, 40, GREEN)
    sparkle(d, x0 + side + 18, y0 - 18, 20, WHITE)
    sparkle(d, x0 - 10, y0 + side + 22, 16, LIME)
    out.parent.mkdir(parents=True, exist_ok=True)
    im.save(out)
    print(f"hyper logo: {SIZE}x{SIZE} -> {out}")


if __name__ == "__main__":
    main()
