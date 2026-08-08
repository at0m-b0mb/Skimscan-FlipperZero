#pragma once

#include "../helpers/skim_db.h"

#include <gui/view.h>

typedef enum {
    DetailPageDevice = 0, /* the facts */
    DetailPageWhy, /* the signals, and what each was worth */
    DetailPageMeans, /* what it means and what to do about it */
    DetailPageCount,
} DetailPage;

typedef struct {
    SkimDevice dev;
    uint16_t stale;
    uint16_t rank; /* 1-based */
    uint16_t total;
} SkimDetailSnapshot;

typedef struct DetailView DetailView;

DetailView* detail_view_alloc(void);
void detail_view_free(DetailView* v);
View* detail_view_get_view(DetailView* v);

void detail_view_update(DetailView* v, const SkimDetailSnapshot* snap);
void detail_view_reset_page(DetailView* v);
