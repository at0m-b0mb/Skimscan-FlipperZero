/* Runs the scripted forecourt through the real scoring engine and prints the
 * result as JSON, so tools_gen_mockups.py can draw README screenshots from
 * what the engine actually decided rather than from what the author remembers
 * it deciding.
 *
 *   make -C test dump
 */
#include "helpers/skim_demo.h"
#include "helpers/skim_score.h"
#include "helpers/skim_sigs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirrors helpers/skim_db.c's accumulation, minus the mutex and the Flipper. */
typedef struct {
    SkimSighting last;
    int8_t best_rssi;
    uint16_t passes;
    uint16_t last_pass;
    SkimAssessment assess;
} Device;

#define MAX_DEVICES 32
#define PASSES 8
#define CLOSE_DBM 70

static Device dev[MAX_DEVICES];
static size_t count = 0;

static void observe(const SkimSighting* s, uint16_t pass) {
    Device* d = NULL;
    for(size_t i = 0; i < count; i++) {
        if(memcmp(dev[i].last.mac, s->mac, 6) == 0) {
            d = &dev[i];
            break;
        }
    }
    if(!d) {
        if(count >= MAX_DEVICES) return;
        d = &dev[count++];
        memset(d, 0, sizeof(*d));
        d->best_rssi = -128;
    }
    d->last = *s;
    if(s->rssi > d->best_rssi) d->best_rssi = s->rssi;
    if(d->last_pass != pass) {
        d->last_pass = pass;
        d->passes++;
    }
    SkimEvidence ev = {
        .last = d->last,
        .best_rssi = d->best_rssi,
        .passes = d->passes,
        .close_dbm = CLOSE_DBM,
    };
    skim_score(&ev, &d->assess);
}

static int by_score(const void* a, const void* b) {
    const Device* x = a;
    const Device* y = b;
    if(x->assess.score != y->assess.score) return y->assess.score - x->assess.score;
    return y->best_rssi - x->best_rssi;
}

static void json_str(const char* s) {
    putchar('"');
    for(const char* p = s; p && *p; p++) {
        if(*p == '"' || *p == '\\') putchar('\\');
        putchar(*p);
    }
    putchar('"');
}

int main(void) {
    for(uint16_t pass = 1; pass <= PASSES; pass++) {
        for(size_t i = 0; i < skim_demo_count(); i++) {
            SkimSighting s;
            if(skim_demo_sighting(i, pass, &s)) observe(&s, pass);
        }
    }
    qsort(dev, count, sizeof(Device), by_score);

    printf("{\n  \"passes\": %d,\n  \"close_dbm\": %d,\n  \"devices\": [\n", PASSES, CLOSE_DBM);
    for(size_t i = 0; i < count; i++) {
        const Device* d = &dev[i];
        const SkimAssessment* a = &d->assess;
        char mac[20], oui[12];
        skim_mac_str(d->last.mac, mac, sizeof(mac));
        skim_oui_str(d->last.mac, oui, sizeof(oui));

        printf("    {\n");
        printf("      \"name\": ");
        json_str(d->last.name);
        printf(",\n      \"mac\": ");
        json_str(mac);
        printf(",\n      \"oui\": ");
        json_str(oui);
        printf(",\n      \"vendor\": ");
        json_str(a->oui.vendor ? a->oui.vendor : "");
        printf(",\n      \"radio\": ");
        json_str(d->last.radio == SkimRadioLe ? "LE" : "BR/EDR");
        printf(",\n      \"class\": ");
        json_str(skim_sig_cod_name(d->last.cod));
        printf(",\n      \"module_note\": ");
        json_str(a->name_sig && a->name_sig->note ? a->name_sig->note : "");
        printf(
            ",\n      \"rssi\": %d,\n      \"passes\": %u,\n      \"score\": %u,\n"
            "      \"raw\": %u,\n      \"families\": %u,\n",
            (int)d->best_rssi,
            (unsigned)d->passes,
            (unsigned)a->score,
            (unsigned)a->raw,
            (unsigned)a->families);
        printf("      \"verdict\": ");
        json_str(skim_verdict_name(a->verdict));
        printf(",\n      \"cap\": ");
        json_str(a->cap_reason ? a->cap_reason : "");
        printf(",\n      \"signals\": [");
        bool first = true;
        for(int s = 0; s < SkimSignalCount; s++) {
            if(!skim_fired(a, (SkimSignal)s)) continue;
            if(!first) printf(", ");
            first = false;
            printf("{\"short\": ");
            json_str(skim_signal_short((SkimSignal)s));
            printf(", \"points\": %u}", (unsigned)a->points[s]);
        }
        printf("]\n    }%s\n", (i + 1 < count) ? "," : "");
    }
    printf("  ]\n}\n");
    return 0;
}
