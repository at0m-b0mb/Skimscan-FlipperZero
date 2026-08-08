/* The card, drawn once and reused.
 *
 * It shows up on the splash, on the sweep screen and in the walkthrough, and
 * the three have to be the same object or the app stops looking like one
 * thing. 50 x 28, anchored at its top-left corner.
 */
#pragma once

#include <gui/gui.h>

#define CARD_W 50
#define CARD_H 28

/** Magstripe, chip and embossed digits. `inverted` fills the body and knocks
 *  the detail out in white -- that is the alarm state, not a style. */
void card_art_draw(Canvas* canvas, int x, int y, bool inverted);

/** A reader's scan bar crossing the card at column `col` (0..CARD_W-1),
 *  drawn XOR so it inverts whatever it passes over. */
void card_art_scanline(Canvas* canvas, int x, int y, int col);
