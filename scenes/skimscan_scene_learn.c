#include "../skimscan_i.h"

void skimscan_scene_learn_on_enter(void* context) {
    SkimscanApp* app = context;
    learn_view_reset(app->learn_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewLearn);
}

bool skimscan_scene_learn_on_event(void* context, SceneManagerEvent event) {
    SkimscanApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        learn_view_tick(app->learn_view);
        return true;
    }
    return false;
}

void skimscan_scene_learn_on_exit(void* context) {
    UNUSED(context);
}
