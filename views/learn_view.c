/* How a Bluetooth skimmer actually works, in six drawn panels.
 *
 * This exists because the app is only worth trusting if you understand what
 * it is looking for. Six screens is enough to explain the whole attack: where
 * the module goes, why it is a radio at all, why nobody comes back for it,
 * and -- the important one -- exactly how little an inquiry response tells
 * you, which is the reason the scores are capped the way they are.
 *
 * Left/Right walks the panels. Each one animates on the tick.
 */
#include "learn_view.h"
#include "card_art.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define LV_HDR_BASE 9
#define LV_RULE_Y 11
#define LV_ART_TOP 13
#define LV_ART_BOT 44
#define LV_CAP1 53
#define LV_CAP2 62

struct LearnView {
    View* view;
};

typedef struct {
    uint8_t panel;
    uint32_t anim;
} LearnModel;

static const char* const titles[LEARN_PANELS] = {
    "1. THE SWIPE",
    "2. THE TAP",
    "3. THE RADIO",
    "4. THE PICKUP",
    "5. WHAT WE SEE",
    "6. WHAT TO DO",
};

/* Two lines each, held under the screen width by the engine's own line rule. */
static const char* const captions[LEARN_PANELS] = {
    "You swipe. The reader\nreads the magnetic stripe.",
    "A module is spliced across\nthe reader's data lines.",
    "It is a Bluetooth serial\nbridge. HC-05. Two dollars.",
    "Nobody comes back for it.\nThey collect from a car.",
    "A name, a MAC, a class.\nThat is the whole picture.",
    "Tap or chip. Cover the PIN.\nTell staff. Open nothing.",
};

static void lv_caption(Canvas* canvas, const char* text) {
    char line[32];
    int y = LV_CAP1;
    while(text && *text) {
        const char* nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : strlen(text);
        if(n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, text, n);
        line[n] = '\0';
        canvas_draw_str(canvas, 2, y, line);
        y = LV_CAP2;
        if(!nl) break;
        text = nl + 1;
    }
}

/* ------------------------------------------------------------------ *
 * 1. The swipe: a pump, and a card going into it.
 * ------------------------------------------------------------------ */
static void lv_panel_swipe(Canvas* canvas, uint32_t anim) {
    /* the pump */
    canvas_draw_rframe(canvas, 8, LV_ART_TOP + 1, 40, 30, 2);
    canvas_draw_frame(canvas, 12, LV_ART_TOP + 4, 32, 10); /* display */
    canvas_draw_box(canvas, 12, LV_ART_TOP + 19, 32, 3); /* card slot */
    for(int r = 0; r < 2; r++) /* keypad */
        for(int c = 0; c < 4; c++)
            canvas_draw_dot(canvas, 14 + c * 5, LV_ART_TOP + 25 + r * 3);

    /* the card, sliding in on a loop */
    int span = 40;
    int x = 108 - (int)(anim % (uint32_t)span);
    canvas_draw_rframe(canvas, x, LV_ART_TOP + 15, 22, 12, 2);
    canvas_draw_box(canvas, x + 2, LV_ART_TOP + 17, 18, 3);

    /* the stripe being read */
    canvas_draw_line(canvas, 50, LV_ART_TOP + 20, x - 2, LV_ART_TOP + 20);
}

/* ------------------------------------------------------------------ *
 * 2. The tap: the module bridged across the read head's wires.
 * ------------------------------------------------------------------ */
static void lv_panel_tap(Canvas* canvas, uint32_t anim) {
    /* read head on the left */
    canvas_draw_frame(canvas, 4, LV_ART_TOP + 6, 16, 18);
    canvas_draw_box(canvas, 7, LV_ART_TOP + 10, 10, 4);
    canvas_draw_str(canvas, 3, LV_ART_BOT, "head");

    /* the module on the right. The body stops at +21 so the pin header has
     * somewhere to go that is not on top of the label. */
    canvas_draw_rframe(canvas, 92, LV_ART_TOP + 7, 30, 14, 1);
    for(int i = 0; i < 5; i++) { /* pin header */
        canvas_draw_line(canvas, 95 + i * 4, LV_ART_TOP + 21, 95 + i * 4, LV_ART_TOP + 23);
    }
    canvas_draw_str(canvas, 92, LV_ART_BOT, "HC-05");

    /* two wires, with data crawling along them */
    for(int w = 0; w < 2; w++) {
        int y = LV_ART_TOP + 11 + w * 6;
        canvas_draw_line(canvas, 20, y, 92, y);
        int head = 20 + (int)((anim * 3 + (uint32_t)w * 12) % 72);
        canvas_draw_box(canvas, head, y - 1, 3, 3);
    }
}

/* ------------------------------------------------------------------ *
 * 3. The radio: it is just a serial cable that happens to be Bluetooth.
 * ------------------------------------------------------------------ */
static void lv_panel_radio(Canvas* canvas, uint32_t anim) {
    canvas_draw_rframe(canvas, 8, LV_ART_TOP + 8, 34, 16, 1);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 12, LV_ART_TOP + 19, "HC-05");

    /* radiating arcs, built from lines: canvas_draw_arc does not exist */
    for(int k = 1; k <= 4; k++) {
        int r = k * 9;
        int cx = 44, cy = LV_ART_TOP + 16;
        if((int)((anim / 3) % 5) == k) continue; /* one gap, travelling outward */
        canvas_draw_line(canvas, cx + r - 3, cy - r + 4, cx + r, cy - 1);
        canvas_draw_line(canvas, cx + r, cy - 1, cx + r, cy + 1);
        canvas_draw_line(canvas, cx + r, cy + 1, cx + r - 3, cy + r - 4);
    }
}

