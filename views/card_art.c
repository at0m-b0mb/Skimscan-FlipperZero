#include "card_art.h"

/* Geometry, relative to the card's top-left. Kept as named offsets because
 * tools_gen_mockups.py mirrors every one of them. */
#define STRIPE_X 3
#define STRIPE_Y 4
#define STRIPE_W 44
#define STRIPE_H 5

#define CHIP_X 5
#define CHIP_Y 12
#define CHIP_W 10
#define CHIP_H 8

#define DIGIT_Y 23
#define DIGIT_GROUPS 4
#define DIGIT_PER_GROUP 4

void card_art_draw(Canvas* canvas, int x, int y, bool inverted) {
    if(inverted) {
        canvas_draw_rbox(canvas, x, y, CARD_W, CARD_H, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, CARD_W, CARD_H, 3);
    }

    /* The magstripe: the thing a skimmer is actually reading. */
    canvas_draw_box(canvas, x + STRIPE_X, y + STRIPE_Y, STRIPE_W, STRIPE_H);

    /* The chip, which is the reason a skimmer wants the stripe instead. */
    canvas_draw_frame(canvas, x + CHIP_X, y + CHIP_Y, CHIP_W, CHIP_H);
    canvas_draw_line(
        canvas, x + CHIP_X + 2, y + CHIP_Y + 3, x + CHIP_X + CHIP_W - 3, y + CHIP_Y + 3);
    canvas_draw_line(
        canvas, x + CHIP_X + CHIP_W / 2, y + CHIP_Y, x + CHIP_X + CHIP_W / 2, y + CHIP_Y + CHIP_H - 1);

    /* Embossed digits, as dots -- at this size anything more is mud. */
    for(int g = 0; g < DIGIT_GROUPS; g++) {
        int gx = x + 5 + g * 12;
        for(int d = 0; d < DIGIT_PER_GROUP; d++) {
            canvas_draw_dot(canvas, gx + d * 2, y + DIGIT_Y);
        }
    }

    if(inverted) canvas_set_color(canvas, ColorBlack);
}

void card_art_scanline(Canvas* canvas, int x, int y, int col) {
    if(col < 0) col = 0;
    if(col >= CARD_W) col = CARD_W - 1;

    canvas_set_color(canvas, ColorXOR);
    canvas_draw_line(canvas, x + col, y, x + col, y + CARD_H - 1);
    /* A soft edge either side, so the bar reads as a sweep rather than as a
     * scratch on the artwork. */
    for(int dy = y; dy < y + CARD_H; dy += 2) {
        if(col > 0) canvas_draw_dot(canvas, x + col - 1, dy);
        if(col < CARD_W - 1) canvas_draw_dot(canvas, x + col + 1, dy + 1);
    }
    canvas_set_color(canvas, ColorBlack);
}
