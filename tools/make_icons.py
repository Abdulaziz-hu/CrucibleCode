#!/usr/bin/env python3
"""Regenerates the CrucibleCode logo assets (PNG / ICO / ICNS) from the geometry below.
The SVG files in resources/logo use exactly the same shapes.   Requires: pip install pillow
"""
from pathlib import Path
from PIL import Image, ImageDraw

BLUE, WHITE, EMBER, SPARK = "#007ACC", "#FFFFFF", "#FFB02E", "#FFD479"
DY = 8  # vertical offset of the artwork inside the 256x256 grid

SHAPES = [  # (kind, coords, colour) on a 256 grid
    ("poly", [(128, 34), (150, 70), (128, 106), (106, 70)], EMBER),   # ember
    ("poly", [(176, 52), (184, 66), (176, 80), (168, 66)], SPARK),    # spark
    ("rect", (52, 118, 204, 132), WHITE),                             # crucible rim
    ("poly", [(64, 132), (192, 132), (166, 196), (90, 196)], WHITE),  # crucible bowl
    ("rect", (106, 196, 150, 208), WHITE),                            # foot
]


def render(size: int) -> Image.Image:
    ss = 4                                   # supersampling
    s = size * ss
    k = s / 256.0
    img = Image.new("RGBA", (s, s), BLUE)
    d = ImageDraw.Draw(img)
    for kind, g, col in SHAPES:
        if kind == "poly":
            d.polygon([(x * k, (y + DY) * k) for x, y in g], fill=col)
        else:
            x0, y0, x1, y1 = g
            d.rectangle([x0 * k, (y0 + DY) * k, x1 * k - 1, (y1 + DY) * k - 1], fill=col)
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    out = Path(__file__).resolve().parent.parent / "resources" / "icons"
    out.mkdir(parents=True, exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256, 512]
    imgs = {s: render(s) for s in sizes}
    for s, im in imgs.items():
        im.save(out / f"icon-{s}.png")
    imgs[256].save(out / "icon.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    try:
        imgs[512].save(out / "icon.icns", append_images=[imgs[s] for s in (16, 32, 64, 128, 256)])
    except Exception as e:  # pragma: no cover
        print("ICNS not written:", e)
    print("written to", out)


if __name__ == "__main__":
    main()
