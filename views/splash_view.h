#pragma once

#include <gui/view.h>
#include <stdbool.h>

typedef struct SplashView SplashView;

SplashView* splash_view_alloc(void);
void splash_view_free(SplashView* v);
View* splash_view_get_view(SplashView* v);

/** Advance the animation. True once it has played out. */
bool splash_view_tick(SplashView* v);

typedef void (*SplashViewCallback)(void* context);
/** Any key -- skip straight to the menu. */
void splash_view_set_skip_callback(SplashView* v, SplashViewCallback cb, void* context);
