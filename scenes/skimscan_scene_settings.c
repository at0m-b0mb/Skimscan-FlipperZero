#include "../skimscan_i.h"

static const char* const on_off[] = {"Off", "On"};

static void settings_mode_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, skim_mode_labels[idx]);
    app->settings.mode = idx;
    /* If a sweep is already running, retarget it rather than making the user
     * back out and start again. */
    if(app->sweeping && !app->settings.demo) skim_link_set_mode(app->link, idx);
}

static void settings_close_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, skim_close_labels[idx]);
    app->settings.close_index = idx;
    /* Every verdict in the table was scored against the old threshold. */
    skim_db_set_close_dbm(app->db, skimscan_close_dbm(app));
}

static void settings_port_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, skim_port_pins[idx]);
    app->settings.port = idx;
}

static void settings_sound_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[idx]);
    app->settings.sound = idx != 0;
}

static void settings_vibro_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[idx]);
    app->settings.vibro = idx != 0;
}

static void settings_led_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[idx]);
    app->settings.led = idx != 0;
}

static void settings_log_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[idx]);
    app->settings.log = idx != 0;
}

static void settings_demo_cb(VariableItem* item) {
    SkimscanApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off[idx]);
    app->settings.demo = idx != 0;
    /* The scripted forecourt and the real one cannot both be the answer, so
     * whatever is in the table belongs to the other one. */
    skim_db_reset(app->db);
    app->selected = 0;
}

void skimscan_scene_settings_on_enter(void* context) {
    SkimscanApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Radios", SkimModeCount, settings_mode_cb, app);
    variable_item_set_current_value_index(item, app->settings.mode);
    variable_item_set_current_value_text(item, skim_mode_labels[app->settings.mode]);

    item = variable_item_list_add(list, "Close is", SKIM_CLOSE_COUNT, settings_close_cb, app);
    variable_item_set_current_value_index(item, app->settings.close_index);
    variable_item_set_current_value_text(item, skim_close_labels[app->settings.close_index]);

    item = variable_item_list_add(list, "Port", SkimPortCount, settings_port_cb, app);
    variable_item_set_current_value_index(item, app->settings.port);
    variable_item_set_current_value_text(item, skim_port_pins[app->settings.port]);

    item = variable_item_list_add(list, "Sound", 2, settings_sound_cb, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "Vibrate", 2, settings_vibro_cb, app);
    variable_item_set_current_value_index(item, app->settings.vibro ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.vibro ? 1 : 0]);

    item = variable_item_list_add(list, "LED", 2, settings_led_cb, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.led ? 1 : 0]);

    item = variable_item_list_add(list, "Log to SD", 2, settings_log_cb, app);
    variable_item_set_current_value_index(item, app->settings.log ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.log ? 1 : 0]);

    item = variable_item_list_add(list, "Demo mode", 2, settings_demo_cb, app);
    variable_item_set_current_value_index(item, app->settings.demo ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.demo ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewSettings);
}

bool skimscan_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void skimscan_scene_settings_on_exit(void* context) {
    SkimscanApp* app = context;
    variable_item_list_reset(app->var_item_list);
    skim_store_settings_save(&app->settings);
}
