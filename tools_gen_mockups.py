#!/usr/bin/env python3
"""Render Flipper-style mock screenshots (128x64, orange backlight) for the README.

Two rules make these worth having:

1. They mirror the on-device draw code in views/*.c constant for constant, so a
   layout collision shows up here before it ships. Text is positioned by
   BASELINE (PIL anchor "ls"/"rs"/"ms") because canvas_draw_str takes y as the
   baseline; canvas_draw_str_aligned with AlignCenter vertically is anchor "mm".

2. The *content* is not made up. `make -C test dump` runs the scripted forecourt
   through the real scoring engine and writes test/demo_dump.json; every number,
   verdict and cap reason below is read out of that file. The screenshots cannot
   drift away from what the app actually decides.

    make -C test dump && python3 tools_gen_mockups.py
"""
from PIL import Image, ImageDraw, ImageFont
import json
import os
import subprocess
import sys

# The real 10x10 glyphs, so header icons here are the same pixels fbt compiles
# into the .fap rather than a stand-in box.
from tools_gen_icons import GLYPHS

S = 6  # upscale factor
W, H = 128, 64
BG = (255, 130, 0)  # flipper backlight orange
FG = (10, 8, 4)  # near-black pixels
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
DUMP = os.path.join(HERE, "test", "demo_dump.json")
os.makedirs(OUT, exist_ok=True)

MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

f_sec = ImageFont.truetype(MONO, 7 * S - 2)  # FontSecondary
f_pri = ImageFont.truetype(BOLD, 8 * S)  # FontPrimary

# ---------------- engine output ----------------


def load_dump():
    if not os.path.exists(DUMP):
        subprocess.run(["make", "-C", "test", "dump"], cwd=HERE, check=True)
    with open(DUMP) as fh:
        return json.load(fh)


DUMP_DATA = load_dump()
DEVICES = DUMP_DATA["devices"]


def device(name):
    for d in DEVICES:
        if d["name"] == name:
            return d
    raise KeyError(f"{name!r} is not in the demo dump: {[d['name'] for d in DEVICES]}")


def flagged_count():
    return sum(1 for d in DEVICES if d["verdict"] != "ORDINARY")


# ---------------- primitives, matching canvas_* semantics ----------------


def canvas():
    img = Image.new("RGB", (W * S, H * S), BG)
    return img, ImageDraw.Draw(img)


def L(v):
    return int(round(v * S))


def line(d, x0, y0, x1, y1, col=FG, w=2):
    d.line([L(x0), L(y0), L(x1), L(y1)], fill=col, width=w)


def box(d, x, y, w, h, col=FG):
    """canvas_draw_box: filled, inclusive of (x,y)..(x+w-1, y+h-1)."""
    d.rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], fill=col)


def frame(d, x, y, w, h, col=FG, lw=2):
    d.rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], outline=col, width=lw)


def rframe(d, x, y, w, h, r, col=FG, lw=2):
    d.rounded_rectangle(
        [L(x), L(y), L(x + w) - 1, L(y + h) - 1], radius=L(r), outline=col, width=lw
    )


def rbox(d, x, y, w, h, r, col=FG):
    d.rounded_rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], radius=L(r), fill=col)


def dot(d, x, y, col=FG):
    d.rectangle([L(x), L(y), L(x + 1) - 1, L(y + 1) - 1], fill=col)


def circle(d, cx, cy, r, col=FG, lw=2):
    d.ellipse([L(cx - r), L(cy - r), L(cx + r), L(cy + r)], outline=col, width=lw)


def text(d, x, y, s, fnt=f_sec, col=FG, anchor="ls"):
    """y is the BASELINE, matching canvas_draw_str."""
    d.text((L(x), L(y)), s, font=fnt, fill=col, anchor=anchor)


def tw(s, fnt=f_sec):
    """String width in Flipper pixels (mirrors canvas_string_width)."""
    return fnt.getlength(s) / S


def clip(s, max_w, fnt=f_sec):
    """Mirrors the pixel-clipping the views do, ellipsis and all."""
    if tw(s, fnt) <= max_w:
        return s
    while len(s) > 1 and tw(s, fnt) > max_w - 6:
        s = s[:-1]
    return s + ".."


