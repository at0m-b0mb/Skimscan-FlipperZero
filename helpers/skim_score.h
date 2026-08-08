/* The scoring engine.
 *
 * Seven signals, grouped into three independent families. A score is only
 * allowed to climb as far as the *breadth* of the evidence justifies: one
 * family of evidence can never produce a verdict, however loud it is, and
 * nothing reaches the top band on a single sighting.
 *
 * The design goal is not sensitivity. It is that every number on the screen
 * can be taken apart on the detail page and read back as a sentence.
 *
 * Flipper-free on purpose -- see test/.
 */
#pragma once

#include "skim_sigs.h"

/* Nothing is ever certain from outside a locked panel, so nothing scores 100. */
#define SKIM_SCORE_CEILING 95

/* A cap reason is printed on one line of the detail screen's footer. */
#define SKIM_CAP_REASON_MAX 22

/* A short signal label shares its row with "+35 ". */
#define SKIM_SIGNAL_SHORT_MAX 20

/* How many passes a device has to survive before it counts as a fixture.
 * Roughly half a minute of inquiry: long enough that the customer at the next
 * pump has driven off, short enough to do before you take your card out. */
#define SKIM_FIXED_PASSES 3

typedef enum {
    SkimSignalName = 0, /* the name is a known serial-bridge module */
    SkimSignalOui, /* the address belongs to a module maker */
    SkimSignalOuiOdd, /* date-coded or self-assigned BR/EDR address */
    SkimSignalCod, /* it declares no device class at all */
    SkimSignalNameless, /* discoverable BR/EDR with no name to give */
    SkimSignalFixed, /* still there several passes later */
    SkimSignalClose, /* strong enough to be at this pump, not across the lot */
    SkimSignalCount,
} SkimSignal;

/* Which question a signal answers. Two signals in one family are one opinion
 * told twice; the caps below are built on that. */
typedef enum {
    SkimFamilyIdentity = 0, /* who does it say it is */
    SkimFamilyDeclaration, /* what does it say it does */
    SkimFamilyBehaviour, /* how does it behave over time */
    SkimFamilyCount,
} SkimFamily;

typedef enum {
    SkimVerdictClear = 0, /* nothing worth reporting */
    SkimVerdictNote, /* one thing stood out */
    SkimVerdictSuspect, /* several things line up */
    SkimVerdictLikely, /* the full picture of a bridge module bolted to a pump */
    SkimVerdictCount,
} SkimVerdict;

/** Everything the scorer is allowed to look at. Nothing else exists to it. */
typedef struct {
    SkimSighting last; /* the most recent sighting */
    int8_t best_rssi; /* strongest level seen across all passes */
    uint16_t passes; /* how many sweep passes it turned up in */
    uint8_t close_dbm; /* "close" threshold as a magnitude: 70 means -70 dBm */
} SkimEvidence;

typedef struct {
    uint8_t score; /* 0..100, after the caps */
    uint8_t raw; /* what the signals added up to before the caps */
    SkimVerdict verdict;
    uint16_t fired; /* bit set of SkimSignal */
    uint8_t points[SkimSignalCount]; /* what each signal contributed */
    uint8_t families; /* how many of the three fired */
    const char* cap_reason; /* NULL, or why the score was held down */
    const SkimNameSig* name_sig; /* the matched module, or NULL */
    SkimOuiInfo oui;
    SkimCodClass cod;
} SkimAssessment;

/** Score one device. `out` is fully written; `ev` is not modified. */
void skim_score(const SkimEvidence* ev, SkimAssessment* out);

/** True if signal `s` fired in this assessment. */
bool skim_fired(const SkimAssessment* a, SkimSignal s);

/* --- naming, so the views and the log never invent their own wording --- */

const char* skim_verdict_name(SkimVerdict v); /* "SUSPECT" */
const char* skim_verdict_line(SkimVerdict v); /* two lines: what it means */
const char* skim_verdict_advice(SkimVerdict v); /* up to three lines: what to do */
const char* skim_signal_label(SkimSignal s); /* "Known module name" */
const char* skim_signal_short(SkimSignal s); /* "Module name" -- fits a row */
const char* skim_signal_why(SkimSignal s); /* why it counts, four short lines */
SkimFamily skim_signal_family(SkimSignal s);
const char* skim_family_name(SkimFamily f);
uint8_t skim_signal_weight(SkimSignal s);

/** Band edges, exposed so the score bar can draw its ticks in the right place
 *  rather than hard-coding numbers that drift away from the engine. */
uint8_t skim_verdict_floor(SkimVerdict v);

/** The verdict for a whole sweep is the worst device in it -- but a sweep that
 *  found nothing is never called clean, only *quiet*. */
const char* skim_sweep_headline(SkimVerdict worst, bool any_devices);
const char* skim_sweep_subline(SkimVerdict worst, bool any_devices);
