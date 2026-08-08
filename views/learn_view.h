#pragma once

#include <gui/view.h>

#define LEARN_PANELS 6

typedef struct LearnView LearnView;

LearnView* learn_view_alloc(void);
void learn_view_free(LearnView* v);
View* learn_view_get_view(LearnView* v);

void learn_view_tick(LearnView* v);
void learn_view_reset(LearnView* v);