def glyph(d, x, y, name):
    """canvas_draw_icon: blit a 1-bit 10x10 icon at (x, y)."""
    for gy, row in enumerate(GLYPHS[name]):
        for gx, ch in enumerate(row):
            if ch == "#":
                dot(d, x + gx, y + gy)


def save(img, name):
    p = os.path.join(OUT, name)
    img.save(p)
    print("wrote", p)
    return name


# ---------------- shared chrome ----------------

HDR_BASE, RULE_Y = 9, 11


def header(d, left, right=None, icon=None):
    if icon:
        glyph(d, 1, 1, icon)
    text(d, 14 if icon else 2, HDR_BASE, left)
    if right:
        text(d, 126, HDR_BASE, right, anchor="rs")
    line(d, 0, RULE_Y, 127, RULE_Y)


def footer(d, y, left, right):
    """Inverted strip: box in FG, text knocked out in BG."""
    box(d, 0, y, 128, 64 - y)
    text(d, 3, 62, left, col=BG)
    text(d, 125, 62, right, col=BG, anchor="rs")


# ---------------- the card (views/card_art.c) ----------------

CARD_W, CARD_H = 50, 28
STRIPE_X, STRIPE_Y, STRIPE_W, STRIPE_H = 3, 4, 44, 5
CHIP_X, CHIP_Y, CHIP_W, CHIP_H = 5, 12, 10, 8
DIGIT_Y, DIGIT_GROUPS, DIGIT_PER_GROUP = 23, 4, 4


def card(d, x, y, inverted=False):
    fgc, bgc = FG, BG
    if inverted:
        rbox(d, x, y, CARD_W, CARD_H, 3)
        fgc = BG
    else:
        rframe(d, x, y, CARD_W, CARD_H, 3)

    box(d, x + STRIPE_X, y + STRIPE_Y, STRIPE_W, STRIPE_H, col=fgc)
    frame(d, x + CHIP_X, y + CHIP_Y, CHIP_W, CHIP_H, col=fgc)
    line(d, x + CHIP_X + 2, y + CHIP_Y + 3, x + CHIP_X + CHIP_W - 3, y + CHIP_Y + 3, col=fgc)
    line(
        d,
        x + CHIP_X + CHIP_W // 2,
        y + CHIP_Y,
        x + CHIP_X + CHIP_W // 2,
        y + CHIP_Y + CHIP_H - 1,
        col=fgc,
    )
    for g in range(DIGIT_GROUPS):
        gx = x + 5 + g * 12
        for i in range(DIGIT_PER_GROUP):
            dot(d, gx + i * 2, y + DIGIT_Y, col=fgc)
    _ = bgc


def scanline(img, d, x, y, col):
    """XOR bar: inverts whatever it crosses, like ColorXOR on the device."""
    px = img.load()
    for cx in (x + col,):
        for yy in range(L(y), L(y + CARD_H)):
            for xx in range(L(cx), L(cx + 1)):
                if 0 <= xx < W * S and 0 <= yy < H * S:
                    px[xx, yy] = FG if px[xx, yy] == BG else BG


# ---------------- splash (views/splash_view.c) ----------------

SP_CARD_X, SP_CARD_Y = 39, 9
SP_TITLE_BASE, SP_TAG_BASE = 51, 61


def draw_splash():
    img, d = canvas()
    line(d, 30, SP_CARD_Y - 3, 30, SP_CARD_Y + CARD_H + 2)
    line(d, 30, SP_CARD_Y - 3, 36, SP_CARD_Y - 3)
    line(d, 30, SP_CARD_Y + CARD_H + 2, 36, SP_CARD_Y + CARD_H + 2)
    card(d, SP_CARD_X, SP_CARD_Y)
    scanline(img, d, SP_CARD_X, SP_CARD_Y, 22)
    text(d, 64, SP_TITLE_BASE, "SKIMSCAN", f_pri, anchor="ms")
    text(d, 64, SP_TAG_BASE, "Check before you swipe", anchor="ms")
    return img


# ---------------- menu (scenes/skimscan_scene_start.c) ----------------


def draw_menu():
    img, d = canvas()
    text(d, 64, 10, "Skimscan", f_pri, anchor="ms")
    line(d, 0, 12, 127, 12)
    items = [
        "Sweep this pump",
        "Devices heard",
        "How skimmers work",
        "Companion wiring",
    ]
    y = 14
    for i, it in enumerate(items):
        if i == 0:
            box(d, 0, y, 128, 12)
            text(d, 4, y + 9, it, col=BG)
        else:
            text(d, 4, y + 9, it)
        y += 12
    return img


