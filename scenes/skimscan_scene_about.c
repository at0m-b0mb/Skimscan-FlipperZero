#include "../skimscan_i.h"

void skimscan_scene_about_on_enter(void* context) {
    SkimscanApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Skimscan " SKIMSCAN_VERSION "\e#\n"
        "Check before you swipe.\n"
        "\n"
        "Card skimmers fitted to fuel\n"
        "pumps and cash machines are\n"
        "usually built around a two-\n"
        "dollar Bluetooth serial bridge,\n"
        "spliced across the reader's\n"
        "data lines. Nobody comes back\n"
        "for the card numbers - they are\n"
        "collected over the air from a\n"
        "car in the lot.\n"
        "\n"
        "That radio is the one part of a\n"
        "skimmer you can find from the\n"
        "outside. Skimscan looks for it.\n"
        "\n"
        "\e#What it looks at\e#\n"
        "An inquiry response carries\n"
        "three things: a name, an\n"
        "address, and a class of device.\n"
        "Skimscan scores seven signals\n"
        "drawn from those three plus how\n"
        "the device behaves over time,\n"
        "grouped into three independent\n"
        "families:\n"
        "\n"
        "identity - is the name a known\n"
        "module, does the address belong\n"
        "to a module maker;\n"
        "declaration - does it say what\n"
        "kind of device it is at all;\n"
        "behaviour - is it still there\n"
        "pass after pass, and is it close\n"
        "enough to be inside this pump.\n"
        "\n"
        "\e#The caps\e#\n"
        "One family of evidence can never\n"
        "produce a verdict, however loud\n"
        "it is. Nothing reaches the top\n"
        "band on a single sighting, and\n"
        "nothing ever scores 100. Open\n"
        "any device and the WHY page\n"
        "shows every signal that fired,\n"
        "what it was worth, and which\n"
        "cap held the score down.\n"
        "\n"
        "\e#What it cannot do\e#\n"
        "It never says a pump is safe,\n"
        "and it cannot. Most skimmers\n"
        "are not radios at all: they\n"
        "store to flash, or use GSM, or\n"
        "are a camera over the PIN pad.\n"
        "None of those are visible here.\n"
        "\n"
        "A renamed module defeats the\n"
        "name table. A hobbyist's robot\n"
        "in the car park will score.\n"
        "Skimscan tells you what it saw\n"
        "and why; the judgement is\n"
        "yours.\n"
        "\n"
        "It is a screening aid, not\n"
        "evidence.\n"
        "\n"
        "\e#Hardware\e#\n"
        "The Flipper's own Bluetooth can\n"
        "advertise but cannot inquire,\n"
        "and has no BR/EDR at all. So\n"
        "the radio is an ESP32 on the\n"
        "GPIO header - a classic ESP32,\n"
        "not an S2, S3 or C3, which have\n"
        "no Bluetooth Classic. See\n"
        "Companion wiring.\n"
        "\n"
        "Demo mode runs a scripted\n"
        "forecourt with no hardware at\n"
        "all.\n"
        "\n"
        "\e#Logs\e#\n"
        "With Log to SD on, every sweep\n"
        "worth reporting is appended to\n"
        "apps_data/skimscan/sweeps.csv\n"
        "with the address, score and the\n"
        "signals behind it.\n"
        "\n"
        "\e#Safety\e#\n"
        "Do not open a pump, pull at a\n"
        "reader, or touch wiring. If a\n"
        "pump scores, pay inside, tell\n"
        "the staff which pump it was,\n"
        "and report it.\n"
        "\n"
        "\e#Licence\e#\n"
        "MIT. Built by at0m-b0mb.\n"
        "github.com/at0m-b0mb/\n"
        "Skimscan-FlipperZero\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, SkimscanViewAbout);
}

bool skimscan_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void skimscan_scene_about_on_exit(void* context) {
    SkimscanApp* app = context;
    widget_reset(app->widget);
}
