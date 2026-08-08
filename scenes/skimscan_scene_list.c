#include "../skimscan_i.h"

static void skimscan_list_move_cb(void* context, int delta) {
    SkimscanApp* app = context;
    uint16_t total = (uint16_t)skim_db_count(app->db);
    if(total == 0) return;

    if(delta < 0) {
        app->selected = (app->selected == 0) ? (uint16_t)(total - 1) : (uint16_t)(app->selected - 1);
    } else {
        app->selected = (uint16_t)((app->selected + 1) % total);
    }
    skimscan_fill_list_snapshot(app);
    list_view_update(app->list_view, &app->list_snap);
}

static void skimscan_list_open_cb(void* context) {
    SkimscanApp* app = context;
    if(skim_db_count(app->db) == 0) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, SkimscanCustomEventOpenDetail);
}

void skimscan_scene_list_on_enter(void* context) {
    SkimscanApp* app = context;

    list_view_set_move_callback(app->list_view, skimscan_list_move_cb, app);
    list_view_set_open_callback(app->list_view, skimscan_list_open_cb, app);

    skimscan_fill_list_snapshot(app);
    list_view_update(app->list_view, &app->list_snap);

    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewList);
}

bool skimscan_scene_list_on_event(void* context, SceneManagerEvent event) {
    SkimscanApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        /* The sweep is still running underneath, so the list stays live:
         * scores move, devices go stale, the order changes. */
        skimscan_fill_list_snapshot(app);
        list_view_update(app->list_view, &app->list_snap);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == SkimscanCustomEventOpenDetail) {
        scene_manager_next_scene(app->scene_manager, SkimscanSceneDetail);
        return true;
    }
    return false;
}

void skimscan_scene_list_on_exit(void* context) {
    UNUSED(context);
}
