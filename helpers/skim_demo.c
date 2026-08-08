#include "skim_demo.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t mac[6];
    const char* name;
    uint32_t cod;
    SkimRadio radio;
    int8_t rssi; /* nominal level */
    uint8_t jitter; /* +/- dB the level wanders by */
    uint16_t from_pass; /* first pass it is present */
    uint16_t to_pass; /* last pass, or 0 for "never leaves" */
    const char* note;
} DemoDevice;

/* The cast. One skimmer, one LE bridge that is only a NOTE because it is
 * across the road, and a car park's worth of ordinary Bluetooth to make sure
 * the ordinary stuff really does stay quiet. */
static const DemoDevice cast[] = {
    {{0x98, 0xD3, 0x31, 0xFB, 0x2E, 0x0C},
     "HC-05",
     0x001F00,
     SkimRadioClassic,
     -54,
     3,
     1,
     0,
     "the skimmer: bridge module, no class, never moves"},
    {{0x3C, 0x22, 0xFB, 0x4A, 0x19, 0x7E},
     "Kailash's iPhone",
     0x5A020C,
     SkimRadioClassic,
     -63,
     6,
     1,
     2,
     "a customer, who drives off after two passes"},
    {{0x04, 0xFE, 0xA1, 0x22, 0x8B, 0x30},
     "JBL Flip 5",
     0x240404,
     SkimRadioClassic,
     -72,
     4,
     1,
     0,
     "a speaker in the shop: declares itself, scores nothing"},
    {{0x20, 0x16, 0x04, 0x27, 0x11, 0x09},
     "",
     SKIM_COD_NONE,
     SkimRadioLe,
     -83,
     5,
     1,
     0,
     "an LE beacon on a date-coded MAC, too far to matter"},
    {{0x98, 0xD3, 0x32, 0x0A, 0x77, 0xB1},
     "HMSoft",
     SKIM_COD_NONE,
     SkimRadioLe,
     -68,
     4,
     2,
     0,
     "an LE bridge module, one pump over"},
    {{0x5C, 0xF3, 0x70, 0x91, 0x02, 0x44},
     "Mi Band 6",
     SKIM_COD_NONE,
     SkimRadioLe,
     -77,
     6,
     1,
     0,
     "a fitness band walking past"},
    {{0x0C, 0xB8, 0x15, 0x63, 0xD1, 0x08},
     "Ford SYNC",
     0x7A020C,
     SkimRadioClassic,
     -75,
     5,
     3,
     0,
     "a car that pulled onto the forecourt on pass 3"},
};

#define CAST_LEN (sizeof(cast) / sizeof(cast[0]))

size_t skim_demo_count(void) {
    return CAST_LEN;
}

const char* skim_demo_note(size_t i) {
    return (i < CAST_LEN) ? cast[i].note : "";
}

/* A small deterministic hash, so a given (device, pass) always produces the
 * same level. Screenshots have to be reproducible. */
static uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

bool skim_demo_sighting(size_t i, uint16_t pass, SkimSighting* out) {
    if(i >= CAST_LEN || !out) return false;
    const DemoDevice* d = &cast[i];

    if(pass < d->from_pass) return false;
    if(d->to_pass != 0 && pass > d->to_pass) return false;

    memset(out, 0, sizeof(*out));
    memcpy(out->mac, d->mac, 6);
    snprintf(out->name, sizeof(out->name), "%s", d->name);
    out->cod = d->cod;
    out->radio = d->radio;

    int span = (int)d->jitter * 2 + 1;
    int offset = (int)(mix((uint32_t)i, pass) % (uint32_t)span) - (int)d->jitter;
    int rssi = d->rssi + offset;
    if(rssi > -20) rssi = -20;
    if(rssi < -99) rssi = -99;
    out->rssi = (int8_t)rssi;
    return true;
}