# ---------------- sweep (views/sweep_view.c) ----------------

SV_CARD_X, SV_CARD_Y = 2, 14
SV_COL_CX, SV_COL_W = 92, 72
SV_WORD_CY, SV_NAME_BASE, SV_META_BASE = 23, 35, 43
SV_BAR_X, SV_BAR_Y, SV_BAR_W, SV_BAR_H = 2, 45, 124, 7
SV_FOOTER_Y = 53

BAND_FLOORS = {"NOTE": 15, "SUSPECT": 40, "SKIMMER?": 70}
HEADLINE = {"ORDINARY": "NO MATCH", "NOTE": "NOTE", "SUSPECT": "SUSPECT", "SKIMMER?": "SKIMMER?"}


def draw_bar(d, score):
    frame(d, SV_BAR_X, SV_BAR_Y, SV_BAR_W, SV_BAR_H)
    inner_x, inner_w = SV_BAR_X + 1, SV_BAR_W - 2
    fill = (score * inner_w) // 100
    if fill > 0:
        box(d, inner_x, SV_BAR_Y + 1, fill, SV_BAR_H - 2)
    for floor in BAND_FLOORS.values():
        tick = inner_x + (floor * inner_w) // 100
        col = BG if tick < inner_x + fill else FG
        line(d, tick, SV_BAR_Y + 1, tick, SV_BAR_Y + SV_BAR_H - 2, col=col)


def draw_sweep(dev, pass_no, seen, flagged, scanning=True, scan_col=18, listening=False):
    img, d = canvas()
    header(d, "SWEEP (demo)", f"P{pass_no} BR+LE", icon="card_10px")

    alarm = bool(dev) and dev["verdict"] == "SKIMMER?"
    card(d, SV_CARD_X, SV_CARD_Y, inverted=alarm)
    if scanning and not alarm:
        scanline(img, d, SV_CARD_X, SV_CARD_Y, scan_col)

    if listening or not dev:
        word, name, meta = "QUIET", "listening..", None
        score = 0
    else:
        word = HEADLINE[dev["verdict"]]
        name = dev["name"] or "(no name)"
        meta = f"{dev['rssi']}dBm  x{dev['passes']}"
        score = dev["score"]

    text(d, SV_COL_CX, SV_WORD_CY, word, f_pri, anchor="mm")
    if dev and dev["verdict"] in ("SUSPECT", "SKIMMER?"):
        w = tw(word, f_pri)
        frame(d, SV_COL_CX - w / 2 - 3, SV_WORD_CY - 7, w + 6, 14)

    text(d, SV_COL_CX, SV_NAME_BASE, clip(name, SV_COL_W), anchor="ms")
    if meta:
        text(d, SV_COL_CX, SV_META_BASE, meta, anchor="ms")

    draw_bar(d, score)
    footer(d, SV_FOOTER_Y, f"{seen} dev  {flagged} flagged", "OK")
    return img


# ---------------- device list (views/list_view.c) ----------------

LV_ROW_TOP, LV_ROW_H = 12, 17
LV_NAME_BASE, LV_META_BASE, LV_TEXT_W = 9, 16, 108


def draw_list(rows, selected=0):
    img, d = canvas()
    header(d, "DEVICES", f"{selected + 1} of {len(DEVICES)}")

    for i, dev in enumerate(rows):
        y = LV_ROW_TOP + i * LV_ROW_H
        sel = i == selected
        col = FG
        if sel:
            box(d, 0, y, 124, LV_ROW_H)
            col = BG

        name = clip(dev["name"] or "(no name)", LV_TEXT_W)
        text(d, 2, y + LV_NAME_BASE, name, col=col)
        text(d, 122, y + LV_NAME_BASE, str(dev["score"]), col=col, anchor="rs")

        meta = f"{dev['oui']} {dev['rssi']}dBm x{dev['passes']}"
        text(d, 2, y + LV_META_BASE, meta, col=col)
        text(d, 122, y + LV_META_BASE, "LE" if dev["radio"] == "LE" else "BR", col=col, anchor="rs")

    # elements_scrollbar_pos at x=126
    box(d, 126, LV_ROW_TOP, 2, LV_ROW_H * 3, col=FG)
    return img


