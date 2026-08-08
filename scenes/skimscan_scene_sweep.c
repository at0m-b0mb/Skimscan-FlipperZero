#include "../skimscan_i.h"

/* The sweep scene owns the radio -- but only while the user is actually
 * leaving. Pushing the device list on top calls this scene's on_exit, and
 * tearing the port down at that moment would stop the sweep every time
 * somebody looked at what it had found. The scene state flag tells the two
 * cases apart. */

static void skimscan_sweep_open_cb(void* context) {
    SkimscanApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SkimscanCustomEventOpenList);
}

static void skimscan_sweep_reset_cb(void* context) {
    SkimscanApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SkimscanCustomEventResetSweep);
}

/* Demo mode has no companion to tell it when an inquiry cycle ended, so the
 * tick plays the part: every SKIMSCAN_DEMO_PASS_TICKS it hands the database a
 * whole pass at once, the way a real inquiry arrives in a burst. */
static void skimscan_sweep_demo_tick(SkimscanApp* app) {
    app->demo_ticks++;
    if(app->demo_ticks < SKIMSCAN_DEMO_PASS_TICKS) return;
    app->demo_ticks = 0;

    uint16_t pass = skim_db_pass(app->db);
    for(size_t i = 0; i < skim_demo_count(); i++) {
        SkimSighting s;
        if(!skim_demo_sighting(i, pass, &s)) continue;
        if(app->settings.mode == SkimModeClassic && s.radio != SkimRadioClassic) continue;
        if(app->settings.mode == SkimModeLe && s.radio != SkimRadioLe) continue;
        if(skim_db_observe(app->db, &s)) app->alarm_pending = true;
    }
    skim_db_next_pass(app->db);
}

void skimscan_scene_sweep_on_enter(void* context) {
    SkimscanApp* app = context;

    scene_manager_set_scene_state(app->scene_manager, SkimscanSceneSweep, 0);
    sweep_view_set_open_callback(app->sweep_view, skimscan_sweep_open_cb, app);
    sweep_view_set_reset_callback(app->sweep_view, skimscan_sweep_reset_cb, app);

    skimscan_sweep_start(app);
    skimscan_fill_sweep_snapshot(app);
    sweep_view_update(app->sweep_view, &app->sweep_snap);

    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewSweep);
}

bool skimscan_scene_sweep_on_event(void* context, SceneManagerEvent event) {
    SkimscanApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        if(app->settings.demo) skimscan_sweep_demo_tick(app);

        skimscan_fill_sweep_snapshot(app);
        sweep_view_update(app->sweep_view, &app->sweep_snap);
        sweep_view_tick(app->sweep_view);

        if(app->alarm_pending) {
            app->alarm_pending = false;
            if(app->sweep_snap.have_worst) {
                skimscan_alarm(app, app->sweep_snap.worst.assess.verdict);
            }
        }
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SkimscanCustomEventOpenList:
            app->selected = 0;
            scene_manager_set_scene_state(
                app->scene_manager, SkimscanSceneSweep, SKIMSCAN_SWEEP_DETOUR);
            scene_manager_next_scene(app->scene_manager, SkimscanSceneList);
            return true;
        case SkimscanCustomEventResetSweep:
            skim_db_reset(app->db);
            app->selected = 0;
            app->last_alarm_verdict = SkimVerdictClear;
            skimscan_click(app); /* so a long-press reset is felt, not guessed at */
            return true;
        default:
            break;
        }
    }
    return false;
}

void skimscan_scene_sweep_on_exit(void* context) {
    SkimscanApp* app = context;

    if(scene_manager_get_scene_state(app->scene_manager, SkimscanSceneSweep) ==
       SKIMSCAN_SWEEP_DETOUR) {
        return; /* a child scene went on top; the sweep carries on underneath */
    }

    skimscan_sweep_stop(app);
    if(app->settings.log) skim_store_log_sweep(app->db, "");
}
