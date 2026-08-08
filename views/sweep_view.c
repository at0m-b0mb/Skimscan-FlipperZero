/* The sweep screen.
 *
 * This is the one screen the app is really for, and it gets looked at for
 * about four seconds at arm's length with a card already in hand. So it
 * answers one question in one word, shows the card being scanned so the state
 * is obvious without reading anything, and puts the evidence behind OK.
 *
 * Layout constants are load-bearing: tools_gen_mockups.py mirrors them, which
 * is how collisions get caught before they ship.
 */
#include "sweep_view.h"
#include "card_art.h"

#include "skimscan_icons.h" /* generated from icons/ by fbt */

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define SV_HDR_BASE 9
#define SV_RULE_Y 11

#define SV_CARD_X 2
#define SV_CARD_Y 14

#define SV_COL_X 56
#define SV_COL_CX 92
#define SV_COL_W 72
#define SV_WORD_CY 23
#define SV_NAME_BASE 35
#define SV_META_BASE 43

#define SV_BAR_X 2
#define SV_BAR_Y 45
#define SV_BAR_W 124
#define SV_BAR_H 7

#define SV_FOOTER_Y 53
#define SV_FOOTER_BASE 62

/* The scan bar takes this many ticks to cross the card. At the scene's 100 ms
 * tick that is a shade under two seconds a pass -- unhurried, so it reads as
 * an instrument working rather than a progress bar failing. */
#define SV_SCAN_STEPS 18

struct SweepView {
    View* view;
    SweepViewCallback open_cb;
    void* open_ctx;
    SweepViewCallback reset_cb;
    void* reset_ctx;
};

typedef struct {
    SkimSweepSnapshot s;
    uint32_t anim;
} SweepModel;

/* ------------------------------------------------------------------ */

static const char* mode_short(uint8_t mode) {
    switch(mode) {
    case 1:
        return "BR/EDR";
    case 2:
        return "LE";
    default:
        return "BR+LE";
    }
}

/* The score bar, with the band edges marked. The ticks are what turn a number
 * into a scale you can read without being told what 62 means. */
static void sv_draw_bar(Canvas* canvas, uint8_t score) {
    canvas_draw_frame(canvas, SV_BAR_X, SV_BAR_Y, SV_BAR_W, SV_BAR_H);

    const int inner_x = SV_BAR_X + 1;
    const int inner_w = SV_BAR_W - 2;
    int fill = (score * inner_w) / 100;
    if(fill > 0) canvas_draw_box(canvas, inner_x, SV_BAR_Y + 1, (size_t)fill, SV_BAR_H - 2);

    static const SkimVerdict bands[] = {SkimVerdictNote, SkimVerdictSuspect, SkimVerdictLikely};
    for(size_t i = 0; i < sizeof(bands) / sizeof(bands[0]); i++) {
        int tick = inner_x + (skim_verdict_floor(bands[i]) * inner_w) / 100;
        /* Knocked out of the fill where it is covered, drawn on the empty
         * track where it is not. */
        canvas_set_color(canvas, (tick < inner_x + fill) ? ColorWhite : ColorBlack);
        canvas_draw_line(canvas, tick, SV_BAR_Y + 1, tick, SV_BAR_Y + SV_BAR_H - 2);
    }
    canvas_set_color(canvas, ColorBlack);
}

/* The right-hand column: one word, then who and how loud. */
static void sv_draw_verdict(Canvas* canvas, const SweepModel* m) {
    const SkimSweepSnapshot* s = &m->s;
    char buf[24];

    const char* word;
    const char* name;
    const char* meta = NULL;

    if(!s->link_online && !s->demo) {
        word = "NO LINK";
        name = "companion silent";
        meta = "See Wiring";
    } else if(!s->have_worst) {
        word = skim_sweep_headline(SkimVerdictClear, false);
        static const char* const dots[] = {"listening", "listening.", "listening..", "listening..."};
        name = dots[(m->anim / 4) % 4];
    } else {
        word = skim_sweep_headline(s->worst.assess.verdict, true);
        name = s->worst.last.name[0] ? s->worst.last.name : "(no name)";
        snprintf(
            buf,
            sizeof(buf),
            "%ddBm  x%u",
            (int)s->worst.best_rssi,
            (unsigned)(s->worst.passes > 99 ? 99 : s->worst.passes));
        meta = buf;
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SV_COL_CX, SV_WORD_CY, AlignCenter, AlignCenter, word);

    /* Anything at SUSPECT or above gets boxed, so it carries at a glance and
     * from an angle. Below that a box would be crying wolf. */
    if(s->have_worst && s->worst.assess.verdict >= SkimVerdictSuspect) {
        int w = canvas_string_width(canvas, word);
        canvas_draw_frame(canvas, SV_COL_CX - w / 2 - 3, SV_WORD_CY - 7, (size_t)(w + 6), 14);
    }

    /* The column is 72 px wide, and plenty of Bluetooth names are longer than
     * that. Clip on a character boundary rather than mid-glyph. */
    canvas_set_font(canvas, FontSecondary);
    char clipped[SKIM_NAME_MAX + 3];
    snprintf(clipped, sizeof(clipped), "%s", name);
    if(canvas_string_width(canvas, clipped) > SV_COL_W) {
        size_t len = strlen(clipped);
        while(len > 1 && canvas_string_width(canvas, clipped) > SV_COL_W - 6) clipped[--len] = '\0';
        snprintf(clipped + len, sizeof(clipped) - len, "..");
    }
    canvas_draw_str_aligned(canvas, SV_COL_CX, SV_NAME_BASE, AlignCenter, AlignBottom, clipped);
    if(meta) canvas_draw_str_aligned(canvas, SV_COL_CX, SV_META_BASE, AlignCenter, AlignBottom, meta);
}

