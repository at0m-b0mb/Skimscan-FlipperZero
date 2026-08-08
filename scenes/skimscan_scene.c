#include "../skimscan_i.h"

// Generate on_enter handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const skimscan_scene_on_enter_handlers[])(void*) = {
#include "skimscan_scene_config.h"
};
#undef ADD_SCENE

// Generate on_event handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const skimscan_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "skimscan_scene_config.h"
};
#undef ADD_SCENE

// Generate on_exit handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const skimscan_scene_on_exit_handlers[])(void* context) = {
#include "skimscan_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers skimscan_scene_handlers = {
    .on_enter_handlers = skimscan_scene_on_enter_handlers,
    .on_event_handlers = skimscan_scene_on_event_handlers,
    .on_exit_handlers = skimscan_scene_on_exit_handlers,
    .scene_num = SkimscanSceneNum,
};