/* ------------------------------------------------------------------ *
 * 4. The pickup: the reason it is a radio at all.
 * ------------------------------------------------------------------ */
static void lv_panel_pickup(Canvas* canvas, uint32_t anim) {
    /* the pump, small, on the left */
    canvas_draw_rframe(canvas, 4, LV_ART_TOP + 4, 18, 24, 2);
    canvas_draw_box(canvas, 7, LV_ART_TOP + 16, 12, 2);

    /* a car in the lot */
    canvas_draw_line(canvas, 88, LV_ART_TOP + 24, 122, LV_ART_TOP + 24);
    canvas_draw_line(canvas, 90, LV_ART_TOP + 24, 93, LV_ART_TOP + 17);
    canvas_draw_line(canvas, 93, LV_ART_TOP + 17, 112, LV_ART_TOP + 17);
    canvas_draw_line(canvas, 112, LV_ART_TOP + 17, 118, LV_ART_TOP + 24);
    canvas_draw_circle(canvas, 96, LV_ART_TOP + 26, 2);
    canvas_draw_circle(canvas, 113, LV_ART_TOP + 26, 2);

    /* the dump crossing the forecourt */
    int span = 60;
    int x = 26 + (int)(anim % (uint32_t)span);
    canvas_draw_line(canvas, 24, LV_ART_TOP + 12, 86, LV_ART_TOP + 12);
    canvas_draw_box(canvas, x, LV_ART_TOP + 10, 4, 4);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 40, LV_ART_TOP + 8, "your PAN");
}

/* ------------------------------------------------------------------ *
 * 5. What an inquiry response actually contains. This is the panel the
 *    whole scoring design rests on.
 * ------------------------------------------------------------------ */
static void lv_panel_sees(Canvas* canvas, uint32_t anim) {
    static const char* const fields[3] = {"NAME", "MAC", "CLASS"};
    static const char* const values[3] = {"HC-05", "98:D3:31", "none"};

    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < 3; i++) {
        int y = LV_ART_TOP + 1 + i * 10;
        bool lit = ((anim / 4) % 3) == (uint32_t)i;
        if(lit) {
            canvas_draw_box(canvas, 0, y, 128, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y + 7, fields[i]);
        canvas_draw_str(canvas, 52, y + 7, values[i]);
        if(lit) canvas_set_color(canvas, ColorBlack);
    }
    canvas_draw_line(canvas, 0, LV_ART_BOT - 1, 127, LV_ART_BOT - 1);
}

/* ------------------------------------------------------------------ *
 * 6. What to do: use the chip.
 * ------------------------------------------------------------------ */
static void lv_panel_do(Canvas* canvas, uint32_t anim) {
    card_art_draw(canvas, 12, LV_ART_TOP + 2, false);

    /* the chip, ringed -- the part a stripe skimmer cannot copy */
    int r = 6 + (int)((anim / 2) % 3);
    canvas_draw_circle(canvas, 12 + 10, LV_ART_TOP + 2 + 16, r);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 72, LV_ART_TOP + 12, "chip");
    canvas_draw_str(canvas, 72, LV_ART_TOP + 22, "or tap");
    canvas_draw_line(canvas, 66, LV_ART_TOP + 9, 70, LV_ART_TOP + 9);
    canvas_draw_line(canvas, 34, LV_ART_TOP + 18, 66, LV_ART_TOP + 9);
}

/* ------------------------------------------------------------------ */

static void learn_view_draw(Canvas* canvas, void* model) {
    LearnModel* m = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, LV_HDR_BASE, titles[m->panel % LEARN_PANELS]);
    char buf[12];
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(m->panel + 1), (unsigned)LEARN_PANELS);
    canvas_draw_str_aligned(canvas, 126, LV_HDR_BASE, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, LV_RULE_Y, 127, LV_RULE_Y);

    switch(m->panel) {
    case 0:
        lv_panel_swipe(canvas, m->anim);
        break;
    case 1:
        lv_panel_tap(canvas, m->anim);
        break;
    case 2:
        lv_panel_radio(canvas, m->anim);
        break;
    case 3:
        lv_panel_pickup(canvas, m->anim);
        break;
    case 4:
        lv_panel_sees(canvas, m->anim);
        break;
    default:
        lv_panel_do(canvas, m->anim);
        break;
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, LV_ART_BOT + 1, 127, LV_ART_BOT + 1);
    lv_caption(canvas, captions[m->panel % LEARN_PANELS]);
}

static bool learn_view_input(InputEvent* event, void* context) {
    LearnView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key != InputKeyLeft && event->key != InputKeyRight) return false;

    with_view_model(
        v->view,
        LearnModel * m,
        {
            if(event->key == InputKeyRight) {
                m->panel = (uint8_t)((m->panel + 1) % LEARN_PANELS);
            } else {
                m->panel = (uint8_t)((m->panel + LEARN_PANELS - 1) % LEARN_PANELS);
            }
            m->anim = 0;
        },
        true);
    return true;
}

LearnView* learn_view_alloc(void) {
    LearnView* v = malloc(sizeof(LearnView));
    memset(v, 0, sizeof(LearnView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, learn_view_draw);
    view_set_input_callback(v->view, learn_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(LearnModel));
    return v;
}

void learn_view_free(LearnView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* learn_view_get_view(LearnView* v) {
    furi_assert(v);
    return v->view;
}

void learn_view_tick(LearnView* v) {
    furi_assert(v);
    with_view_model(v->view, LearnModel * m, { m->anim++; }, true);
}

void learn_view_reset(LearnView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        LearnModel * m,
        {
            m->panel = 0;
            m->anim = 0;
        },
        true);
}
