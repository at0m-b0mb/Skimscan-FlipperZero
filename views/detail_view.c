/* One device, taken apart.
 *
 * This is where the number on the sweep screen has to justify itself. Three
 * pages, cycled with Left/Right:
 *
 *   DEVICE    the facts, with nothing inferred
 *   WHY       every signal that fired, what it was worth, and the cap
 *   WHAT NOW  what it means, and what to actually do
 *
 * A tool that grades things without showing its working is asking to be
 * believed. This one shows the working.
 */
#include "detail_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define DV_HDR_BASE 9
#define DV_RULE_Y 11
#define DV_LINE_STEP 9
#define DV_BODY_BASE 21

#define DV_FOOTER_Y 54
#define DV_FOOTER_BASE 62

/* At most five signals can fire at once, and the whole point of this page is
 * that none of them are hidden -- so all five get a row, on a tighter step
 * than the DEVICE page uses. */
#define DV_WHY_ROWS 5
#define DV_WHY_BASE 19
#define DV_WHY_STEP 8

struct DetailView {
    View* view;
};

typedef struct {
    SkimDetailSnapshot s;
    uint8_t page;
    uint8_t scroll; /* page 2 only */
} DetailModel;

/* ------------------------------------------------------------------ */

static void dv_draw_lines(Canvas* canvas, int x, int base, int step, const char* text) {
    char line[32];
    int y = base;
    while(text && *text) {
        const char* nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : strlen(text);
        if(n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, text, n);
        line[n] = '\0';
        canvas_draw_str(canvas, x, y, line);
        y += step;
        if(!nl) break;
        text = nl + 1;
    }
}

/* Three page markers in the top right: filled is where you are. */
static void dv_draw_pips(Canvas* canvas, uint8_t page) {
    for(int i = 0; i < DetailPageCount; i++) {
        int x = 112 + i * 6;
        if(i == page) {
            canvas_draw_box(canvas, x, 3, 4, 4);
        } else {
            canvas_draw_frame(canvas, x, 3, 4, 4);
        }
    }
}

static void dv_header(Canvas* canvas, const char* title, uint8_t page) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, DV_HDR_BASE, title);
    dv_draw_pips(canvas, page);
    canvas_draw_line(canvas, 0, DV_RULE_Y, 127, DV_RULE_Y);
}

/* Clip to a pixel width on a character boundary, with an ellipsis. */
static void dv_clip(Canvas* canvas, char* buf, size_t buf_len, int max_w) {
    if(canvas_string_width(canvas, buf) <= max_w) return;
    size_t len = strlen(buf);
    while(len > 1 && canvas_string_width(canvas, buf) > max_w - 6) buf[--len] = '\0';
    snprintf(buf + len, buf_len - len, "..");
}

/* ------------------------------------------------------------------ *
 * Page: DEVICE
 * ------------------------------------------------------------------ */

