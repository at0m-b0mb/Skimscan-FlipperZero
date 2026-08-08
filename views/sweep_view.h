#pragma once

#include "../helpers/skim_db.h"

#include <gui/view.h>

/* Everything the sweep screen is allowed to know. The scene fills one of
 * these on the tick; the view never reaches into the database itself. */
typedef struct {
    bool link_online;
    bool demo;
    bool scanning;
    uint8_t mode; /* SkimMode */
    uint16_t pass;
    uint16_t seen;
    uint16_t flagged;
    bool have_worst;
    SkimDevice worst;
} SkimSweepSnapshot;

typedef struct SweepView SweepView;

SweepView* sweep_view_alloc(void);
void sweep_view_free(SweepView* v);
View* sweep_view_get_view(SweepView* v);

void sweep_view_update(SweepView* v, const SkimSweepSnapshot* snap);
void sweep_view_tick(SweepView* v);

typedef void (*SweepViewCallback)(void* context);
/** OK -- open the device list. */
void sweep_view_set_open_callback(SweepView* v, SweepViewCallback cb, void* context);
/** OK held -- throw the sweep away and start again. */
void sweep_view_set_reset_callback(SweepView* v, SweepViewCallback cb, void* context);
