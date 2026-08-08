#include "../skimscan_i.h"

/* The detail screen follows the *device*, not the rank. Rank changes under
 * you as the sweep runs -- a module climbing past a phone would otherwise
 * swap the page out from under whoever is reading it. */
static void skimscan_detail_refresh(SkimscanApp* app) {
    SkimDetailSnapshot* s = &app->detail_snap;
    uint16_t total = (uint16_t)skim_db_count(app->db);

    SkimDevice found;
    bool have = false;
    for(uint16_t rank = 0; rank < total; rank++) {
        SkimDevice d;
        if(!skim_db_get(app->db, rank, &d)) break;
        if(memcmp(d.last.mac, s->dev.last.mac, 6) != 0) continue;
        found = d;
        s->rank = (uint16_t)(rank + 1);
        have = true;
        break;
    }

    if(have) s->dev = found;
    s->total = total;
    s->stale = skim_device_staleness(app->db, &s->dev);
}

void skimscan_scene_detail_on_enter(void* context) {
    SkimscanApp* app = context;

    memset(&app->detail_snap, 0, sizeof(app->detail_snap));
    if(skim_db_get(app->db, app->selected, &app->detail_snap.dev)) {
        app->detail_snap.rank = (uint16_t)(app->selected + 1);
        app->detail_snap.total = (uint16_t)skim_db_count(app->db);
        app->detail_snap.stale = skim_device_staleness(app->db, &app->detail_snap.dev);
    }

    detail_view_reset_page(app->detail_view);
    detail_view_update(app->detail_view, &app->detail_snap);
    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewDetail);
}

bool skimscan_scene_detail_on_event(void* context, SceneManagerEvent event) {
    SkimscanApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        skimscan_detail_refresh(app);
        detail_view_update(app->detail_view, &app->detail_snap);
        return true;
    }
    return false;
}

void skimscan_scene_detail_on_exit(void* context) {
    UNUSED(context);
}
