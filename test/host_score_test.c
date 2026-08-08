/* Host tests for the Skimscan signature and scoring engine.
 *
 * The engine is the product. Everything on the Flipper's screen is a rendering
 * of what skim_score() decided, and none of it is visible in a screenshot, so
 * it gets taken apart here on every push: every table entry, every boundary,
 * every cap, and then an exhaustive sweep of the whole evidence space checking
 * the invariants that make the scores mean anything.
 *
 *   make -C test
 */
#include "helpers/skim_score.h"
#include "helpers/skim_sigs.h"

#include <stdio.h>
#include <string.h>

static unsigned checks = 0;
static unsigned failures = 0;

static void ok(bool cond, const char* what, const char* detail) {
    checks++;
    if(!cond) {
        failures++;
        if(failures <= 30) {
            printf("  FAIL  %s%s%s\n", what, detail ? " :: " : "", detail ? detail : "");
        }
    }
}

static void eq_int(long got, long want, const char* what) {
    char detail[128];
    snprintf(detail, sizeof(detail), "got %ld, want %ld", got, want);
    ok(got == want, what, detail);
}

static void eq_str(const char* got, const char* want, const char* what) {
    char detail[192];
    snprintf(detail, sizeof(detail), "got \"%s\", want \"%s\"", got ? got : "(null)", want);
    ok(got && strcmp(got, want) == 0, what, detail);
}

static void section(const char* name) {
    printf("* %s\n", name);
}

/* The Flipper screen is 128 px and the secondary font averages a shade under
 * five pixels a character, so a line over this runs off the right edge. Every
 * multi-line string the engine hands the views goes through here -- it is the
 * one layout rule that can be checked without a device. */
#define SKIM_LINE_MAX 27

static void lines_fit(const char* text, const char* what) {
    const char* p = text;
    while(p && *p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char detail[96];
        snprintf(detail, sizeof(detail), "%.*s (%u)", (int)len, p, (unsigned)len);
        ok(len <= SKIM_LINE_MAX, what, detail);
        if(!nl) break;
        p = nl + 1;
    }
}

/* ------------------------------------------------------------------ *
 * helpers for building evidence
 * ------------------------------------------------------------------ */

static const uint8_t MAC_ORDINARY[6] = {0x3C, 0x22, 0xFB, 0x11, 0x22, 0x33}; /* Apple */
static const uint8_t MAC_BOLUTEK[6] = {0x98, 0xD3, 0x31, 0xFB, 0x2E, 0x0C};
static const uint8_t MAC_ROVING[6] = {0x00, 0x06, 0x66, 0x71, 0x0A, 0x5B};
static const uint8_t MAC_RAYSON[6] = {0x00, 0x12, 0x6F, 0x01, 0x02, 0x03};
static const uint8_t MAC_DATED[6] = {0x20, 0x16, 0x04, 0x27, 0x11, 0x09};
static const uint8_t MAC_NULL[6] = {0x00, 0x00, 0x00, 0x12, 0x34, 0x56};
static const uint8_t MAC_LOCAL[6] = {0x4A, 0x11, 0x22, 0x33, 0x44, 0x55};

static SkimEvidence ev_make(
    const uint8_t mac[6],
    const char* name,
    uint32_t cod,
    SkimRadio radio,
    int8_t rssi,
    uint16_t passes) {
    SkimEvidence ev;
    memset(&ev, 0, sizeof(ev));
    memcpy(ev.last.mac, mac, 6);
    snprintf(ev.last.name, sizeof(ev.last.name), "%s", name ? name : "");
    ev.last.cod = cod;
    ev.last.radio = radio;
    ev.last.rssi = rssi;
    ev.best_rssi = rssi;
    ev.passes = passes;
    ev.close_dbm = 70;
    return ev;
}

/* ------------------------------------------------------------------ *
 * 1. name normalisation
 * ------------------------------------------------------------------ */

static void test_normalise(void) {
    section("name normalisation");
    char buf[64];

    eq_str(skim_name_normalise("HC-05", buf, sizeof(buf)), "hc05", "dash and case");
    eq_str(skim_name_normalise("hc_05", buf, sizeof(buf)), "hc05", "underscore");
    eq_str(skim_name_normalise("H C 0 5", buf, sizeof(buf)), "hc05", "spaces");
    eq_str(skim_name_normalise("H-C-2010-06-01", buf, sizeof(buf)), "hc20100601", "HC-06 default");
    eq_str(skim_name_normalise("MLT-BT05", buf, sizeof(buf)), "mltbt05", "mixed");
    eq_str(skim_name_normalise("", buf, sizeof(buf)), "", "empty stays empty");
    eq_str(skim_name_normalise(NULL, buf, sizeof(buf)), "", "NULL is empty");
    eq_str(skim_name_normalise("!!!---", buf, sizeof(buf)), "", "punctuation only");
    eq_str(skim_name_normalise("Café", buf, sizeof(buf)), "caf", "high bytes dropped");

    /* Truncation must terminate and must not run off the end. */
    char tiny[5];
    eq_str(skim_name_normalise("HC-05-XYZ", tiny, sizeof(tiny)), "hc05", "truncates to fit");
    char one[1];
    eq_str(skim_name_normalise("HC-05", one, sizeof(one)), "", "one-byte buffer");
    /* out_len 0 must not write at all */
    char guard[2] = {'Z', 'Z'};
    skim_name_normalise("HC-05", guard, 0);
    eq_int(guard[0], 'Z', "zero-length buffer untouched");
}

