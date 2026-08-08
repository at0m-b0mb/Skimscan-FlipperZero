/* Settings that survive a reboot, and the sweep log.
 *
 * The log is the reason a forecourt sweep is worth anything after you have
 * driven away: a CSV on the SD card with the address, the score and the
 * reasons, which is the form a station manager or a police report can use.
 */
#pragma once

#include "skim_db.h"
#include "skim_link.h"

/* Which radios the companion should look at. */
typedef enum {
    SkimModeBoth = 0,
    SkimModeClassic = 1,
    SkimModeLe = 2,
    SkimModeCount,
} SkimMode;

/* "Close" thresholds, as magnitudes. -70 dBm is roughly "inside this pump
 * rather than in the next bay", but it depends on the metal in between, so it
 * is a setting and not a constant. */
#define SKIM_CLOSE_COUNT 3
extern const uint8_t skim_close_dbm[SKIM_CLOSE_COUNT];
extern const char* const skim_close_labels[SKIM_CLOSE_COUNT];
extern const char* const skim_mode_labels[SkimModeCount];

typedef struct {
    uint8_t mode; /* SkimMode */
    uint8_t close_index; /* index into skim_close_dbm */
    uint8_t port; /* SkimPort */
    bool sound;
    bool vibro;
    bool led;
    bool log; /* write sweeps to the SD card */
    bool demo; /* run the scripted forecourt, no hardware needed */
} SkimSettings;

void skim_store_settings_load(SkimSettings* s);
void skim_store_settings_save(const SkimSettings* s);

/** Where the sweeps end up, for the About screen to print. */
extern const char* const skim_log_path_display;

/** Append every device worth reporting to the CSV. Returns the number of rows
 *  written, or 0 if there was nothing to say or the card could not be opened. */
size_t skim_store_log_sweep(const SkimDb* db, const char* site_note);