# ---------------- detail (views/detail_view.c) ----------------

DV_BODY_BASE, DV_LINE_STEP, DV_FOOTER_Y = 21, 9, 54
DV_WHY_BASE, DV_WHY_STEP, DV_WHY_ROWS = 19, 8, 5


def pips(d, page, total=3):
    for i in range(total):
        x = 112 + i * 6
        if i == page:
            box(d, x, 3, 4, 4)
        else:
            frame(d, x, 3, 4, 4)


def draw_detail_device(dev):
    img, d = canvas()
    text(d, 2, HDR_BASE, "DEVICE")
    pips(d, 0)
    line(d, 0, RULE_Y, 127, RULE_Y)

    name = dev["name"] or "(no name)"
    fnt = f_pri if tw(name, f_pri) <= 124 else f_sec
    text(d, 2, DV_BODY_BASE, clip(name, 124, fnt), fnt)

    text(d, 2, DV_BODY_BASE + DV_LINE_STEP, dev["mac"])
    text(d, 2, DV_BODY_BASE + DV_LINE_STEP * 2, dev["vendor"] or "unlisted prefix")
    text(d, 126, DV_BODY_BASE + DV_LINE_STEP * 2, dev["radio"], anchor="rs")
    text(d, 2, DV_BODY_BASE + DV_LINE_STEP * 3, dev["class"])
    text(
        d,
        126,
        DV_BODY_BASE + DV_LINE_STEP * 3,
        f"{dev['rssi']}dBm x{dev['passes']}",
        anchor="rs",
    )

    footer(d, DV_FOOTER_Y, dev["verdict"], str(dev["score"]))
    return img


def draw_detail_why(dev):
    img, d = canvas()
    text(d, 2, HDR_BASE, "WHY")
    pips(d, 1)
    line(d, 0, RULE_Y, 127, RULE_Y)

    sigs = dev["signals"]
    for i, sig in enumerate(sigs[:DV_WHY_ROWS]):
        y = DV_WHY_BASE + i * DV_WHY_STEP
        text(d, 2, y, f"+{sig['points']}")
        label = sig["short"]
        if label == "Module name" and dev["module_note"]:
            label = dev["module_note"]
        elif label in ("Module MAC", "Odd MAC"):
            label = f"{label}: {dev['vendor'] or '?'}"
        elif label == "Still here":
            label = f"Still here, x{dev['passes']}"
        elif label == "Close by":
            label = f"Close: {dev['rssi']}dBm"
        text(d, 24, y, clip(label, 96))

    if len(sigs) > DV_WHY_ROWS:
        box(d, 126, DV_WHY_BASE - 8, 2, DV_WHY_ROWS * DV_WHY_STEP, col=FG)

    left = f"held: {dev['cap']}" if dev["cap"] else f"{dev['families']} of 3 families"
    footer(d, DV_FOOTER_Y, clip(left, 104), str(dev["score"]))
    return img


VERDICT_LINE = {
    "ORDINARY": "Looks like ordinary\nconsumer Bluetooth.",
    "NOTE": "One thing stood out.\nProbably nothing.",
    "SUSPECT": "Several signals line up.\nWorth a second sweep.",
    "SKIMMER?": "This looks like a bridge\nmodule bolted to something.",
}
VERDICT_ADVICE = {
    "ORDINARY": "Nothing to do. Cover the\nPIN pad anyway - most\nskimmers are not radios.",
    "NOTE": "Carry on, but prefer the\nchip or the tap. A stripe\nis the only thing at risk.",
    "SUSPECT": "Do not swipe. Use tap or\nchip, or pay inside, and\nsweep once more first.",
    "SKIMMER?": "Pay inside. Tell the staff\nwhich pump. Do not open\nanything or touch wires.",
}


def draw_detail_means(dev):
    img, d = canvas()
    text(d, 2, HDR_BASE, "WHAT NOW")
    pips(d, 2)
    line(d, 0, RULE_Y, 127, RULE_Y)

    y = 20
    for ln in VERDICT_LINE[dev["verdict"]].split("\n"):
        text(d, 2, y, ln)
        y += DV_LINE_STEP
    line(d, 0, 32, 127, 32)
    y = 41
    for ln in VERDICT_ADVICE[dev["verdict"]].split("\n"):
        text(d, 2, y, ln)
        y += DV_LINE_STEP
    return img


