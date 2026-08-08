#pragma once

#include "../helpers/skim_db.h"

#include <gui/view.h>

/* Three rows fit between the header rule and the bottom of the screen at a
 * size that stays readable at arm's length. */
#define LIST_ROWS 3

typedef struct {
    SkimDevice row[LIST_ROWS];
    uint16_t stale[LIST_ROWS]; /* passes since each was last heard */
    uint8_t visible;
    uint16_t total;
    uint16_t selected; /* absolute index */
    uint16_t top; /* absolute index of row[0] */
} SkimListSnapshot;

typedef struct ListView ListView;

typedef void (*ListViewMoveCallback)(void* context, int delta);
typedef void (*ListViewCallback)(void* context);

ListView* list_view_alloc(void);
void list_view_free(ListView* v);
View* list_view_get_view(ListView* v);

void list_view_update(ListView* v, const SkimListSnapshot* snap);
void list_view_set_move_callback(ListView* v, ListViewMoveCallback cb, void* context);
void list_view_set_open_callback(ListView* v, ListViewCallback cb, void* context);
