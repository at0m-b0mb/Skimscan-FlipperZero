#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "skimscan_icons.h" /* generated from icons/ by fbt */

#include "helpers/skim_db.h"
#include "helpers/skim_demo.h"
#include "helpers/skim_link.h"
#include "helpers/skim_score.h"
#include "helpers/skim_sigs.h"
#include "helpers/skim_store.h"
#include "views/detail_view.h"
#include "views/learn_view.h"
#include "views/list_view.h"
#include "views/splash_view.h"
#include "views/sweep_view.h"
#include "views/wiring_view.h"
#include "scenes/skimscan_scene.h"

#define SKIMSCAN_VERSION "1.0"

/* The GUI ticks at 100 ms. A demo pass every four seconds is close enough to
 * the ten a real BR/EDR inquiry takes to feel like the same instrument,
 * without making the screenshots take a minute to set up. */
#define SKIMSCAN_TICK_MS 100
#define SKIMSCAN_DEMO_PASS_TICKS 40

/* Nothing makes a noise more than once every three seconds, however many
 * modules turn up at once. */
#define SKIMSCAN_ALARM_GAP_MS 3000

typedef enum {
    SkimscanViewSplash,
    SkimscanViewSubmenu,
    SkimscanViewSweep,
    SkimscanViewList,
    SkimscanViewDetail,
    SkimscanViewLearn,
    SkimscanViewWiring,
    SkimscanViewSettings,
    SkimscanViewAbout,
} SkimscanViewId;

typedef enum {
    /* Above any submenu index, so a custom event cannot be mistaken for a
     * menu selection. */
    SkimscanCustomEventSkipSplash = 100,
    SkimscanCustomEventOpenList,
    SkimscanCustomEventOpenDetail,
    SkimscanCustomEventResetSweep,
} SkimscanCustomEvent;

/* Scene state on Sweep: set while a child scene is on top, so Sweep's
 * on_exit can tell "the user went to look at a device" from "the user left",
 * and only tear the radio down for the second. */
#define SKIMSCAN_SWEEP_DETOUR 1

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    SplashView* splash_view;
    SweepView* sweep_view;
    ListView* list_view;
    DetailView* detail_view;
    LearnView* learn_view;
    WiringView* wiring_view;

    SkimDb* db;
    SkimLink* link;
    SkimSettings settings;

    /* Snapshots live here rather than on a scene's stack: together they are
     * the best part of a kilobyte, and the GUI thread's stack is not the
     * place for it. */
    SkimSweepSnapshot sweep_snap;
    SkimListSnapshot list_snap;
    SkimDetailSnapshot detail_snap;

    uint16_t selected; /* rank of the device the list has selected */
    uint32_t demo_ticks;
    bool scanning;
    bool sweeping; /* the radio (or the demo) is armed */
    uint32_t last_alarm_tick;
    SkimVerdict last_alarm_verdict;
    /* Set by the UART worker when a verdict rises; consumed by the GUI tick. */
    volatile bool alarm_pending;

    /* The intro lives inside the root scene, so coming back to the menu from
     * a sweep never replays it and Back from the menu still exits cleanly. */
    bool splash_done;
} SkimscanApp;

/** Feedback for a device whose verdict just went up. Gated by settings, and
 *  rate-limited, because an alarm that fires constantly is furniture. */
void skimscan_alarm(SkimscanApp* app, SkimVerdict verdict);

/** A short acknowledgement for an action the user took, not a finding. */
void skimscan_click(SkimscanApp* app);

/** The proximity threshold currently configured, as a magnitude. */
uint8_t skimscan_close_dbm(const SkimscanApp* app);

/** Arm and disarm the radio -- real or scripted -- for the sweep scene. */
void skimscan_sweep_start(SkimscanApp* app);
void skimscan_sweep_stop(SkimscanApp* app);

/** Rebuild app->sweep_snap from the database. */
void skimscan_fill_sweep_snapshot(SkimscanApp* app);
/** Rebuild app->list_snap around the current selection. */
void skimscan_fill_list_snapshot(SkimscanApp* app);