# ---------------- learn (views/learn_view.c) ----------------

LN_ART_TOP, LN_ART_BOT, LN_CAP1, LN_CAP2 = 13, 44, 53, 62


def learn_chrome(d, title, n, caption):
    text(d, 2, HDR_BASE, title)
    text(d, 126, HDR_BASE, f"{n}/6", anchor="rs")
    line(d, 0, RULE_Y, 127, RULE_Y)
    line(d, 0, LN_ART_BOT + 1, 127, LN_ART_BOT + 1)
    parts = caption.split("\n")
    text(d, 2, LN_CAP1, parts[0])
    if len(parts) > 1:
        text(d, 2, LN_CAP2, parts[1])


def draw_learn_tap(anim=6):
    img, d = canvas()
    learn_chrome(d, "2. THE TAP", 2, "A module is spliced across\nthe reader's data lines.")

    frame(d, 4, LN_ART_TOP + 6, 16, 18)
    box(d, 7, LN_ART_TOP + 10, 10, 4)
    text(d, 3, LN_ART_BOT, "head")

    rframe(d, 92, LN_ART_TOP + 7, 30, 14, 1)
    for i in range(5):
        line(d, 95 + i * 4, LN_ART_TOP + 21, 95 + i * 4, LN_ART_TOP + 23)
    text(d, 92, LN_ART_BOT, "HC-05")

    for w in range(2):
        y = LN_ART_TOP + 11 + w * 6
        line(d, 20, y, 92, y)
        head = 20 + (anim * 3 + w * 12) % 72
        box(d, head, y - 1, 3, 3)
    return img


