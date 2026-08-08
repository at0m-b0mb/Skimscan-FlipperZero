#!/usr/bin/env python3
"""Render the Skimscan GitHub banner + social-preview card.

The motif is the product in one picture: a payment card under a scan bar, and
the thing that should not be there -- a small module tapped onto the reader's
data lines, quietly radiating. Everything cold and instrument-like except the
skimmer's carrier, which is the one hot thing on the page.

Supersampled, then LANCZOS-downsampled.
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"

# palette - cold instrument, one hot contact
BG_TOP = (8, 10, 15)
BG_BOT = (14, 17, 27)
INSTR = (88, 196, 255)      # scanner blue: the measurement
THREAT = (255, 78, 66)      # the module, and its carrier
WARM = (255, 138, 92)
GRAY = (146, 156, 172)
WHITE = (240, 246, 252)
DIM = (32, 42, 58)

SS = 2  # supersample


def font(path, px):
    try:
        return ImageFont.truetype(path, px)
    except OSError:
        return ImageFont.truetype(BOLD, px)


def vgradient(w, h):
    img = Image.new("RGB", (w, h), BG_TOP)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line(
            [(0, y), (w, y)],
            fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)),
        )
    return img


def build_card(size, cx, cy, cw):
    """The card, its magstripe, and the scan bar crossing it."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    ch = int(cw * 0.63)
    x0, y0 = cx - cw // 2, cy - ch // 2
    lw = max(2, int(cw * 0.012))
    r = int(cw * 0.07)

    d.rounded_rectangle([x0, y0, x0 + cw, y0 + ch], radius=r, outline=INSTR + (230,), width=lw * 2)

    # magstripe: the part a skimmer is actually after
    sy = y0 + int(ch * 0.16)
    d.rounded_rectangle(
        [x0 + int(cw * 0.06), sy, x0 + cw - int(cw * 0.06), sy + int(ch * 0.17)],
        radius=lw,
        fill=INSTR + (110,),
    )

    # chip: the part it cannot copy
    chip_x, chip_y = x0 + int(cw * 0.10), y0 + int(ch * 0.48)
    chip_w, chip_h = int(cw * 0.16), int(ch * 0.26)
    d.rounded_rectangle(
        [chip_x, chip_y, chip_x + chip_w, chip_y + chip_h],
        radius=lw,
        outline=INSTR + (230,),
        width=lw,
    )
    d.line([chip_x, chip_y + chip_h // 2, chip_x + chip_w, chip_y + chip_h // 2],
           fill=INSTR + (230,), width=lw)
    d.line([chip_x + chip_w // 2, chip_y, chip_x + chip_w // 2, chip_y + chip_h],
           fill=INSTR + (230,), width=lw)

    # embossed digits
    dy = y0 + int(ch * 0.83)
    for g in range(4):
        gx = x0 + int(cw * 0.10) + g * int(cw * 0.21)
        for i in range(4):
            d.ellipse(
                [gx + i * lw * 5, dy, gx + i * lw * 5 + lw * 2, dy + lw * 2],
                fill=INSTR + (150,),
            )
    return layer, (x0, y0, cw, ch)


def build_scanbar(size, x0, y0, cw, ch, frac=0.62):
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    lw = max(2, int(cw * 0.012))
    sx = int(x0 + cw * frac)
    pad = int(ch * 0.28)
    d.line([sx, y0 - pad, sx, y0 + ch + pad], fill=WHITE + (255,), width=lw * 2)
    for k in range(1, 5):
        d.line(
            [sx - k * lw * 3, y0 - pad, sx - k * lw * 3, y0 + ch + pad],
            fill=INSTR + (int(120 / k),),
            width=lw,
        )
    return layer


def build_module(size, x0, y0, cw, ch):
    """The thing that should not be there: a bridge module on the reader's
    lines, still radiating."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    lw = max(2, int(cw * 0.012))

    mw, mh = int(cw * 0.30), int(cw * 0.17)
    mx = x0 + cw + int(cw * 0.30)
    my = y0 + ch - mh
    d.rounded_rectangle([mx, my, mx + mw, my + mh], radius=lw, outline=THREAT + (255,), width=lw * 2)
    for i in range(6):  # pin header
        px = mx + int(mw * 0.12) + i * int(mw * 0.15)
        d.line([px, my + mh, px, my + mh + lw * 4], fill=THREAT + (255,), width=lw)

    # the tap: two wires back to the card's stripe
    for k in range(2):
        wy = my + int(mh * (0.35 + 0.30 * k))
        d.line([x0 + cw - int(cw * 0.06), wy, mx, wy], fill=THREAT + (190,), width=lw)

    # the carrier going out
    ccx, ccy = mx + mw, my + mh // 2
    for k in range(1, 6):
        rr = int(cw * 0.09) * k
        d.arc(
            [ccx - rr, ccy - rr, ccx + rr, ccy + rr],
            start=-58,
            end=58,
            fill=THREAT + (int(230 * (1 - k / 6.5)),),
            width=lw * 2,
        )
    return layer, (ccx, ccy)


def render(path, W, H, layout="wide"):
    w, h = W * SS, H * SS
    img = vgradient(w, h).convert("RGBA")

    if layout == "wide":
        cx, cy, cw = int(w * 0.735), int(h * 0.47), int(h * 0.52)
    else:
        cx, cy, cw = int(w * 0.46), int(h * 0.27), int(h * 0.30)

    card, (x0, y0, cwid, ch) = build_card((w, h), cx, cy, cw)
    module, _ = build_module((w, h), x0, y0, cwid, ch)
    bar = build_scanbar((w, h), x0, y0, cwid, ch)

    img.alpha_composite(card.filter(ImageFilter.GaussianBlur(5 * SS)))
    img.alpha_composite(card)
    img.alpha_composite(module.filter(ImageFilter.GaussianBlur(6 * SS)))
    img.alpha_composite(module)
    img.alpha_composite(bar.filter(ImageFilter.GaussianBlur(4 * SS)))
    img.alpha_composite(bar)

    # ---- text ----
    tx = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(tx)

    if layout == "wide":
        x, kicker_y, title_y, title_px = 70 * SS, 92 * SS, 120 * SS, 112 * SS
        anchor = "la"
    else:
        x, kicker_y, title_y, title_px = w // 2, 330 * SS, 356 * SS, 120 * SS
        anchor = "ma"

    f_kick = font(MONO, 22 * SS)
    f_title = font(BLACK_F, title_px)
    f_tag = font(BOLD, 34 * SS)
    f_sub = font(REG, 23 * SS)
    f_foot = font(MONO, 21 * SS)

    td.text((x, kicker_y), "FLIPPER ZERO  ·  BLUETOOTH SKIMMER DETECTOR",
            font=f_kick, fill=INSTR, anchor=anchor)
    td.text((x + 4 * SS, title_y + 4 * SS), "SKIMSCAN", font=f_title,
            fill=THREAT + (140,), anchor=anchor)
    td.text((x, title_y), "SKIMSCAN", font=f_title, fill=WHITE, anchor=anchor)

    tag_y = title_y + title_px + 22 * SS
    td.text((x, tag_y), "Check before you swipe.", font=f_tag, fill=INSTR, anchor=anchor)
    td.text(
        (x, tag_y + 43 * SS),
        "Finds the two-dollar radio a card skimmer talks to the car park with.",
        font=f_sub,
        fill=GRAY,
        anchor=anchor,
    )

    img.alpha_composite(tx)

    fd = ImageDraw.Draw(img)
    fd.line([(70 * SS, h - 54 * SS), (w - 70 * SS, h - 54 * SS)], fill=DIM, width=2 * SS)
    fd.text((70 * SS, h - 44 * SS), "github.com/at0m-b0mb/Skimscan-FlipperZero",
            font=f_foot, fill=GRAY)
    fd.text((w - 70 * SS, h - 44 * SS), "MIT · by at0m-b0mb", font=f_foot, fill=GRAY, anchor="ra")

    img.convert("RGB").resize((W, H), Image.LANCZOS).save(path)
    print("wrote", path)
    _ = (math, WARM)


if __name__ == "__main__":
    render(os.path.join(OUT, "banner.png"), 1280, 400, layout="wide")
    render(os.path.join(OUT, "social-preview.png"), 1280, 640, layout="card")
