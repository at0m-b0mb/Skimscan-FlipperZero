#include "../skimscan_i.h"

/* The intro plays for a shade under two seconds at the 100 ms tick, then hands
 * off to the menu; any key skips it. It lives inside the root scene rather
 * than on the scene stack, so returning to the menu from a sweep never replays
 * it, and Back from the menu still exits the app cleanly. */

typedef enum {
    StartIndexSweep,
    StartIndexDevices,
    StartIndexLearn,
    StartIndexWiring,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void skimscan_scene_start_submenu_cb(void* context, uint32_t index) {
    SkimscanApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void skimscan_scene_start_show_menu(SkimscanApp* app) {
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Skimscan");
    submenu_add_item(
        submenu, "Sweep this pump", StartIndexSweep, skimscan_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Devices heard", StartIndexDevices, skimscan_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "How skimmers work", StartIndexLearn, skimscan_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Companion wiring", StartIndexWiring, skimscan_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Settings", StartIndexSettings, skimscan_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, skimscan_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, SkimscanSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewSubmenu);
}

static void skimscan_scene_start_skip_splash(void* context) {
    SkimscanApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SkimscanCustomEventSkipSplash);
}

void skimscan_scene_start_on_enter(void* context) {
    SkimscanApp* app = context;

    if(!app->splash_done) {
        splash_view_set_skip_callback(app->splash_view, skimscan_scene_start_skip_splash, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewSplash);
    } else {
        skimscan_scene_start_show_menu(app);
    }
}

bool skimscan_scene_start_on_event(void* context, SceneManagerEvent event) {
    SkimscanApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(!app->splash_done) {
            if(splash_view_tick(app->splash_view)) {
                app->splash_done = true;
                skimscan_scene_start_show_menu(app);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(!app->splash_done && event.event == SkimscanCustomEventSkipSplash) {
            app->splash_done = true;
            skimscan_scene_start_show_menu(app);
            return true;
        }

        scene_manager_set_scene_state(app->scene_manager, SkimscanSceneStart, event.event);
        switch(event.event) {
        case StartIndexSweep:
            scene_manager_next_scene(app->scene_manager, SkimscanSceneSweep);
            consumed = true;
            break;
        case StartIndexDevices:
            scene_manager_next_scene(app->scene_manager, SkimscanSceneList);
            consumed = true;
            break;
        case StartIndexLearn:
            scene_manager_next_scene(app->scene_manager, SkimscanSceneLearn);
            consumed = true;
            break;
        case StartIndexWiring:
            scene_manager_next_scene(app->scene_manager, SkimscanSceneWiring);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, SkimscanSceneSettings);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, SkimscanSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void skimscan_scene_start_on_exit(void* context) {
    SkimscanApp* app = context;
    submenu_reset(app->submenu);
}