def draw_learn_sees(anim=0):
    img, d = canvas()
    learn_chrome(d, "5. WHAT WE SEE", 5, "A name, a MAC, a class.\nThat is the whole picture.")

    fields = [("NAME", "HC-05"), ("MAC", "98:D3:31"), ("CLASS", "none")]
    for i, (k, v) in enumerate(fields):
        y = LN_ART_TOP + 1 + i * 10
        lit = (anim // 4) % 3 == i
        col = FG
        if lit:
            box(d, 0, y, 128, 9)
            col = BG
        text(d, 4, y + 7, k, col=col)
        text(d, 52, y + 7, v, col=col)
    line(d, 0, LN_ART_BOT - 1, 127, LN_ART_BOT - 1)
    return img


# ---------------- wiring (views/wiring_view.c) ----------------

WV_COL_BASE, WV_ROW0, WV_ROW_STEP, WV_STATUS_Y = 19, 27, 8, 54
WV_LEFT_X, WV_RIGHT_X = 44, 82


def draw_wiring(online=True):
    img, d = canvas()
    header(d, "WIRING", "USART")
    text(d, 8, WV_COL_BASE, "FLIPPER")
    text(d, WV_RIGHT_X, WV_COL_BASE, "ESP32")

    rows = [("13 TX", "RX0", 1), ("14 RX", "TX0", -1), ("8 GND", "GND", 0), ("1 5V", "5V", 0)]
    for i, (lft, rgt, arrow) in enumerate(rows):
        base = WV_ROW0 + i * WV_ROW_STEP
        text(d, WV_LEFT_X, base, lft, anchor="rs")
        text(d, WV_RIGHT_X, base, rgt)
        y = base - 3
        line(d, WV_LEFT_X + 3, y, WV_RIGHT_X - 3, y)
        if arrow > 0:
            line(d, WV_RIGHT_X - 3, y, WV_RIGHT_X - 6, y - 2)
            line(d, WV_RIGHT_X - 3, y, WV_RIGHT_X - 6, y + 2)
        elif arrow < 0:
            line(d, WV_LEFT_X + 3, y, WV_LEFT_X + 6, y - 2)
            line(d, WV_LEFT_X + 3, y, WV_LEFT_X + 6, y + 2)

    if online:
        footer(d, WV_STATUS_Y, "ONLINE  fw 1.0", "OK")
    else:
        footer(d, WV_STATUS_Y, "NO REPLY  115200 8N1", "--")
    return img


# ---------------- settings ----------------


def draw_settings():
    rows = [
        ("Radios", "BR/EDR+LE"),
        ("Close is", "-70 dBm"),
        ("Port", "TX 13 / RX 14"),
        ("Sound", "On"),
        ("Vibrate", "On"),
    ]
    img, d = canvas()
    y = 0
    for i, (k, v) in enumerate(rows):
        if i == 0:
            box(d, 0, y, 128, 12)
            text(d, 4, y + 9, k, col=BG)
            text(d, 124, y + 9, "< " + v + " >", col=BG, anchor="rs")
        else:
            text(d, 4, y + 9, k)
            text(d, 124, y + 9, v, anchor="rs")
        y += 12
    return img


# ---------------- the sheet ----------------


def contact_sheet(names, cols=3, pad=8):
    ims = [Image.open(os.path.join(OUT, n)) for n in names]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new(
        "RGB", (cols * w + (cols + 1) * pad, rows * h + (rows + 1) * pad), (18, 18, 20)
    )
    for i, im in enumerate(ims):
        r, c = divmod(i, cols)
        sheet.paste(im, (pad + c * (w + pad), pad + r * (h + pad)))
    save(sheet, "screens.png")


# ---------------- overflow check ----------------

OVERFLOWS = []


def check(label, s, max_w, fnt=f_sec):
    if tw(s, fnt) > max_w:
        OVERFLOWS.append(f"{label}: {s!r} is {tw(s, fnt):.0f}px, budget {max_w}px")


def audit():
    """The reason these mockups exist. Anything that would run off the panel
    on the device runs off it here too, and says so."""
    for dev in DEVICES:
        check("list meta", f"{dev['oui']} {dev['rssi']}dBm x{dev['passes']}", 108)
        check("detail mac", dev["mac"], 124)
        check("sweep meta", f"{dev['rssi']}dBm  x{dev['passes']}", SV_COL_W)
        check("sweep headline", HEADLINE[dev["verdict"]], SV_COL_W, f_pri)
        if dev["cap"]:
            check("why footer", f"held: {dev['cap']}", 104)
        for sig in dev["signals"]:
            label = sig["short"]
            if label == "Module name" and dev["module_note"]:
                label = dev["module_note"]
            check("why row", label, 96)
    for v in VERDICT_LINE:
        for ln in VERDICT_LINE[v].split("\n") + VERDICT_ADVICE[v].split("\n"):
            check("detail text", ln, 126)


if __name__ == "__main__":
    skimmer = device("HC-05")
    suspect = device("HMSoft")
    beacon = device("")

    names = []
    names.append(save(draw_splash(), "screen_splash.png"))
    names.append(save(draw_menu(), "screen_menu.png"))
    names.append(
        save(
            draw_sweep(None, 1, 2, 0, scan_col=8, listening=True),
            "screen_sweep_listening.png",
        )
    )
    names.append(save(draw_sweep(suspect, 4, 6, 2, scan_col=31), "screen_sweep_suspect.png"))
    names.append(
        save(
            draw_sweep(skimmer, 8, len(DEVICES), flagged_count(), scanning=False),
            "screen_sweep_skimmer.png",
        )
    )
    names.append(save(draw_list(DEVICES[:3], selected=0), "screen_list.png"))
    names.append(save(draw_detail_device(skimmer), "screen_detail_device.png"))
    names.append(save(draw_detail_why(skimmer), "screen_detail_why.png"))
    names.append(save(draw_detail_means(skimmer), "screen_detail_means.png"))
    names.append(save(draw_detail_why(suspect), "screen_detail_why_capped.png"))
    names.append(save(draw_learn_tap(), "screen_learn_tap.png"))
    names.append(save(draw_learn_sees(), "screen_learn_sees.png"))
    names.append(save(draw_wiring(), "screen_wiring.png"))
    names.append(save(draw_settings(), "screen_settings.png"))
    save(draw_sweep(beacon, 6, 7, 3, scan_col=44), "screen_sweep_note.png")

    contact_sheet(names[1:], cols=3)

    audit()
    if OVERFLOWS:
        print("\nLAYOUT OVERFLOW:")
        for o in OVERFLOWS:
            print("  " + o)
        sys.exit(1)
    print("\nlayout audit clean")
