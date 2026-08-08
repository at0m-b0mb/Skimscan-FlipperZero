/* Four wires, drawn rather than described.
 *
 * Nobody wants to alt-tab to a README with a breadboard in one hand, so the
 * pinout lives on the device, next to a live "is it actually talking" light.
 */
#include "wiring_view.h"
#include "../helpers/skim_link.h"

#include <furi.h>
#include <gui/gui.h>
#include <stdio.h>
#include <string.h>

#define WV_HDR_BASE 9
#define WV_RULE_Y 11
#define WV_COL_BASE 19
#define WV_ROW0 27
#define WV_ROW_STEP 8
#define WV_STATUS_Y 54
#define WV_LEFT_X 44 /* right edge of the Flipper column */
#define WV_RIGHT_X 82 /* left edge of the ESP32 column */

struct WiringView {
    View* view;
};

typedef struct {
    uint8_t port;
    bool online;
    char firmware[16];
} WiringModel;

typedef struct {
    const char* flipper;
    const char* esp;
    int8_t arrow; /* -1 left, +1 right, 0 plain */
} WireRow;

/* The companion talks on its own UART0, which is also its USB port -- so the
 * board is either plugged into a computer or into the Flipper, never both. */
static const WireRow rows_usart[] = {
    {"13 TX", "RX0", +1},
    {"14 RX", "TX0", -1},
    {"8 GND", "GND", 0},
    {"1 5V", "5V", 0},
};
static const WireRow rows_lpuart[] = {
    {"15 TX", "RX0", +1},
    {"16 RX", "TX0", -1},
    {"8 GND", "GND", 0},
    {"1 5V", "5V", 0},
};

#define WIRE_ROWS 4

static void wv_arrow(Canvas* canvas, int x0, int x1, int y, int8_t dir) {
    canvas_draw_line(canvas, x0, y, x1, y);
    if(dir > 0) {
        canvas_draw_line(canvas, x1, y, x1 - 3, y - 2);
        canvas_draw_line(canvas, x1, y, x1 - 3, y + 2);
    } else if(dir < 0) {
        canvas_draw_line(canvas, x0, y, x0 + 3, y - 2);
        canvas_draw_line(canvas, x0, y, x0 + 3, y + 2);
    }
}

static void wiring_view_draw(Canvas* canvas, void* model) {
    WiringModel* m = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, WV_HDR_BASE, "WIRING");
    canvas_draw_str_aligned(
        canvas,
        126,
        WV_HDR_BASE,
        AlignRight,
        AlignBottom,
        skim_port_labels[m->port < SkimPortCount ? m->port : 0]);
    canvas_draw_line(canvas, 0, WV_RULE_Y, 127, WV_RULE_Y);

    canvas_draw_str(canvas, 8, WV_COL_BASE, "FLIPPER");
    canvas_draw_str(canvas, WV_RIGHT_X, WV_COL_BASE, "ESP32");

    const WireRow* rows = (m->port == SkimPortLpuart) ? rows_lpuart : rows_usart;
    for(int i = 0; i < WIRE_ROWS; i++) {
        int base = WV_ROW0 + i * WV_ROW_STEP;
        canvas_draw_str_aligned(canvas, WV_LEFT_X, base, AlignRight, AlignBottom, rows[i].flipper);
        canvas_draw_str(canvas, WV_RIGHT_X, base, rows[i].esp);
        wv_arrow(canvas, WV_LEFT_X + 3, WV_RIGHT_X - 3, base - 3, rows[i].arrow);
    }

    /* Live status, inverted so it reads as a lamp rather than a caption. */
    canvas_draw_box(canvas, 0, WV_STATUS_Y, 128, 64 - WV_STATUS_Y);
    canvas_set_color(canvas, ColorWhite);
    char buf[36];
    if(m->online) {
        snprintf(buf, sizeof(buf), "ONLINE  fw %s", m->firmware[0] ? m->firmware : "?");
    } else {
        snprintf(buf, sizeof(buf), "NO REPLY  115200 8N1");
    }
    canvas_draw_str(canvas, 3, 62, buf);
    canvas_draw_str_aligned(canvas, 125, 62, AlignRight, AlignBottom, m->online ? "OK" : "--");
    canvas_set_color(canvas, ColorBlack);
}

WiringView* wiring_view_alloc(void) {
    WiringView* v = malloc(sizeof(WiringView));
    memset(v, 0, sizeof(WiringView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, wiring_view_draw);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(WiringModel));
    return v;
}

void wiring_view_free(WiringView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* wiring_view_get_view(WiringView* v) {
    furi_assert(v);
    return v->view;
}

void wiring_view_update(WiringView* v, uint8_t port, bool online, const char* firmware) {
    furi_assert(v);
    with_view_model(
        v->view,
        WiringModel * m,
        {
            m->port = port;
            m->online = online;
            snprintf(m->firmware, sizeof(m->firmware), "%s", firmware ? firmware : "");
        },
        true);
}