/* ------------------------------------------------------------------ *
 * 2. the name table
 * ------------------------------------------------------------------ */

static void test_name_table(void) {
    section("name table");
    size_t n = 0;
    const SkimNameSig* table = skim_sig_name_table(&n);
    ok(n >= 30, "table has a useful number of entries", NULL);

    for(size_t i = 0; i < n; i++) {
        char pat[64];
        snprintf(pat, sizeof(pat), "%s", table[i].pattern);
        size_t len = strlen(pat);
        bool prefix = len > 0 && pat[len - 1] == '*';
        if(prefix) pat[len - 1] = '\0';

        char what[96];
        snprintf(what, sizeof(what), "entry %s matches itself", table[i].pattern);
        const SkimNameSig* hit = skim_sig_name_lookup(pat);
        ok(hit != NULL, what, pat);

        /* Every entry must carry a note, or the detail page shows a blank. */
        snprintf(what, sizeof(what), "entry %s has a note", table[i].pattern);
        ok(table[i].note && table[i].note[0] != '\0', what, NULL);

        if(prefix) {
            char extended[80];
            snprintf(extended, sizeof(extended), "%s-1A2B", pat);
            snprintf(what, sizeof(what), "prefix %s matches a suffixed name", table[i].pattern);
            ok(skim_sig_name_lookup(extended) != NULL, what, extended);
        }
    }

    /* Known-good hits, spelled the way a real module spells them. */
    ok(skim_sig_name_lookup("HC-05") != NULL, "HC-05", NULL);
    ok(skim_sig_name_lookup("hc06") != NULL, "hc06 lower case", NULL);
    ok(skim_sig_name_lookup("HC-05-2010-06-01") != NULL, "HC-05 with firmware date", NULL);
    ok(skim_sig_name_lookup("linvor") != NULL, "linvor", NULL);
    ok(skim_sig_name_lookup("RNBT-A5B1") != NULL, "RNBT-xxxx", NULL);
    ok(skim_sig_name_lookup("FireFly-1234") != NULL, "FireFly-xxxx", NULL);
    ok(skim_sig_name_lookup("HMSoft") != NULL, "HMSoft", NULL);
    ok(skim_sig_name_lookup("MLT-BT05") != NULL, "MLT-BT05", NULL);

    /* And the things that live in every car park on earth. A false positive
     * here is the failure mode that matters. */
    static const char* innocents[] = {
        "iPhone",       "Kailash's iPhone", "Galaxy S23",      "AirPods Pro",
        "JBL Flip 5",   "Bose QC35 II",     "Tile",            "Mi Band 6",
        "Ford SYNC",    "Toyota BT",        "Pixel Buds",      "LE_WH-1000XM4",
        "TV Samsung",   "Logitech K380",    "Beats Studio",    "Garmin vivo",
        "MX Master 3",  "Nintendo Switch",  "ELM327",          "OBDII",
        "Bluetooth",    "Speaker",          "Car Multimedia",  "BLACKVUE",
    };
    for(size_t i = 0; i < sizeof(innocents) / sizeof(innocents[0]); i++) {
        char what[96];
        snprintf(what, sizeof(what), "innocent name not flagged: %s", innocents[i]);
        ok(skim_sig_name_lookup(innocents[i]) == NULL, what, NULL);
    }

    ok(skim_sig_name_lookup("") == NULL, "empty name matches nothing", NULL);
    ok(skim_sig_name_lookup(NULL) == NULL, "NULL name matches nothing", NULL);
    /* A prefix pattern must not match a *shorter* string. */
    ok(skim_sig_name_lookup("hc0") == NULL, "partial prefix does not match", NULL);
}

/* ------------------------------------------------------------------ *
 * 3. addresses
 * ------------------------------------------------------------------ */

