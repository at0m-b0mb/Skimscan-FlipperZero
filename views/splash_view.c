/* The card goes into the reader, the reader scans it, and the app says what
 * it is. Under two seconds, and any key skips it. */
#include "splash_view.h"
#include "card_art.h"

#include <furi.h>
#include <gui/gui.h>
#include <string.h>

#define SP_CARD_X 39
#define SP_CARD_Y 9
#define SP_SLIDE 7 /* ticks for the card to arrive */
#define SP_TOTAL 17 /* ticks before the scene moves on by itself */
#define SP_TITLE_BASE 51
#define SP_TAG_BASE 61

struct SplashView {
    View* view;
    SplashViewCallback skip_cb;
    void* skip_ctx;
};

typedef struct {
    uint8_t anim;
} SplashModel;

static void splash_view_draw(Canvas* canvas, void* model) {
    SplashModel* m = model;
    canvas_clear(canvas);

    /* Slide in from the right, decelerating -- the card is being pushed into
     * a slot, not fired at one. */
    int x = SP_CARD_X;
    if(m->anim < SP_SLIDE) {
        int remain = SP_SLIDE - m->anim;
        x = SP_CARD_X + (140 - SP_CARD_X) * remain * remain / (SP_SLIDE * SP_SLIDE);
    }

    /* The reader slot the card is going into. */
    canvas_draw_line(canvas, 30, SP_CARD_Y - 3, 30, SP_CARD_Y + CARD_H + 2);
    canvas_draw_line(canvas, 30, SP_CARD_Y - 3, 36, SP_CARD_Y - 3);
    canvas_draw_line(canvas, 30, SP_CARD_Y + CARD_H + 2, 36, SP_CARD_Y + CARD_H + 2);

    card_art_draw(canvas, x, SP_CARD_Y, false);

    if(m->anim >= SP_SLIDE) {
        int span = SP_TOTAL - SP_SLIDE;
        int col = (int)(m->anim - SP_SLIDE) * CARD_W / (span > 0 ? span : 1);
        card_art_scanline(canvas, SP_CARD_X, SP_CARD_Y, col);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, 64, SP_TITLE_BASE, AlignCenter, AlignBottom, "SKIMSCAN");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, SP_TAG_BASE, AlignCenter, AlignBottom, "Check before you swipe");
    }
}

static bool splash_view_input(InputEvent* event, void* context) {
    SplashView* v = context;
    if(event->type == InputTypeShort || event->type == InputTypePress) {
        if(v->skip_cb) v->skip_cb(v->skip_ctx);
        return true;
    }
    return false;
}

SplashView* splash_view_alloc(void) {
    SplashView* v = malloc(sizeof(SplashView));
    memset(v, 0, sizeof(SplashView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, splash_view_draw);
    view_set_input_callback(v->view, splash_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SplashModel));
    return v;
}

void splash_view_free(SplashView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* splash_view_get_view(SplashView* v) {
    furi_assert(v);
    return v->view;
}

bool splash_view_tick(SplashView* v) {
    furi_assert(v);
    bool done = false;
    with_view_model(
        v->view,
        SplashModel * m,
        {
            if(m->anim < SP_TOTAL) m->anim++;
            done = m->anim >= SP_TOTAL;
        },
        true);
    return done;
}

void splash_view_set_skip_callback(SplashView* v, SplashViewCallback cb, void* context) {
    furi_assert(v);
    v->skip_cb = cb;
    v->skip_ctx = context;
}
