#!/usr/bin/env python3
"""Generate 1-bit 10x10 Flipper icons for Skimscan from ASCII bitmaps.

'#' = foreground (black / on), anything else = background (white / off).
fbt thresholds PNGs to 1-bit, where dark pixels become 'on'.
"""
from PIL import Image
import os

OUT = os.path.join(os.path.dirname(__file__), "icons")
os.makedirs(OUT, exist_ok=True)

GLYPHS = {
    # App mark: a card with its magstripe, crossed by a scan bar. The whole
    # product in ten pixels -- something reading a card that should not be.
    "skimscan_10px": [
        "..........",
        ".########.",
        ".#..#...#.",
        ".####...#.",
        ".#..#...#.",
        ".#..#.###.",
        ".#..#...#.",
        ".########.",
        "....#.....",
        "..........",
    ],
    # A card, for the sweep header. Magstripe across the top, chip below.
    "card_10px": [
        "..........",
        "##########",
        "#........#",
        "#........#",
        "#.######.#",
        "#........#",
        "#.##.....#",
        "#.##.....#",
        "##########",
        "..........",
    ],
    # Bluetooth: the rune, as close as ten pixels allows.
    "bt_10px": [
        "....#.....",
        "....##....",
        "..#.#.#...",
        "...###....",
        "....#.....",
        "....#.....",
        "...###....",
        "..#.#.#...",
        "....##....",
        "....#.....",
    ],
    # Warning, for a flagged device in a list.
    "warn_10px": [
        "....#.....",
        "....#.....",
        "...###....",
        "...#.#....",
        "..##.##...",
        "..#.#.#...",
        ".##...##..",
        ".#..#..#..",
        "##########",
        "..........",
    ],
}


def render(name, rows):
    img = Image.new("1", (10, 10), 1)  # 1 = white background
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:10]):
            if ch == "#":
                img.putpixel((x, y), 0)  # 0 = black foreground
    path = os.path.join(OUT, name + ".png")
    img.save(path)
    return path


if __name__ == "__main__":
    for name, rows in GLYPHS.items():
        assert len(rows) == 10, f"{name} must have 10 rows"
        for r in rows:
            assert len(r) == 10, f"{name} row not 10 wide: {r!r}"
        print("wrote", render(name, rows))