static void test_oui(void) {
    section("address classification");

    eq_int(skim_sig_oui_lookup(MAC_BOLUTEK, SkimRadioClassic).klass, SkimOuiModule, "98:D3 family");
    eq_str(skim_sig_oui_lookup(MAC_BOLUTEK, SkimRadioClassic).vendor, "Bolutek", "98:D3 vendor");
    eq_int(skim_sig_oui_lookup(MAC_ROVING, SkimRadioClassic).klass, SkimOuiModule, "00:06:66");
    eq_str(
        skim_sig_oui_lookup(MAC_ROVING, SkimRadioClassic).vendor,
        "Roving Networks",
        "00:06:66 vendor");
    eq_int(skim_sig_oui_lookup(MAC_RAYSON, SkimRadioClassic).klass, SkimOuiModule, "00:12:6F");
    eq_int(skim_sig_oui_lookup(MAC_DATED, SkimRadioClassic).klass, SkimOuiDateCoded, "20:16");
    eq_int(skim_sig_oui_lookup(MAC_NULL, SkimRadioClassic).klass, SkimOuiDateCoded, "00:00:00");
    eq_int(skim_sig_oui_lookup(MAC_ORDINARY, SkimRadioClassic).klass, SkimOuiNone, "ordinary OUI");

    /* Date-coded range edges: 20:13..20:19 in, 20:12 and 20:1A out. */
    uint8_t m[6] = {0x20, 0x12, 0, 0, 0, 1};
    eq_int(skim_sig_oui_lookup(m, SkimRadioClassic).klass, SkimOuiNone, "20:12 below range");
    m[1] = 0x13;
    eq_int(skim_sig_oui_lookup(m, SkimRadioClassic).klass, SkimOuiDateCoded, "20:13 lower edge");
    m[1] = 0x19;
    eq_int(skim_sig_oui_lookup(m, SkimRadioClassic).klass, SkimOuiDateCoded, "20:19 upper edge");
    m[1] = 0x1A;
    eq_int(skim_sig_oui_lookup(m, SkimRadioClassic).klass, SkimOuiNone, "20:1A above range");

    /* The locally-administered bit means something on BR/EDR and nothing at
     * all on LE, where every modern phone rotates a random address. */
    eq_int(skim_sig_oui_lookup(MAC_LOCAL, SkimRadioClassic).klass, SkimOuiLocal, "local on BR/EDR");
    eq_int(skim_sig_oui_lookup(MAC_LOCAL, SkimRadioLe).klass, SkimOuiNone, "local on LE is normal");

    /* A module OUI still counts on LE -- an HM-10 is an HM-10 either way. */
    eq_int(skim_sig_oui_lookup(MAC_BOLUTEK, SkimRadioLe).klass, SkimOuiModule, "module OUI on LE");

    eq_int(skim_sig_oui_lookup(NULL, SkimRadioClassic).klass, SkimOuiNone, "NULL mac is safe");

    /* Every classification must carry a vendor string the UI can print. */
    ok(skim_sig_oui_lookup(MAC_ORDINARY, SkimRadioClassic).vendor != NULL, "vendor never NULL", NULL);
}

/* ------------------------------------------------------------------ *
 * 4. class of device
 * ------------------------------------------------------------------ */

static void test_cod(void) {
    section("class of device");

    eq_int(skim_sig_cod_class(SKIM_COD_NONE), SkimCodAbsent, "absent");
    eq_int(skim_sig_cod_class(0x000000), SkimCodUncategorised, "all zero");
    eq_int(skim_sig_cod_class(0x001F00), SkimCodUncategorised, "major 0x1F");
    eq_int(skim_sig_cod_class(0x241F00), SkimCodUncategorised, "major 0x1F with services");
    eq_int(skim_sig_cod_class(0x240404), SkimCodOrdinary, "audio headset");
    eq_int(skim_sig_cod_class(0x5A020C), SkimCodOrdinary, "smartphone");
    eq_int(skim_sig_cod_class(0x0C025A), SkimCodOrdinary, "phone, other minor");

    eq_str(skim_sig_cod_name(SKIM_COD_NONE), "not reported", "absent name");
    eq_str(skim_sig_cod_name(0x000000), "Uncategorised", "zero name");
    eq_str(skim_sig_cod_name(0x001F00), "Uncategorised", "0x1F name");
    eq_str(skim_sig_cod_name(0x000100), "Computer", "computer name");
    eq_str(skim_sig_cod_name(0x5A020C), "Phone", "phone name");
    eq_str(skim_sig_cod_name(0x240404), "Audio/Video", "audio name");
    eq_str(skim_sig_cod_name(0x000500), "Peripheral", "peripheral name");
    eq_str(skim_sig_cod_name(0x000700), "Wearable", "wearable name");
    eq_str(skim_sig_cod_name(0x000900), "Health", "health name");

    /* Every major class must render as something printable. */
    for(uint32_t major = 0; major < 0x20; major++) {
        const char* nm = skim_sig_cod_name((major << 8) | 0x04);
        char what[64];
        snprintf(what, sizeof(what), "major 0x%02X has a name", (unsigned)major);
        ok(nm && nm[0] != '\0', what, NULL);
    }
}

