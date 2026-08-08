/* A scripted forecourt.
 *
 * Skimscan is useless to evaluate without an ESP32 wired on, and nobody should
 * have to find a live skimmer to see whether the app works. Demo mode replays
 * a plausible petrol station: a few phones, a speaker, a beacon, a customer
 * who drives off after two passes, and one HC-05 that is still there every
 * time you look.
 *
 * It is deterministic -- pass number in, sighting out -- so the README
 * screenshots show exactly what the device shows.
 */
#pragma once

#include "skim_sigs.h"

size_t skim_demo_count(void);

/** Device `i` as it appears on `pass`. False when it is not there this pass. */
bool skim_demo_sighting(size_t i, uint16_t pass, SkimSighting* out);

/** One line describing what the scripted device is, for the demo banner. */
const char* skim_demo_note(size_t i);
