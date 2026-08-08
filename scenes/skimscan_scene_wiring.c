#include "../skimscan_i.h"

/* Opened from the menu, this is a diagram. Opened while a sweep is running,
 * the status strip at the bottom is a live answer to "is the thing I just
 * wired up actually talking to me", which is the only question anyone has
 * while looking at a pinout. */

void skimscan_scene_wiring_on_enter(void* context) {
    SkimscanApp* app = context;
    wiring_view_update(
        app->wiring_view,
        app->settings.port,
        skim_link_is_online(app->link),
        skim_link_firmware(app->link));
    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewWiring);
}

bool skimscan_scene_wiring_on_event(void* context, SceneManagerEvent event) {
    SkimscanApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        wiring_view_update(
            app->wiring_view,
            app->settings.port,
            skim_link_is_online(app->link),
            skim_link_firmware(app->link));
        return true;
    }
    return false;
}

void skimscan_scene_wiring_on_exit(void* context) {
    UNUSED(context);
}