/* ------------------------------------------------------------------ *
 * 5. formatting
 * ------------------------------------------------------------------ */

static void test_format(void) {
    section("formatting");
    char buf[32];

    skim_mac_str(MAC_BOLUTEK, buf, sizeof(buf));
    eq_str(buf, "98:D3:31:FB:2E:0C", "mac string");
    skim_oui_str(MAC_BOLUTEK, buf, sizeof(buf));
    eq_str(buf, "98:D3:31", "oui string");

    /* Undersized buffers must produce an empty string, never a partial MAC
     * that reads as a different address. */
    char small[10];
    skim_mac_str(MAC_BOLUTEK, small, sizeof(small));
    eq_str(small, "", "mac into a short buffer");
    char tiny[4];
    skim_oui_str(MAC_BOLUTEK, tiny, sizeof(tiny));
    eq_str(tiny, "", "oui into a short buffer");

    char guard[2] = {'Z', 'Z'};
    skim_mac_str(MAC_BOLUTEK, guard, 0);
    eq_int(guard[0], 'Z', "mac with zero length");
    skim_mac_str(NULL, buf, sizeof(buf));
    eq_str(buf, "", "NULL mac formats empty");
}

/* ------------------------------------------------------------------ *
 * 6. scoring: the named cases
 * ------------------------------------------------------------------ */

