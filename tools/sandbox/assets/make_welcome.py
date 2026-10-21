#!/usr/bin/env python3
"""Generate the flat waving-person illustration on the dashboard hero card.

Drawn at 4x and downsampled for clean edges; the PNG keeps a transparent
background so it can sit on the blue card.  Requires Pillow.
"""
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 4
W, H = 320, 280


def main(out):
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    def sc(v):
        return int(v * S)

    def ell(box, fill):
        draw.ellipse(tuple(sc(v) for v in box), fill=fill)

    def rrect(box, radius, fill):
        draw.rounded_rectangle(tuple(sc(v) for v in box), radius=sc(radius), fill=fill)

    def line(points, width, fill):
        draw.line([(sc(x), sc(y)) for x, y in points], fill=fill,
                  width=sc(width), joint="curve")

    # Soft ground shadow.
    shadow = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse((sc(92), sc(242), sc(228), sc(268)),
                                   fill=(24, 10, 60, 110))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(sc(6))))
    draw = ImageDraw.Draw(img)

    # Backdrop circle + floating dots.
    ell((50, 38, 270, 258), (114, 60, 234, 255))
    ell((62, 50, 258, 246), (172, 139, 243, 90))
    for x, y, r, colour in [(58, 84, 5, (255, 255, 255, 120)),
                            (268, 110, 4, (255, 255, 255, 100)),
                            (246, 232, 6, (255, 255, 255, 80)),
                            (78, 214, 4, (255, 255, 255, 80))]:
        ell((x - r, y - r, x + r, y + r), colour)

    # Legs + shoes.
    rrect((137, 208, 160, 252), 10, (38, 30, 70, 255))
    rrect((163, 208, 186, 252), 10, (38, 30, 70, 255))
    rrect((131, 244, 160, 258), 7, (248, 250, 252, 255))
    rrect((163, 244, 192, 258), 7, (248, 250, 252, 255))

    # Shirt.
    rrect((118, 130, 202, 220), 26, (212, 253, 99, 255))
    draw.polygon([(sc(146), sc(132)), (sc(160), sc(150)), (sc(174), sc(132))],
                 fill=(233, 255, 170, 255))

    # Arms: right down, left raised waving.
    line([(192, 152), (214, 188)], 17, (212, 253, 99, 255))
    ell((206, 182, 224, 200), (242, 194, 155, 255))
    line([(130, 154), (104, 118), (92, 92)], 17, (212, 253, 99, 255))
    ell((80, 80, 102, 102), (242, 194, 155, 255))
    for a, b in [((84, 84), (80, 74)), ((89, 82), (88, 71)), ((94, 83), (96, 73))]:
        line([a, b], 5, (242, 194, 155, 255))

    # Neck, head, ears.
    rrect((150, 116, 170, 138), 8, (226, 174, 136, 255))
    ell((134, 62, 186, 122), (242, 194, 155, 255))
    ell((130, 88, 140, 102), (226, 174, 136, 255))
    ell((180, 88, 190, 102), (226, 174, 136, 255))

    # Hair.
    draw.pieslice((sc(131), sc(54), sc(189), sc(108)), 180, 360,
                  fill=(43, 27, 18, 255))
    draw.rectangle((sc(131), sc(82), sc(138), sc(100)), fill=(43, 27, 18, 255))
    draw.rectangle((sc(182), sc(82), sc(189), sc(100)), fill=(43, 27, 18, 255))
    draw.ellipse((sc(150), sc(55), sc(186), sc(82)), fill=(43, 27, 18, 255))

    # Face.
    ell((148, 92, 154, 98), (32, 33, 36, 255))
    ell((166, 92, 172, 98), (32, 33, 36, 255))
    draw.arc((sc(151), sc(99), sc(169), sc(114)), 15, 165,
             fill=(120, 63, 42, 255), width=sc(2))
    ell((143, 101, 151, 106), (244, 160, 140, 110))
    ell((169, 101, 177, 106), (244, 160, 140, 110))

    img.resize((W, H), Image.LANCZOS).save(out, "PNG", optimize=True)
    print("wrote", out, (W, H))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "welcome.png")
