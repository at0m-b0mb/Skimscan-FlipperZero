/* Everything the sweep heard, worst first.
 *
 * Two lines per device, because one is not enough to be fair to it: the name
 * on top, and underneath the three facts that decide whether the name means
 * anything -- who the MAC belongs to, how loud it is, and whether it is still
 * there. A device that has stopped answering says so, which is how a customer
 * driving off stops looking like a fixture.
 */
#include "list_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define LV_HDR_BASE 9
#define LV_RULE_Y 11
#define LV_ROW_TOP 12
#define LV_ROW_H 17
#define LV_NAME_BASE 9 /* within the row */
#define LV_META_BASE 16 /* within the row */
#define LV_TEXT_W 108 /* leaves the score column and the scrollbar alone */

struct ListView {
    View* view;
    ListViewMoveCallback move_cb;
    void* move_ctx;
    ListViewCallback open_cb;
    void* open_ctx;
};

typedef struct {
    SkimListSnapshot s;
} ListModel;

static void lv_draw_row(Canvas* canvas, int y, const SkimDevice* d, uint16_t stale, bool selected) {
    char buf[40];

    if(selected) {
        /* The full row height: at LV_ROW_H - 1 the meta line's baseline sits
         * on the last covered pixel and its descenders fall outside the
         * inverted box, which reads as the text being cut off. */
        canvas_draw_box(canvas, 0, y, 124, LV_ROW_H);
        canvas_set_color(canvas, ColorWhite);
    }

    /* --- name and score --- */
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%s", d->last.name[0] ? d->last.name : "(no name)");
    /* Clip by hand rather than by pixel: a name cut mid-glyph looks broken. */
    if(canvas_string_width(canvas, buf) > LV_TEXT_W) {
        size_t len = strlen(buf);
        while(len > 3 && canvas_string_width(canvas, buf) > LV_TEXT_W - 6) {
            buf[--len] = '\0';
        }
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "..");
    }
    canvas_draw_str(canvas, 2, y + LV_NAME_BASE, buf);

    snprintf(buf, sizeof(buf), "%u", (unsigned)d->assess.score);
    canvas_draw_str_aligned(canvas, 122, y + LV_NAME_BASE, AlignRight, AlignBottom, buf);

    /* --- the three facts underneath --- */
    char oui[12];
    skim_oui_str(d->last.mac, oui, sizeof(oui));
    if(stale > 0) {
        snprintf(
            buf,
            sizeof(buf),
            "%s %ddBm gone %u",
            oui,
            (int)d->best_rssi,
            (unsigned)(stale > 99 ? 99 : stale));
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "%s %ddBm x%u",
            oui,
            (int)d->best_rssi,
            (unsigned)(d->passes > 99 ? 99 : d->passes));
    }
    canvas_draw_str(canvas, 2, y + LV_META_BASE, buf);

    canvas_draw_str_aligned(
        canvas,
        122,
        y + LV_META_BASE,
        AlignRight,
        AlignBottom,
        (d->last.radio == SkimRadioLe) ? "LE" : "BR");

    if(selected) canvas_set_color(canvas, ColorBlack);
}

static void list_view_draw(Canvas* canvas, void* model) {
    ListModel* m = model;
    const SkimListSnapshot* s = &m->s;
    char buf[24];

    canvas_clear(canvas);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, LV_HDR_BASE, "DEVICES");
    if(s->total > 0) {
        snprintf(
            buf, sizeof(buf), "%u of %u", (unsigned)(s->selected + 1), (unsigned)s->total);
        canvas_draw_str_aligned(canvas, 126, LV_HDR_BASE, AlignRight, AlignBottom, buf);
    }
    canvas_draw_line(canvas, 0, LV_RULE_Y, 127, LV_RULE_Y);

    if(s->total == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Nothing heard yet.");
        canvas_draw_str_aligned(
            canvas, 64, 42, AlignCenter, AlignCenter, "Go back and let it sweep.");
        return;
    }

    for(uint8_t i = 0; i < s->visible && i < LIST_ROWS; i++) {
        int y = LV_ROW_TOP + i * LV_ROW_H;
        lv_draw_row(
            canvas, y, &s->row[i], s->stale[i], (uint16_t)(s->top + i) == s->selected);
    }

    if(s->total > LIST_ROWS) {
        elements_scrollbar_pos(canvas, 126, LV_ROW_TOP, LV_ROW_H * LIST_ROWS, s->selected, s->total);
    }
}

static bool list_view_input(InputEvent* event, void* context) {
    ListView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyUp:
        if(v->move_cb) v->move_cb(v->move_ctx, -1);
        return true;
    case InputKeyDown:
        if(v->move_cb) v->move_cb(v->move_ctx, 1);
        return true;
    case InputKeyOk:
        if(event->type == InputTypeShort && v->open_cb) v->open_cb(v->open_ctx);
        return true;
    default:
        return false;
    }
}

ListView* list_view_alloc(void) {
    ListView* v = malloc(sizeof(ListView));
    memset(v, 0, sizeof(ListView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, list_view_draw);
    view_set_input_callback(v->view, list_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ListModel));
    return v;
}

void list_view_free(ListView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* list_view_get_view(ListView* v) {
    furi_assert(v);
    return v->view;
}

void list_view_update(ListView* v, const SkimListSnapshot* snap) {
    furi_assert(v);
    furi_assert(snap);
    with_view_model(v->view, ListModel * m, { m->s = *snap; }, true);
}

void list_view_set_move_callback(ListView* v, ListViewMoveCallback cb, void* context) {
    furi_assert(v);
    v->move_cb = cb;
    v->move_ctx = context;
}

void list_view_set_open_callback(ListView* v, ListViewCallback cb, void* context) {
    furi_assert(v);
    v->open_cb = cb;
    v->open_ctx = context;
}