static void test_score_cases(void) {
    section("scoring");
    SkimAssessment a;

    /* Nothing at all. */
    SkimEvidence ev = ev_make(MAC_ORDINARY, "iPhone", 0x5A020C, SkimRadioClassic, -88, 1);
    skim_score(&ev, &a);
    eq_int(a.score, 0, "ordinary phone scores nothing");
    eq_int(a.verdict, SkimVerdictClear, "ordinary phone is clear");
    eq_int(a.families, 0, "no families fired");

    /* The full house: an HC-05 on a Bolutek MAC, no class, sitting still,
     * loud. This is the case the whole app exists for. */
    ev = ev_make(MAC_BOLUTEK, "HC-05", 0x001F00, SkimRadioClassic, -52, 8);
    skim_score(&ev, &a);
    eq_int(a.raw, 100, "full house raw is 100");
    eq_int(a.score, 95, "full house is capped at 95");
    eq_int(a.verdict, SkimVerdictLikely, "full house is a likely skimmer");
    eq_int(a.families, 3, "full house spans three families");
    eq_str(a.cap_reason, "never fully certain", "ceiling reason");
    ok(skim_fired(&a, SkimSignalName), "name fired", NULL);
    ok(skim_fired(&a, SkimSignalOui), "oui fired", NULL);
    ok(skim_fired(&a, SkimSignalCod), "cod fired", NULL);
    ok(skim_fired(&a, SkimSignalFixed), "fixed fired", NULL);
    ok(skim_fired(&a, SkimSignalClose), "close fired", NULL);
    ok(!skim_fired(&a, SkimSignalNameless), "nameless did not fire", NULL);
    ok(!skim_fired(&a, SkimSignalOuiOdd), "odd-oui did not fire alongside module oui", NULL);

    /* A hobby robot on a bench: right name, right MAC, but nothing else and
     * only seen once. Two identity signals are still one opinion. */
    ev = ev_make(MAC_BOLUTEK, "HC-05", 0x5A020C, SkimRadioClassic, -91, 1);
    skim_score(&ev, &a);
    eq_int(a.raw, 60, "name + oui raw");
    eq_int(a.families, 1, "both signals are identity");
    eq_int(a.score, 39, "one family cannot exceed NOTE");
    eq_int(a.verdict, SkimVerdictNote, "one family tops out at NOTE");
    eq_str(a.cap_reason, "one kind of evidence", "single-family reason");

    /* Two families, one sighting: allowed into SUSPECT, never past it. */
    ev = ev_make(MAC_BOLUTEK, "HC-05", 0x001F00, SkimRadioClassic, -91, 1);
    skim_score(&ev, &a);
    eq_int(a.raw, 75, "name + oui + cod raw");
    eq_int(a.families, 2, "identity + declaration");
    eq_int(a.score, 69, "two families cap below LIKELY");
    eq_int(a.verdict, SkimVerdictSuspect, "two families reach SUSPECT");

    /* Three families but a single sighting. A passer-by with an HC-05 in a
     * rucksack must not light up the screen on the first pass. */
    ev = ev_make(MAC_BOLUTEK, "HC-05", 0x001F00, SkimRadioClassic, -50, 1);
    skim_score(&ev, &a);
    ok(!skim_fired(&a, SkimSignalFixed), "one pass cannot be 'fixed'", NULL);
    eq_int(a.families, 3, "close alone still opens the behaviour family");
    eq_int(a.raw, 85, "name + oui + cod + close raw");
    eq_int(a.score, 69, "a single sighting cannot reach LIKELY");
    eq_int(a.verdict, SkimVerdictSuspect, "one loud sighting is SUSPECT at most");
    eq_str(a.cap_reason, "seen once, sweep again", "single-sighting reason");

    /* Behaviour and declaration only: a nameless, classless, stationary,
     * nearby BR/EDR device. Suspicious-feeling, but nothing says module. */
    ev = ev_make(MAC_ORDINARY, "", SKIM_COD_NONE, SkimRadioClassic, -40, 20);
    skim_score(&ev, &a);
    ok(!skim_fired(&a, SkimSignalCod), "absent CoD is not 'declares no class'", NULL);
    ok(skim_fired(&a, SkimSignalNameless), "nameless fired", NULL);
    eq_int(a.families, 2, "declaration + behaviour");
    eq_int(a.score, 31, "no identity, so no cap bites below the raw score");
    eq_int(a.verdict, SkimVerdictNote, "no identity signal cannot pass NOTE");
    ok(a.cap_reason == NULL, "raw was already under the identity cap", NULL);

    /* Same, but loud and long-lived enough to bump into the identity cap. */
    ev = ev_make(MAC_ORDINARY, "", 0x001F00, SkimRadioClassic, -40, 20);
    skim_score(&ev, &a);
    eq_int(a.raw, 46, "nameless + cod + fixed + close");
    eq_int(a.score, 39, "identity cap bites");
    eq_str(a.cap_reason, "no module identity", "identity cap reason");

    /* An LE beacon: random address, no name, no class. All three of those are
     * normal on LE and must contribute nothing. */
    ev = ev_make(MAC_LOCAL, "", SKIM_COD_NONE, SkimRadioLe, -45, 30);
    skim_score(&ev, &a);
    eq_int(a.raw, 25, "LE beacon scores only behaviour");
    eq_int(a.score, 14, "behaviour alone is held under NOTE");
    eq_int(a.verdict, SkimVerdictClear, "a stationary LE beacon is ordinary");
    eq_str(a.cap_reason, "only sitting there", "behaviour-only reason");
    ok(!skim_fired(&a, SkimSignalNameless), "LE namelessness is not a signal", NULL);
    ok(!skim_fired(&a, SkimSignalOuiOdd), "LE random address is not a signal", NULL);

    /* An HM-10 wired into a reader, seen over LE. The newer skimmer. */
    ev = ev_make(MAC_BOLUTEK, "HMSoft", SKIM_COD_NONE, SkimRadioLe, -55, 6);
    skim_score(&ev, &a);
    eq_int(a.families, 2, "LE bridge: identity + behaviour");
    eq_int(a.verdict, SkimVerdictSuspect, "LE bridge reaches SUSPECT");
    eq_str(a.cap_reason, "needs a 3rd signal", "third-family reason");

    /* NULL inputs must not crash and must not accuse anyone. */
    skim_score(NULL, &a);
    eq_int(a.score, 0, "NULL evidence scores zero");
    eq_int(a.verdict, SkimVerdictClear, "NULL evidence is clear");
    skim_score(&ev, NULL); /* must simply return */
    ok(true, "NULL output does not crash", NULL);
}

/* ------------------------------------------------------------------ *
 * 7. boundaries
 * ------------------------------------------------------------------ */

