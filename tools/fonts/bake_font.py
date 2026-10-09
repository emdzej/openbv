#!/usr/bin/env python3
"""Bake a TrueType font into BV2's font texture (main/fonts/babo.tga).

The game's font loader (CFont::loadTGAFile, src/engine/dk/CFont.cpp) reads a 512x512 RGBA TGA:
characters 33..159 in rows of 64-pixel cells, left to right, each glyph a run of columns with some
alpha, glyphs separated by fully transparent columns. A glyph's width is the run's width over 64. The
game's text layouts assume the original font's metrics: capitals from y 20 to the baseline at 44, so
this places glyphs the same way.

Glyphs whose ink has gaps between columns (", %, ...) would be read as several glyphs, so every glyph
gets an invisible row (alpha 1 of 255) along the bottom of its cell across its whole advance: one run
per character, and the run's width is the character's advance.

Needs Pillow:  python3 -m venv .deps/py && .deps/py/bin/pip install pillow
Usage:         .deps/py/bin/python tools/fonts/bake_font.py <font.ttf> <out.tga> [weight]

This file is part of openbv, under the GNU General Public License v3 or later.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

SIZE = 512
CELL = 64
CAP_TOP, BASELINE = 20, 44      # the original font's capitals
SCALE = 4                       # drawn 4x larger, then reduced: smooth edges


def main():
    src, out = sys.argv[1], sys.argv[2]
    weight = int(sys.argv[3]) if len(sys.argv) > 3 else 500

    # the size whose capitals are CAP_TOP..BASELINE high
    def load(px):
        f = ImageFont.truetype(src, px)
        try:
            f.set_variation_by_axes([weight])
        except Exception:
            pass
        return f

    probe = load(100 * SCALE)
    cap = probe.getbbox("H", anchor="ls")          # left, top (negative: above baseline), right, bottom
    cap_h = -cap[1] / (100 * SCALE)
    px = round((BASELINE - CAP_TOP) * SCALE / cap_h)
    font = load(px)

    big = Image.new("L", (SIZE * SCALE, SIZE * SCALE), 0)
    draw = ImageDraw.Draw(big)
    bridges = []            # (x0, x1, row) in final pixels
    x, row = 1, 0
    for code in range(33, 160):
        ch = bytes([code]).decode("cp1252", errors="replace")
        if ch == "�":
            ch = ""          # undefined in Windows-1252: an empty glyph of a narrow width
        adv = font.getlength(ch) / SCALE if ch else 4.0
        ink = font.getbbox(ch, anchor="ls") if ch else (0, 0, 0, 0)
        # the loader adds the empty column on each side to a run: the run is the advance less 2, and
        # the ink (which may reach past it, as in j or f) extends it
        left = min(0.0, ink[0] / SCALE)                      # ink left of the origin
        right = max(adv - 2, ink[2] / SCALE)
        width = max(1, int(round(right - left)))
        if x + width + 2 > SIZE:                            # next row
            x, row = 1, row + 1
            if row >= SIZE // CELL:
                raise SystemExit("font too wide for the texture: use a narrower font or weight")
        ox = x - left - 1                                   # the glyph's origin (one column before the run)
        if ch:
            draw.text(((ox) * SCALE, (row * CELL + BASELINE) * SCALE), ch, fill=255, font=font, anchor="ls")
        bridges.append((x, x + max(1, int(round(adv - 2))), row))
        x += width + 2                                      # one empty column at least between runs

    alpha = big.resize((SIZE, SIZE), Image.LANCZOS)
    pa = alpha.load()
    # the gaps between runs must be empty: clear what the reduction blurred into them
    runs = {}
    for x0, x1, r in bridges:
        runs.setdefault(r, []).append((x0, x1))
    for r in range(SIZE // CELL):
        inside = [False] * SIZE
        for x0, x1 in runs.get(r, []):
            for xx in range(x0, x1):
                inside[xx] = True
        for xx in range(SIZE):
            if not inside[xx]:
                for y in range(r * CELL, r * CELL + CELL):
                    pa[xx, y] = 0
    for x0, x1, r in bridges:
        y = r * CELL + CELL - 1
        for xx in range(x0, x1):
            if pa[xx, y] == 0:
                pa[xx, y] = 1
    # white glyphs, the shape in alpha (as the original)
    rgba = Image.merge("RGBA", (Image.new("L", (SIZE, SIZE), 255),) * 3 + (alpha,))
    # as the original: uncompressed 32-bit, rows stored bottom-up (Pillow flips when writing)
    rgba.save(out, format="TGA", compression=None)
    print(f"{out}: {src} at {px / SCALE:.1f} px, weight {weight}, {row + 1} rows")


if __name__ == "__main__":
    main()