static void dv_draw_device(Canvas* canvas, const DetailModel* m) {
    const SkimDevice* d = &m->s.dev;
    char buf[48];

    dv_header(canvas, "DEVICE", m->page);

    /* The name, as large as it will go. */
    snprintf(buf, sizeof(buf), "%s", d->last.name[0] ? d->last.name : "(no name)");
    canvas_set_font(canvas, FontPrimary);
    if(canvas_string_width(canvas, buf) > 124) {
        canvas_set_font(canvas, FontSecondary);
        dv_clip(canvas, buf, sizeof(buf), 124);
    }
    canvas_draw_str(canvas, 2, DV_BODY_BASE, buf);

    canvas_set_font(canvas, FontSecondary);

    skim_mac_str(d->last.mac, buf, sizeof(buf));
    canvas_draw_str(canvas, 2, DV_BODY_BASE + DV_LINE_STEP, buf);

    const char* vendor = (d->assess.oui.vendor && d->assess.oui.vendor[0]) ?
                             d->assess.oui.vendor :
                             "unlisted prefix";
    canvas_draw_str(canvas, 2, DV_BODY_BASE + DV_LINE_STEP * 2, vendor);
    canvas_draw_str_aligned(
        canvas,
        126,
        DV_BODY_BASE + DV_LINE_STEP * 2,
        AlignRight,
        AlignBottom,
        (d->last.radio == SkimRadioLe) ? "LE" : "BR/EDR");

    canvas_draw_str(canvas, 2, DV_BODY_BASE + DV_LINE_STEP * 3, skim_sig_cod_name(d->last.cod));
    if(m->s.stale > 0) {
        snprintf(
            buf,
            sizeof(buf),
            "%ddBm gone %u",
            (int)d->best_rssi,
            (unsigned)(m->s.stale > 99 ? 99 : m->s.stale));
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "%ddBm x%u",
            (int)d->best_rssi,
            (unsigned)(d->passes > 99 ? 99 : d->passes));
    }
    canvas_draw_str_aligned(
        canvas, 126, DV_BODY_BASE + DV_LINE_STEP * 3, AlignRight, AlignBottom, buf);

    /* Footer: the verdict, so it is on every page you might be looking at. */
    canvas_draw_box(canvas, 0, DV_FOOTER_Y, 128, 64 - DV_FOOTER_Y);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, 3, DV_FOOTER_BASE, skim_verdict_name(d->assess.verdict));
    snprintf(buf, sizeof(buf), "%u", (unsigned)d->assess.score);
    canvas_draw_str_aligned(canvas, 125, DV_FOOTER_BASE, AlignRight, AlignBottom, buf);
    canvas_set_color(canvas, ColorBlack);
}

/* ------------------------------------------------------------------ *
 * Page: WHY
 * ------------------------------------------------------------------ */

static void dv_draw_why(Canvas* canvas, const DetailModel* m) {
    const SkimDevice* d = &m->s.dev;
    const SkimAssessment* a = &d->assess;
    char buf[48];

    dv_header(canvas, "WHY", m->page);
    canvas_set_font(canvas, FontSecondary);

    /* Collect what fired, in weight order as declared by the enum. */
    SkimSignal fired[SkimSignalCount];
    uint8_t n = 0;
    for(int s = 0; s < SkimSignalCount; s++) {
        if(skim_fired(a, (SkimSignal)s)) fired[n++] = (SkimSignal)s;
    }

    if(n == 0) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "No signals fired.");
        canvas_draw_str_aligned(
            canvas, 64, 38, AlignCenter, AlignCenter, "Ordinary Bluetooth.");
    }

    uint8_t top = m->scroll;
    if(top + DV_WHY_ROWS > n) top = (n > DV_WHY_ROWS) ? (uint8_t)(n - DV_WHY_ROWS) : 0;

    for(uint8_t i = 0; i < DV_WHY_ROWS && (top + i) < n; i++) {
        SkimSignal s = fired[top + i];
        int y = DV_WHY_BASE + i * DV_WHY_STEP;

        snprintf(buf, sizeof(buf), "+%u", (unsigned)a->points[s]);
        canvas_draw_str(canvas, 2, y, buf);

        /* Where the signal has a specific thing to point at, point at it --
         * "Module MAC" is a category, "Bolutek" is evidence. */
        switch(s) {
        case SkimSignalName:
            snprintf(
                buf,
                sizeof(buf),
                "%s",
                a->name_sig && a->name_sig->note ? a->name_sig->note : skim_signal_short(s));
            break;
        case SkimSignalOui:
        case SkimSignalOuiOdd:
            snprintf(
                buf,
                sizeof(buf),
                "%s: %s",
                skim_signal_short(s),
                (a->oui.vendor && a->oui.vendor[0]) ? a->oui.vendor : "?");
            break;
        case SkimSignalFixed:
            snprintf(
                buf,
                sizeof(buf),
                "Still here, x%u",
                (unsigned)(d->passes > 99 ? 99 : d->passes));
            break;
        case SkimSignalClose:
            snprintf(buf, sizeof(buf), "Close: %ddBm", (int)d->best_rssi);
            break;
        default:
            snprintf(buf, sizeof(buf), "%s", skim_signal_short(s));
            break;
        }
        dv_clip(canvas, buf, sizeof(buf), 96);
        canvas_draw_str(canvas, 24, y, buf);
    }

    if(n > DV_WHY_ROWS) {
        elements_scrollbar_pos(
            canvas, 126, DV_WHY_BASE - 8, DV_WHY_ROWS * DV_WHY_STEP, top, (size_t)n);
    }

    /* Footer: the cap, which is the honest half of the score. */
    canvas_draw_box(canvas, 0, DV_FOOTER_Y, 128, 64 - DV_FOOTER_Y);
    canvas_set_color(canvas, ColorWhite);
    if(a->cap_reason) {
        snprintf(buf, sizeof(buf), "held: %s", a->cap_reason);
    } else {
        snprintf(buf, sizeof(buf), "%u of 3 families", (unsigned)a->families);
    }
    dv_clip(canvas, buf, sizeof(buf), 104);
    canvas_draw_str(canvas, 3, DV_FOOTER_BASE, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)a->score);
    canvas_draw_str_aligned(canvas, 125, DV_FOOTER_BASE, AlignRight, AlignBottom, buf);
    canvas_set_color(canvas, ColorBlack);
}

