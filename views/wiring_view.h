#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef struct WiringView WiringView;

WiringView* wiring_view_alloc(void);
void wiring_view_free(WiringView* v);
View* wiring_view_get_view(WiringView* v);

/** `port` is a SkimPort; `online` dims the diagram into a live status read. */
void wiring_view_update(WiringView* v, uint8_t port, bool online, const char* firmware);