static void sweep_view_draw(Canvas* canvas, void* model) {
    SweepModel* m = model;
    const SkimSweepSnapshot* s = &m->s;
    char buf[24];

    canvas_clear(canvas);

    /* --- header --- */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_icon(canvas, 1, 1, &I_card_10px);
    canvas_draw_str(canvas, 14, SV_HDR_BASE, s->demo ? "SWEEP (demo)" : "SWEEP");
    snprintf(buf, sizeof(buf), "P%u %s", (unsigned)(s->pass > 999 ? 999 : s->pass), mode_short(s->mode));
    canvas_draw_str_aligned(canvas, 126, SV_HDR_BASE, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, SV_RULE_Y, 127, SV_RULE_Y);

    /* --- the card --- */
    bool alarm = s->have_worst && s->worst.assess.verdict >= SkimVerdictLikely;
    card_art_draw(canvas, SV_CARD_X, SV_CARD_Y, alarm);

    /* The bar sweeps while the companion is inquiring. When the verdict is at
     * the top it stops: the scan is over, look at the screen. */
    if(s->scanning && !alarm) {
        card_art_scanline(
            canvas, SV_CARD_X, SV_CARD_Y, (int)((m->anim % SV_SCAN_STEPS) * CARD_W / SV_SCAN_STEPS));
    }

    /* --- verdict --- */
    sv_draw_verdict(canvas, m);

    /* --- score bar --- */
    sv_draw_bar(canvas, s->have_worst ? s->worst.assess.score : 0);

    /* --- footer --- */
    canvas_draw_box(canvas, 0, SV_FOOTER_Y, 128, 64 - SV_FOOTER_Y);
    canvas_set_color(canvas, ColorWhite);
    snprintf(
        buf,
        sizeof(buf),
        "%u dev  %u flagged",
        (unsigned)(s->seen > 99 ? 99 : s->seen),
        (unsigned)(s->flagged > 99 ? 99 : s->flagged));
    canvas_draw_str(canvas, 3, SV_FOOTER_BASE, buf);
    canvas_draw_str_aligned(canvas, 125, SV_FOOTER_BASE, AlignRight, AlignBottom, "OK");
    canvas_set_color(canvas, ColorBlack);
}

static bool sweep_view_input(InputEvent* event, void* context) {
    SweepView* v = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(v->open_cb) v->open_cb(v->open_ctx);
        return true;
    }
    if(event->type == InputTypeLong && event->key == InputKeyOk) {
        if(v->reset_cb) v->reset_cb(v->reset_ctx);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */

SweepView* sweep_view_alloc(void) {
    SweepView* v = malloc(sizeof(SweepView));
    memset(v, 0, sizeof(SweepView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, sweep_view_draw);
    view_set_input_callback(v->view, sweep_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SweepModel));
    return v;
}

void sweep_view_free(SweepView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* sweep_view_get_view(SweepView* v) {
    furi_assert(v);
    return v->view;
}

void sweep_view_update(SweepView* v, const SkimSweepSnapshot* snap) {
    furi_assert(v);
    furi_assert(snap);
    with_view_model(v->view, SweepModel * m, { m->s = *snap; }, true);
}

void sweep_view_tick(SweepView* v) {
    furi_assert(v);
    with_view_model(v->view, SweepModel * m, { m->anim++; }, true);
}

void sweep_view_set_open_callback(SweepView* v, SweepViewCallback cb, void* context) {
    furi_assert(v);
    v->open_cb = cb;
    v->open_ctx = context;
}

void sweep_view_set_reset_callback(SweepView* v, SweepViewCallback cb, void* context) {
    furi_assert(v);
    v->reset_cb = cb;
    v->reset_ctx = context;
}