/* ------------------------------------------------------------------ *
 * Page: WHAT NOW
 * ------------------------------------------------------------------ */

static void dv_draw_means(Canvas* canvas, const DetailModel* m) {
    const SkimAssessment* a = &m->s.dev.assess;

    dv_header(canvas, "WHAT NOW", m->page);

    canvas_set_font(canvas, FontSecondary);
    dv_draw_lines(canvas, 2, 20, DV_LINE_STEP, skim_verdict_line(a->verdict));
    canvas_draw_line(canvas, 0, 32, 127, 32);
    dv_draw_lines(canvas, 2, 41, DV_LINE_STEP, skim_verdict_advice(a->verdict));
}

/* ------------------------------------------------------------------ */

static void detail_view_draw(Canvas* canvas, void* model) {
    DetailModel* m = model;
    canvas_clear(canvas);
    switch(m->page) {
    case DetailPageWhy:
        dv_draw_why(canvas, m);
        break;
    case DetailPageMeans:
        dv_draw_means(canvas, m);
        break;
    default:
        dv_draw_device(canvas, m);
        break;
    }
}

static bool detail_view_input(InputEvent* event, void* context) {
    DetailView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool handled = false;
    with_view_model(
        v->view,
        DetailModel * m,
        {
            switch(event->key) {
            case InputKeyLeft:
                m->page = (uint8_t)((m->page + DetailPageCount - 1) % DetailPageCount);
                m->scroll = 0;
                handled = true;
                break;
            case InputKeyRight:
                m->page = (uint8_t)((m->page + 1) % DetailPageCount);
                m->scroll = 0;
                handled = true;
                break;
            case InputKeyUp:
                if(m->page == DetailPageWhy && m->scroll > 0) m->scroll--;
                handled = (m->page == DetailPageWhy);
                break;
            case InputKeyDown:
                if(m->page == DetailPageWhy) {
                    uint8_t n = 0;
                    for(int s = 0; s < SkimSignalCount; s++)
                        if(skim_fired(&m->s.dev.assess, (SkimSignal)s)) n++;
                    if(n > DV_WHY_ROWS && m->scroll < (uint8_t)(n - DV_WHY_ROWS)) m->scroll++;
                    handled = true;
                }
                break;
            default:
                break;
            }
        },
        true);
    return handled;
}

DetailView* detail_view_alloc(void) {
    DetailView* v = malloc(sizeof(DetailView));
    memset(v, 0, sizeof(DetailView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, detail_view_draw);
    view_set_input_callback(v->view, detail_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(DetailModel));
    return v;
}

void detail_view_free(DetailView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* detail_view_get_view(DetailView* v) {
    furi_assert(v);
    return v->view;
}

void detail_view_update(DetailView* v, const SkimDetailSnapshot* snap) {
    furi_assert(v);
    furi_assert(snap);
    with_view_model(v->view, DetailModel * m, { m->s = *snap; }, true);
}

void detail_view_reset_page(DetailView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        DetailModel * m,
        {
            m->page = DetailPageDevice;
            m->scroll = 0;
        },
        true);
}
