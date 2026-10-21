#!/usr/bin/env python3
"""Generate the circular "JD" avatar used by the dashboard top bar.

Requires Pillow; falls back to Pillow's built-in font if DejaVu is absent.
"""
import sys

from PIL import Image, ImageDraw, ImageFont

W = 80
S = 4


def main(out):
    img = Image.new("RGBA", (W * S, W * S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Vertical violet gradient, clipped to a circle below.
    for y in range(W * S):
        t = y / (W * S - 1)
        draw.line([(0, y), (W * S, y)],
                  fill=(int(146 + (114 - 146) * t),
                        int(97 + (60 - 97) * t),
                        int(241 + (234 - 241) * t), 255))
    mask = Image.new("L", (W * S, W * S), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, W * S - 1, W * S - 1), fill=255)
    img.putalpha(mask)

    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30 * S)
    except OSError:
        font = ImageFont.load_default()
    bbox = draw.textbbox((0, 0), "JD", font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text(((W * S - tw) // 2 - bbox[0], (W * S - th) // 2 - bbox[1] - 2 * S),
              "JD", font=font, fill=(255, 255, 255, 255))
    img.resize((W, W), Image.LANCZOS).save(out, "PNG", optimize=True)
    print("wrote", out, (W, W))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "avatar.png")