static void test_boundaries(void) {
    section("boundaries");
    SkimAssessment a;

    /* "Close" is inclusive of the threshold itself. */
    SkimEvidence ev = ev_make(MAC_ORDINARY, "iPhone", 0x5A020C, SkimRadioClassic, -70, 1);
    ev.close_dbm = 70;
    skim_score(&ev, &a);
    ok(skim_fired(&a, SkimSignalClose), "-70 with a -70 threshold is close", NULL);
    ev.best_rssi = -71;
    skim_score(&ev, &a);
    ok(!skim_fired(&a, SkimSignalClose), "-71 with a -70 threshold is not", NULL);
    ev.best_rssi = -69;
    skim_score(&ev, &a);
    ok(skim_fired(&a, SkimSignalClose), "-69 is close", NULL);

    /* best_rssi drives proximity, not the last sighting -- a device that was
     * loud once and is fading is still something that was next to you. */
    ev = ev_make(MAC_ORDINARY, "iPhone", 0x5A020C, SkimRadioClassic, -95, 1);
    ev.best_rssi = -40;
    skim_score(&ev, &a);
    ok(skim_fired(&a, SkimSignalClose), "best_rssi decides proximity", NULL);

    /* "Fixed" needs three passes. */
    for(uint16_t p = 0; p <= 5; p++) {
        ev = ev_make(MAC_ORDINARY, "iPhone", 0x5A020C, SkimRadioClassic, -95, p);
        skim_score(&ev, &a);
        char what[64];
        snprintf(what, sizeof(what), "passes=%u fixed", (unsigned)p);
        ok(skim_fired(&a, SkimSignalFixed) == (p >= SKIM_FIXED_PASSES), what, NULL);
    }

    /* Verdict band edges, exercised through a real evidence path rather than
     * by poking the classifier directly. */
    eq_int(skim_verdict_floor(SkimVerdictClear), 0, "CLEAR floor");
    eq_int(skim_verdict_floor(SkimVerdictNote), 15, "NOTE floor");
    eq_int(skim_verdict_floor(SkimVerdictSuspect), 40, "SUSPECT floor");
    eq_int(skim_verdict_floor(SkimVerdictLikely), 70, "LIKELY floor");
    ok(skim_verdict_floor(SkimVerdictCount) == 0, "out-of-range floor is safe", NULL);
}

/* ------------------------------------------------------------------ *
 * 8. wording -- every enum value must render
 * ------------------------------------------------------------------ */

static void test_wording(void) {
    section("wording");

    for(int v = 0; v < SkimVerdictCount; v++) {
        char what[64];
        snprintf(what, sizeof(what), "verdict %d has a name", v);
        ok(skim_verdict_name((SkimVerdict)v)[0] != '\0', what, NULL);
        snprintf(what, sizeof(what), "verdict %d has a line", v);
        ok(skim_verdict_line((SkimVerdict)v)[0] != '\0', what, NULL);
        snprintf(what, sizeof(what), "verdict %d has advice", v);
        ok(skim_verdict_advice((SkimVerdict)v)[0] != '\0', what, NULL);
        lines_fit(skim_verdict_line((SkimVerdict)v), "verdict line fits the screen");
        lines_fit(skim_verdict_advice((SkimVerdict)v), "verdict advice fits the screen");
    }
    for(int s = 0; s < SkimSignalCount; s++) {
        char what[64];
        snprintf(what, sizeof(what), "signal %d has a label", s);
        ok(skim_signal_label((SkimSignal)s)[0] != '\0', what, NULL);
        snprintf(what, sizeof(what), "signal %d short label fits a row", s);
        ok(strlen(skim_signal_short((SkimSignal)s)) <= SKIM_SIGNAL_SHORT_MAX, what, NULL);
        snprintf(what, sizeof(what), "signal %d explains itself", s);
        ok(strlen(skim_signal_why((SkimSignal)s)) > 40, what, NULL);
        lines_fit(skim_signal_why((SkimSignal)s), "signal explanation fits the screen");
        snprintf(what, sizeof(what), "signal %d has a weight", s);
        ok(skim_signal_weight((SkimSignal)s) > 0, what, NULL);
        snprintf(what, sizeof(what), "signal %d family has a name", s);
        ok(skim_family_name(skim_signal_family((SkimSignal)s))[0] != '?', what, NULL);
    }
    /* Out-of-range must be safe: these run from view code on every frame. */
    ok(skim_signal_weight(SkimSignalCount) == 0, "weight out of range", NULL);
    ok(skim_verdict_name(SkimVerdictCount)[0] != '\0', "verdict name out of range", NULL);

    /* The one thing the app must never say. */
    for(int v = 0; v < SkimVerdictCount; v++) {
        for(int any = 0; any < 2; any++) {
            const char* head = skim_sweep_headline((SkimVerdict)v, any != 0);
            const char* sub = skim_sweep_subline((SkimVerdict)v, any != 0);
            ok(head[0] != '\0', "headline present", NULL);
            ok(strstr(head, "SAFE") == NULL, "no headline claims safety", head);
            ok(strstr(sub, "safe") == NULL, "no subline claims safety", sub);
            /* The sweep screen fits eight characters of primary font beside
             * the card. A longer headline runs off the panel. */
            ok(strlen(head) <= 8, "headline fits the sweep panel", head);
            lines_fit(sub, "sweep subline fits the screen");
        }
    }
    for(int v = 0; v < SkimVerdictCount; v++) {
        ok(strlen(skim_verdict_name((SkimVerdict)v)) <= 8, "verdict name fits", NULL);
    }
    eq_str(skim_sweep_headline(SkimVerdictClear, true), "NO MATCH", "quiet sweep wording");
    eq_str(skim_sweep_headline(SkimVerdictClear, false), "QUIET", "silent sweep wording");
}

