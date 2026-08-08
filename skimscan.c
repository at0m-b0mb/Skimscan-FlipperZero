#include "skimscan_i.h"

#include <string.h>

/* ---------------- feedback ----------------
 *
 * The screen is not where the user is looking. They are looking at the pump.
 * So NOTE is a single low note you can ignore, SUSPECT is a rising pair, and
 * anything at the top gets the buzzer as well -- three levels you can tell
 * apart with the Flipper in a pocket.
 */

static const NotificationSequence seq_note = {
    &message_note_c4,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_suspect = {
    &message_note_e5,
    &message_delay_50,
    &message_note_a5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_likely = {
    &message_note_a5,
    &message_delay_100,
    &message_note_c4,
    &message_delay_100,
    &message_note_a5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_led_note = {
    &message_blue_255,
    &message_delay_100,
    &message_blue_0,
    NULL,
};
static const NotificationSequence seq_led_suspect = {
    &message_red_255,
    &message_green_255,
    &message_delay_250,
    &message_red_0,
    &message_green_0,
    NULL,
};
static const NotificationSequence seq_led_likely = {
    &message_red_255,
    &message_delay_250,
    &message_red_0,
    &message_delay_100,
    &message_red_255,
    &message_delay_250,
    &message_red_0,
    NULL,
};

static const NotificationSequence seq_vibro = {
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    NULL,
};

void skimscan_alarm(SkimscanApp* app, SkimVerdict verdict) {
    furi_assert(app);
    if(verdict <= SkimVerdictClear) return;

    uint32_t now = furi_get_tick();
    /* Let a worse verdict through immediately -- being told about a SUSPECT
     * must never stop you being told about the thing behind it. */
    if(verdict <= app->last_alarm_verdict &&
       now - app->last_alarm_tick < furi_ms_to_ticks(SKIMSCAN_ALARM_GAP_MS)) {
        return;
    }
    app->last_alarm_tick = now;
    app->last_alarm_verdict = verdict;

    if(app->settings.sound) {
        if(verdict >= SkimVerdictLikely) {
            notification_message(app->notifications, &seq_likely);
        } else if(verdict == SkimVerdictSuspect) {
            notification_message(app->notifications, &seq_suspect);
        } else {
            notification_message(app->notifications, &seq_note);
        }
    }
    if(app->settings.led) {
        if(verdict >= SkimVerdictLikely) {
            notification_message(app->notifications, &seq_led_likely);
        } else if(verdict == SkimVerdictSuspect) {
            notification_message(app->notifications, &seq_led_suspect);
        } else {
            notification_message(app->notifications, &seq_led_note);
        }
    }
    if(app->settings.vibro && verdict >= SkimVerdictSuspect) {
        notification_message(app->notifications, &seq_vibro);
    }
}

void skimscan_click(SkimscanApp* app) {
    furi_assert(app);
    if(app->settings.sound) notification_message(app->notifications, &sequence_semi_success);
}

uint8_t skimscan_close_dbm(const SkimscanApp* app) {
    furi_assert(app);
    uint8_t i = app->settings.close_index;
    if(i >= SKIM_CLOSE_COUNT) i = 1;
    return skim_close_dbm[i];
}

/* ---------------- the radio ---------------- */

/* Called on the UART worker thread. It only touches the database, which is
 * mutex-guarded; the screen picks the change up on the next tick. */
static void skimscan_on_device(void* context, const SkimSighting* sighting) {
    SkimscanApp* app = context;
    if(skim_db_observe(app->db, sighting)) {
        /* Something's verdict went up. The noise is made from the GUI tick,
         * not from here -- a hundred milliseconds of latency costs nothing
         * and driving the notification service off a UART worker does not. */
        app->alarm_pending = true;
    }
}

static void skimscan_on_pass(void* context, uint16_t pass) {
    UNUSED(pass);
    SkimscanApp* app = context;
    skim_db_next_pass(app->db);
}

static void skimscan_on_state(void* context, bool scanning) {
    SkimscanApp* app = context;
    app->scanning = scanning;
}

static void skimscan_on_status(void* context, const char* fw, uint8_t caps) {
    UNUSED(fw);
    UNUSED(caps);
    SkimscanApp* app = context;
    /* A companion that just announced itself needs telling what to look at. */
    skim_link_set_mode(app->link, app->settings.mode);
    skim_link_send(app->link, "START\n");
}

void skimscan_sweep_start(SkimscanApp* app) {
    furi_assert(app);
    if(app->sweeping) return;

    skim_db_set_close_dbm(app->db, skimscan_close_dbm(app));
    app->sweeping = true;
    app->demo_ticks = 0;

    if(app->settings.demo) {
        /* No hardware, no port: the scripted forecourt runs off the tick. */
        app->scanning = true;
        return;
    }

    skim_link_start(app->link, app->settings.port);
    skim_link_set_mode(app->link, app->settings.mode);
    skim_link_send(app->link, "START\n");
}

void skimscan_sweep_stop(SkimscanApp* app) {
    furi_assert(app);
    if(!app->sweeping) return;
    app->sweeping = false;
    app->scanning = false;
    if(!app->settings.demo) skim_link_stop(app->link);
}

/* ---------------- snapshots ---------------- */

void skimscan_fill_sweep_snapshot(SkimscanApp* app) {
    furi_assert(app);
    SkimSweepSnapshot* s = &app->sweep_snap;
    memset(s, 0, sizeof(*s));

    s->demo = app->settings.demo;
    s->link_online = app->settings.demo || skim_link_is_online(app->link);
    s->scanning = app->scanning;
    s->mode = app->settings.mode;
    s->pass = skim_db_pass(app->db);
    s->seen = (uint16_t)skim_db_count(app->db);
    s->flagged = (uint16_t)skim_db_flagged(app->db);
    s->have_worst = skim_db_worst(app->db, &s->worst);
}

void skimscan_fill_list_snapshot(SkimscanApp* app) {
    furi_assert(app);
    SkimListSnapshot* s = &app->list_snap;
    memset(s, 0, sizeof(*s));

    s->total = (uint16_t)skim_db_count(app->db);
    if(s->total == 0) {
        app->selected = 0;
        return;
    }
    if(app->selected >= s->total) app->selected = (uint16_t)(s->total - 1);
    s->selected = app->selected;

    /* Keep the selection inside the window, scrolling by one. */
    uint16_t top = s->selected;
    if(top >= LIST_ROWS) {
        top = (uint16_t)(s->selected - (LIST_ROWS - 1));
    } else {
        top = 0;
    }
    if(top + LIST_ROWS > s->total) {
        top = (s->total > LIST_ROWS) ? (uint16_t)(s->total - LIST_ROWS) : 0;
    }
    s->top = top;

    for(uint8_t i = 0; i < LIST_ROWS; i++) {
        if(!skim_db_get(app->db, (size_t)(top + i), &s->row[i])) break;
        s->stale[i] = skim_device_staleness(app->db, &s->row[i]);
        s->visible = (uint8_t)(i + 1);
    }
}

/* ---------------- view dispatcher plumbing ---------------- */

static bool skimscan_custom_event_callback(void* context, uint32_t event) {
    SkimscanApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool skimscan_back_event_callback(void* context) {
    SkimscanApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void skimscan_tick_event_callback(void* context) {
    SkimscanApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---------------- lifecycle ---------------- */

static SkimscanApp* skimscan_app_alloc(void) {
    SkimscanApp* app = malloc(sizeof(SkimscanApp));
    memset(app, 0, sizeof(SkimscanApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&skimscan_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, skimscan_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, skimscan_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, skimscan_tick_event_callback, SKIMSCAN_TICK_MS);

    /* Defaults first, then whatever was saved last run. */
    app->settings.mode = SkimModeBoth;
    app->settings.close_index = 1; /* -70 dBm */
    app->settings.port = SkimPortUsart;
    app->settings.sound = true;
    app->settings.vibro = true;
    app->settings.led = true;
    app->settings.log = false;
    app->settings.demo = false;
    skim_store_settings_load(&app->settings);

    app->db = skim_db_alloc();
    skim_db_set_close_dbm(app->db, skimscan_close_dbm(app));

    app->link = skim_link_alloc();
    skim_link_set_callbacks(
        app->link,
        skimscan_on_device,
        skimscan_on_pass,
        skimscan_on_status,
        skimscan_on_state,
        app);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SkimscanViewSettings,
        variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewAbout, widget_get_view(app->widget));

    app->splash_view = splash_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewSplash, splash_view_get_view(app->splash_view));

    app->sweep_view = sweep_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewSweep, sweep_view_get_view(app->sweep_view));

    app->list_view = list_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewList, list_view_get_view(app->list_view));

    app->detail_view = detail_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewDetail, detail_view_get_view(app->detail_view));

    app->learn_view = learn_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewLearn, learn_view_get_view(app->learn_view));

    app->wiring_view = wiring_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SkimscanViewWiring, wiring_view_get_view(app->wiring_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void skimscan_app_free(SkimscanApp* app) {
    furi_assert(app);

    skimscan_sweep_stop(app);
    skim_store_settings_save(&app->settings);

    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewAbout);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewSplash);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewSweep);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewList);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewDetail);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewLearn);
    view_dispatcher_remove_view(app->view_dispatcher, SkimscanViewWiring);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    splash_view_free(app->splash_view);
    sweep_view_free(app->sweep_view);
    list_view_free(app->list_view);
    detail_view_free(app->detail_view);
    learn_view_free(app->learn_view);
    wiring_view_free(app->wiring_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    skim_link_free(app->link);
    skim_db_free(app->db);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t skimscan_app(void* p) {
    UNUSED(p);
    SkimscanApp* app = skimscan_app_alloc();
    scene_manager_next_scene(app->scene_manager, SkimscanSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    skimscan_app_free(app);
    return 0;
}
