#!/usr/bin/env python3
"""Generate the split-circle "Loud" mark used by the landing-page demo.

The reference logo is a lavender disc with the top-right quadrant in a
stronger violet, separated from the rest by a thin dark gap.  Rendering it
as a PNG keeps the mark crisp at the small size the demo draws it and lets
the Incense markup stay a plain Image node.
"""
import sys

from PIL import Image, ImageDraw

W = 128
S = 4


def main(out):
    n = W * S
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Soft vertical gradient for the three light quadrants.
    for y in range(n):
        t = y / (n - 1)
        color = (
            int(206 + (172 - 206) * t),
            int(186 + (146 - 186) * t),
            int(250 + (240 - 250) * t),
            255,
        )
        draw.rectangle([0, y, n, y + 1], fill=color)

    # Top-right quadrant in the dominant violet.
    draw.pieslice([0, 0, n - 1, n - 1], start=-90, end=0,
                  fill=(126, 78, 238, 255))

    # Punch the disc out of the square, then carve thin gaps along the
    # vertical and horizontal seams so the split reads at 32 px.
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, n - 1, n - 1], fill=255)
    gap = max(2, n // 40)
    ImageDraw.Draw(mask).rectangle([n // 2 - gap, 0, n // 2 + gap - 1, n // 2],
                                   fill=0)
    ImageDraw.Draw(mask).rectangle([n // 2, n // 2 - gap, n, n // 2 + gap - 1],
                                   fill=0)
    img.putalpha(mask)

    img.resize((W, W), Image.LANCZOS).save(out, "PNG", optimize=True)
    print("wrote", out, (W, W))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "loud_logo.png")
