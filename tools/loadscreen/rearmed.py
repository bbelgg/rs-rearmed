#!/usr/bin/env python3
"""Paint a red "Re-ARMed 2026" graffiti tag over the Rocksmith 2014 Remastered logo.

    rearmed.py <logo.png> <out.png> [--variant A|B|C] [--seed N]

Input and output are 1024x256 RGBA images of gfxassets/views/logo_elk_final.dds
(or logo_graded_elk_final.dds). Needs Pillow and numpy.

The tag is drawn at SS times the size with hard edges and scaled down once at
the end, so its edges are crisp and only anti-aliased: flat red fill, a shine
along the top-left of each stroke, a thin dark outline, a cream border so the
tag reads over the navy letters, and a few paint drips.

Variants:
  A  tag tilted over "REMASTERED"
  B  "2014 EDITION" struck out, tag over "REMASTERED"
  C  big tag slashed across the whole logo
"""
import argparse

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

FONT = "/System/Library/Fonts/Supplemental/MarkerFelt.ttc"
FONT_INDEX = 1  # Marker Felt Wide
RED = (210, 24, 30)
SHINE = (255, 120, 112)
INK = (28, 20, 26)
# Pale border so the red reads over the navy letters.
CREAM = (242, 238, 230)
SS = 4  # all tag layers are drawn at SS x the logo size

# Widths in logo pixels.
INK_W = 3.0  # thinner outlines smear into the red under DXT5
CREAM_W = 3.5
SHINE_W = 1.5


def text_mask(size, text, font_px, center, angle):
    """Hard mask of `text` at SS scale, centred at `center`, rotated by `angle` degrees."""
    w, h = size
    big = Image.new("L", (w * SS, h * SS), 0)
    font = ImageFont.truetype(FONT, font_px * SS, index=FONT_INDEX)
    d = ImageDraw.Draw(big)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=font)
    cx, cy = center[0] * SS, center[1] * SS
    d.text((cx - (x0 + x1) / 2, cy - (y0 + y1) / 2), text, font=font, fill=255)
    return big.rotate(angle, resample=Image.BICUBIC, center=(cx, cy)).point(lambda v: 255 * (v >= 128))


def stroke_mask(size, p0, p1, width):
    """A hand-painted line from p0 to p1 at SS scale, slightly wobbly."""
    w, h = size
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    rng = np.random.default_rng(7)
    pts = [(x * SS, (y + rng.normal(0, 1.0)) * SS) for x, y in np.linspace(p0, p1, 12)]
    d.line(pts, fill=255, width=int(width * SS), joint="curve")
    for x, y in (pts[0], pts[-1]):
        r = width * SS / 2
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    return big


def dilate(mask, px):
    """Grow a mask by `px` logo pixels (round-ish, in 2-pixel steps at SS scale)."""
    for _ in range(max(1, round(px * SS / 2))):
        mask = mask.filter(ImageFilter.MaxFilter(5))
    return mask


def add_drips(mask, rng, count, max_len):
    """Paint runs hanging off the lower edges of the mask: tapered, with a bead."""
    a = np.asarray(mask) > 127
    edge = a[:-SS] & ~a[SS:]  # solid here, empty SS rows below
    ys, xs = np.nonzero(edge)
    out = mask.copy()
    if len(xs) == 0:
        return out
    d = ImageDraw.Draw(out)
    # Spread the drips out: pick from distinct columns.
    picks = rng.choice(len(xs), size=min(count * 8, len(xs)), replace=False)
    used = []
    for i in picks:
        x, y = int(xs[i]), int(ys[i])
        if any(abs(x - u) < 40 * SS for u in used):
            continue
        used.append(x)
        length = rng.uniform(0.35, 1.0) * max_len * SS
        top_w = rng.uniform(1.4, 2.4) * SS
        bot_w = top_w * 0.6
        d.polygon([(x - top_w, y - SS), (x + top_w, y - SS),
                   (x + bot_w, y + length), (x - bot_w, y + length)], fill=255)
        r = bot_w * 1.45
        d.ellipse((x - r, y + length - r, x + r, y + length + r), fill=255)
        if len(used) == count:
            break
    return out


def shine(fill):
    """Thin highlight along the top-left edges of the strokes, inside the fill."""
    off = round(SHINE_W * SS)
    shifted = ImageChops.offset(fill, off, off)
    return ImageChops.subtract(fill, shifted).filter(ImageFilter.MinFilter(3))


def down(mask, size):
    return np.asarray(mask.resize(size, Image.BOX), np.float32) / 255


def composite(logo, layers):
    rgb = logo[..., :3].astype(np.float32)
    la = logo[..., 3:4].astype(np.float32) / 255
    for color, a in layers:
        a = a[..., None]
        out_a = a + la * (1 - a)
        rgb = (np.array(color, np.float32) * a + rgb * la * (1 - a)) / np.maximum(out_a, 1e-6)
        la = out_a
    out = np.concatenate([rgb, la * 255], axis=-1)
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


def tag(size, variant):
    if variant == "A":
        return text_mask(size, "Re-ARMed 2026", 76, (610, 182), 6)
    if variant == "B":
        strike = stroke_mask(size, (136, 200), (316, 180), 7)
        return ImageChops.lighter(text_mask(size, "Re-ARMed 2026", 70, (630, 186), 5), strike)
    if variant == "C":
        return text_mask(size, "Re-ARMed 2026", 118, (512, 132), 8)
    raise SystemExit(f"unknown variant {variant}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logo")
    ap.add_argument("out")
    ap.add_argument("--variant", default="A")
    ap.add_argument("--seed", type=int, default=2026)
    a = ap.parse_args()
    logo = np.asarray(Image.open(a.logo).convert("RGBA"))
    size = (logo.shape[1], logo.shape[0])
    rng = np.random.default_rng(a.seed)

    fill = add_drips(dilate(tag(size, a.variant), 0.5), rng, count=6, max_len=20)
    ink = dilate(fill, INK_W)
    cream = dilate(ink, CREAM_W)
    layers = [(CREAM, down(cream, size)), (INK, down(ink, size)),
              (RED, down(fill, size)), (SHINE, down(shine(fill), size))]
    Image.fromarray(composite(logo, layers)).save(a.out)


if __name__ == "__main__":
    main()