/* ------------------------------------------------------------------ *
 * 9. exhaustive invariants
 *
 * The named cases above say what the engine does. This says what it may never
 * do, across every combination of evidence it can be handed.
 * ------------------------------------------------------------------ */

static void test_invariants(void) {
    section("invariants over the whole evidence space");

    static const uint8_t* macs[] = {
        MAC_ORDINARY, MAC_BOLUTEK, MAC_ROVING, MAC_DATED, MAC_NULL, MAC_LOCAL};
    static const char* names[] = {"", "HC-05", "iPhone", "HMSoft", "RNBT-9F0C"};
    static const uint32_t cods[] = {SKIM_COD_NONE, 0x000000, 0x001F00, 0x240404, 0x5A020C};
    static const int8_t rssis[] = {-20, -70, -71, -99};
    static const uint16_t passes[] = {0, 1, 2, 3, 12};
    static const uint8_t closes[] = {60, 70, 80};

    unsigned likely_seen = 0, suspect_seen = 0, note_seen = 0, clear_seen = 0;

    for(size_t mi = 0; mi < 6; mi++)
        for(size_t ni = 0; ni < 5; ni++)
            for(size_t ci = 0; ci < 5; ci++)
                for(int radio = 0; radio < SkimRadioCount; radio++)
                    for(size_t ri = 0; ri < 4; ri++)
                        for(size_t pi = 0; pi < 5; pi++)
                            for(size_t xi = 0; xi < 3; xi++) {
                                SkimEvidence ev = ev_make(
                                    macs[mi],
                                    names[ni],
                                    cods[ci],
                                    (SkimRadio)radio,
                                    rssis[ri],
                                    passes[pi]);
                                ev.close_dbm = closes[xi];

                                SkimAssessment a;
                                skim_score(&ev, &a);

                                char id[160];
                                snprintf(
                                    id,
                                    sizeof(id),
                                    "mac%zu name=%s cod=%08lX radio=%d rssi=%d passes=%u close=%u",
                                    mi,
                                    names[ni],
                                    (unsigned long)cods[ci],
                                    radio,
                                    rssis[ri],
                                    (unsigned)passes[pi],
                                    (unsigned)closes[xi]);

                                /* A cap may only ever hold a score down. */
                                ok(a.score <= a.raw, "score never exceeds raw", id);
                                ok(a.raw <= 100, "raw never exceeds 100", id);
                                ok(a.score <= SKIM_SCORE_CEILING, "score never exceeds the ceiling", id);

                                /* The verdict must agree with the bands. */
                                SkimVerdict want = SkimVerdictClear;
                                if(a.score >= skim_verdict_floor(SkimVerdictLikely))
                                    want = SkimVerdictLikely;
                                else if(a.score >= skim_verdict_floor(SkimVerdictSuspect))
                                    want = SkimVerdictSuspect;
                                else if(a.score >= skim_verdict_floor(SkimVerdictNote))
                                    want = SkimVerdictNote;
                                ok(a.verdict == want, "verdict matches its band", id);

                                /* The claims the caps exist to make. */
                                if(a.verdict == SkimVerdictLikely) {
                                    likely_seen++;
                                    ok(a.families == 3, "LIKELY spans three families", id);
                                    ok(ev.passes >= 2, "LIKELY needs a second sighting", id);
                                    ok(
                                        skim_fired(&a, SkimSignalName) ||
                                            skim_fired(&a, SkimSignalOui) ||
                                            skim_fired(&a, SkimSignalOuiOdd),
                                        "LIKELY needs an identity signal",
                                        id);
                                }
                                if(a.verdict >= SkimVerdictNote) {
                                    ok(
                                        skim_fired(&a, SkimSignalName) ||
                                            skim_fired(&a, SkimSignalOui) ||
                                            skim_fired(&a, SkimSignalOuiOdd) ||
                                            skim_fired(&a, SkimSignalCod) ||
                                            skim_fired(&a, SkimSignalNameless),
                                        "NOTE needs more than sitting still",
                                        id);
                                }
                                if(a.verdict >= SkimVerdictSuspect) {
                                    suspect_seen++;
                                    ok(a.families >= 2, "SUSPECT spans two families", id);
                                    ok(
                                        skim_fired(&a, SkimSignalName) ||
                                            skim_fired(&a, SkimSignalOui) ||
                                            skim_fired(&a, SkimSignalOuiOdd),
                                        "SUSPECT needs an identity signal",
                                        id);
                                }
                                if(a.verdict == SkimVerdictNote) note_seen++;
                                if(a.verdict == SkimVerdictClear) clear_seen++;

                                /* A capped score must always say why, in a
                                 * reason short enough to actually print. */
                                ok(
                                    (a.score == a.raw) == (a.cap_reason == NULL),
                                    "a capped score explains itself",
                                    id);
                                if(a.cap_reason) {
                                    ok(
                                        strlen(a.cap_reason) <= SKIM_CAP_REASON_MAX,
                                        "cap reason fits its row",
                                        a.cap_reason);
                                }

                                /* Mutually exclusive signals. */
                                ok(
                                    !(skim_fired(&a, SkimSignalOui) &&
                                      skim_fired(&a, SkimSignalOuiOdd)),
                                    "OUI signals are exclusive",
                                    id);
                                ok(
                                    !(skim_fired(&a, SkimSignalName) &&
                                      skim_fired(&a, SkimSignalNameless)),
                                    "name signals are exclusive",
                                    id);

                                /* LE must never be penalised for behaving like LE. */
                                if(radio == SkimRadioLe) {
                                    ok(
                                        !skim_fired(&a, SkimSignalNameless),
                                        "LE is never flagged nameless",
                                        id);
                                }

                                /* Points and fired flags must agree. */
                                uint16_t sum = 0;
                                for(int s = 0; s < SkimSignalCount; s++) {
                                    bool f = skim_fired(&a, (SkimSignal)s);
                                    ok(
                                        (a.points[s] != 0) == f,
                                        "points are set exactly for fired signals",
                                        id);
                                    sum += a.points[s];
                                }
                                if(sum > 100) sum = 100;
                                ok(sum == a.raw, "points add up to raw", id);
                            }

    /* The space must actually exercise every band, or the invariants above
     * are checking an empty set. */
    ok(likely_seen > 0, "the space produces LIKELY verdicts", NULL);
    ok(suspect_seen > 0, "the space produces SUSPECT verdicts", NULL);
    ok(note_seen > 0, "the space produces NOTE verdicts", NULL);
    ok(clear_seen > 0, "the space produces CLEAR verdicts", NULL);
}

