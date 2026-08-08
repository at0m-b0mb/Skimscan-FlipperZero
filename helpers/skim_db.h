/* The device table: what has been heard, how often, and how it scores.
 *
 * The companion radio pushes sightings in from a worker thread while the GUI
 * thread reads them, so everything here is behind a mutex and nothing hands
 * out a pointer into the table -- callers get copies.
 */
#pragma once

#include "skim_score.h"

#include <furi.h>

/* A forecourt has a dozen phones on it. Thirty-two is comfortably more than
 * anywhere Skimscan is meant to be used, and the table is fixed so a busy car
 * park cannot walk the heap. */
#define SKIM_MAX_DEVICES 32

typedef struct {
    SkimSighting last;
    int8_t best_rssi;
    uint16_t passes; /* sweep passes this device appeared in */
    uint16_t sightings; /* raw inquiry responses */
    uint16_t last_pass; /* the pass it was last heard in */
    uint32_t first_tick;
    uint32_t last_tick;
    SkimAssessment assess;
} SkimDevice;

typedef struct SkimDb SkimDb;

SkimDb* skim_db_alloc(void);
void skim_db_free(SkimDb* db);

/** Forget everything and start a new sweep at pass 1. */
void skim_db_reset(SkimDb* db);

/** The proximity threshold handed to the scorer, as a magnitude (70 => -70). */
void skim_db_set_close_dbm(SkimDb* db, uint8_t close_dbm);

/** Record one sighting. Rescores the device.
 *  Returns true if this sighting *raised* the device's verdict, which is the
 *  only moment worth making a noise about. */
bool skim_db_observe(SkimDb* db, const SkimSighting* s);

/** The companion finished an inquiry cycle. */
void skim_db_next_pass(SkimDb* db);
uint16_t skim_db_pass(const SkimDb* db);

size_t skim_db_count(const SkimDb* db);

/** Devices scoring at or above the NOTE floor. */
size_t skim_db_flagged(const SkimDb* db);

/** Copy the device at `rank` in worst-first order. False if out of range. */
bool skim_db_get(const SkimDb* db, size_t rank, SkimDevice* out);

/** The worst device in the table. `out` may be NULL. False if the table is
 *  empty. */
bool skim_db_worst(const SkimDb* db, SkimDevice* out);

/** Passes since this device was last heard -- 0 means "still here". */
uint16_t skim_device_staleness(const SkimDb* db, const SkimDevice* dev);