/* ------------------------------------------------------------------ *
 * 10. monotonicity
 *
 * More evidence must never mean a lower score. It is easy to break this by
 * hand while tuning weights, and impossible to notice on the device.
 * ------------------------------------------------------------------ */

static void test_monotonic(void) {
    section("monotonicity");
    SkimAssessment a, b;

    /* Adding passes never lowers the score. */
    for(uint16_t p = 0; p < 20; p++) {
        SkimEvidence lo = ev_make(MAC_BOLUTEK, "HC-05", 0x001F00, SkimRadioClassic, -55, p);
        SkimEvidence hi = lo;
        hi.passes = (uint16_t)(p + 1);
        skim_score(&lo, &a);
        skim_score(&hi, &b);
        char what[64];
        snprintf(what, sizeof(what), "passes %u -> %u never lowers the score", p, p + 1);
        ok(b.score >= a.score, what, NULL);
    }

    /* Getting closer never lowers the score. */
    for(int r = -99; r < -20; r++) {
        SkimEvidence lo = ev_make(MAC_BOLUTEK, "HC-05", 0x001F00, SkimRadioClassic, (int8_t)r, 5);
        SkimEvidence hi = lo;
        hi.best_rssi = (int8_t)(r + 1);
        skim_score(&lo, &a);
        skim_score(&hi, &b);
        char what[64];
        snprintf(what, sizeof(what), "rssi %d -> %d never lowers the score", r, r + 1);
        ok(b.score >= a.score, what, NULL);
    }

    /* A known module name never scores below the same device unnamed. */
    static const uint8_t* macs[] = {MAC_ORDINARY, MAC_BOLUTEK, MAC_DATED};
    static const uint32_t cods[] = {SKIM_COD_NONE, 0x001F00, 0x5A020C};
    for(size_t mi = 0; mi < 3; mi++)
        for(size_t ci = 0; ci < 3; ci++)
            for(uint16_t p = 0; p < 6; p++) {
                SkimEvidence plain =
                    ev_make(macs[mi], "Speaker", cods[ci], SkimRadioClassic, -60, p);
                SkimEvidence flagged = plain;
                snprintf(flagged.last.name, sizeof(flagged.last.name), "HC-05");
                skim_score(&plain, &a);
                skim_score(&flagged, &b);
                ok(b.score >= a.score, "a module name never scores lower", NULL);
            }
}

/* ------------------------------------------------------------------ */

int main(void) {
    printf("Skimscan engine tests\n");
    printf("=====================\n");

    test_normalise();
    test_name_table();
    test_oui();
    test_cod();
    test_format();
    test_score_cases();
    test_boundaries();
    test_wording();
    test_invariants();
    test_monotonic();

    printf("---------------------\n");
    printf("%u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
