#include "skim_score.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * Weights
 *
 * Name and OUI are the two heavy ones because they are the two that are hard
 * to have by accident, and they are independent of each other: a builder who
 * renames the module still cannot change the block its MAC came out of.
 *
 * Name(35) + Oui(25) + Cod(15) + Fixed(15) + Close(10) = 100. That is the
 * only route to a full score, and the caps below still take five off it.
 * ------------------------------------------------------------------ */

static const uint8_t weight[SkimSignalCount] = {
    [SkimSignalName] = 35,
    [SkimSignalOui] = 25,
    [SkimSignalOuiOdd] = 12,
    [SkimSignalCod] = 15,
    [SkimSignalNameless] = 6,
    [SkimSignalFixed] = 15,
    [SkimSignalClose] = 10,
};

static const SkimFamily family_of[SkimSignalCount] = {
    [SkimSignalName] = SkimFamilyIdentity,
    [SkimSignalOui] = SkimFamilyIdentity,
    [SkimSignalOuiOdd] = SkimFamilyIdentity,
    [SkimSignalCod] = SkimFamilyDeclaration,
    [SkimSignalNameless] = SkimFamilyDeclaration,
    [SkimSignalFixed] = SkimFamilyBehaviour,
    [SkimSignalClose] = SkimFamilyBehaviour,
};

/* Band floors. The gaps are deliberate: a device sitting on 39 and a device
 * sitting on 40 should not read as the same thing at a glance. */
static const uint8_t verdict_floor[SkimVerdictCount] = {
    [SkimVerdictClear] = 0,
    [SkimVerdictNote] = 15,
    [SkimVerdictSuspect] = 40,
    [SkimVerdictLikely] = 70,
};

/* ------------------------------------------------------------------ */

static void fire(SkimAssessment* a, SkimSignal s) {
    a->fired |= (uint16_t)(1u << s);
    a->points[s] = weight[s];
}

bool skim_fired(const SkimAssessment* a, SkimSignal s) {
    if(!a || s >= SkimSignalCount) return false;
    return (a->fired & (uint16_t)(1u << s)) != 0;
}

static SkimVerdict verdict_for(uint8_t score) {
    if(score >= verdict_floor[SkimVerdictLikely]) return SkimVerdictLikely;
    if(score >= verdict_floor[SkimVerdictSuspect]) return SkimVerdictSuspect;
    if(score >= verdict_floor[SkimVerdictNote]) return SkimVerdictNote;
    return SkimVerdictClear;
}

/* Apply a ceiling, keeping the reason that belongs to the *binding* one. */
static void cap(uint8_t* score, const char** reason, uint8_t limit, const char* why) {
    if(*score > limit) {
        *score = limit;
        *reason = why;
    }
}

void skim_score(const SkimEvidence* ev, SkimAssessment* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    if(!ev) return;

    const SkimSighting* s = &ev->last;
    out->name_sig = skim_sig_name_lookup(s->name);
    out->oui = skim_sig_oui_lookup(s->mac, s->radio);
    out->cod = skim_sig_cod_class(s->cod);

    /* --- identity --- */
    if(out->name_sig) fire(out, SkimSignalName);
    if(out->oui.klass == SkimOuiModule) {
        fire(out, SkimSignalOui);
    } else if(out->oui.klass == SkimOuiDateCoded || out->oui.klass == SkimOuiLocal) {
        fire(out, SkimSignalOuiOdd);
    }

    /* --- declaration --- */
    if(out->cod == SkimCodUncategorised) fire(out, SkimSignalCod);
    /* An LE beacon with no name is a beacon doing its job. A discoverable
     * BR/EDR device with no name has gone out of its way to be findable and
     * then declined to say what it is. Only the second is worth anything. */
    if(s->name[0] == '\0' && s->radio == SkimRadioClassic) fire(out, SkimSignalNameless);

    /* --- behaviour --- */
    if(ev->passes >= SKIM_FIXED_PASSES) fire(out, SkimSignalFixed);
    if(ev->best_rssi >= -(int8_t)ev->close_dbm) fire(out, SkimSignalClose);

    /* --- add up --- */
    uint16_t raw = 0;
    bool family_hit[SkimFamilyCount] = {false, false, false};
    for(int i = 0; i < SkimSignalCount; i++) {
        if(!skim_fired(out, (SkimSignal)i)) continue;
        raw += out->points[i];
        family_hit[family_of[i]] = true;
    }
    if(raw > 100) raw = 100;
    out->raw = (uint8_t)raw;

    out->families = 0;
    for(int f = 0; f < SkimFamilyCount; f++)
        if(family_hit[f]) out->families++;

    /* --- caps ---
     *
     * These are the whole design. Without them the engine is a keyword search
     * that shouts SKIMMER at anyone's robot kit, and one bad call in a petrol
     * station forecourt is worse than ten quiet ones.
     *
     * Reasons are kept to SKIM_CAP_REASON_MAX characters because they are
     * printed on one line of the detail screen, and a reason the user cannot
     * read is the same as no reason at all. The tests hold the length.
     */
    uint8_t score = out->raw;
    const char* reason = NULL;

    /* Behaviour on its own describes the furniture. The speaker in the shop
     * and the car wash controller are close by and never move, and if that
     * were worth reporting then everything would be, which is the same as
     * nothing being. Behaviour qualifies other evidence; it is not evidence. */
    if(!family_hit[SkimFamilyIdentity] && !family_hit[SkimFamilyDeclaration]) {
        cap(&score, &reason, verdict_floor[SkimVerdictNote] - 1, "only sitting there");
    }
    if(!family_hit[SkimFamilyIdentity]) {
        cap(&score, &reason, verdict_floor[SkimVerdictSuspect] - 1, "no module identity");
    }
    if(out->families < 2) {
        cap(&score, &reason, verdict_floor[SkimVerdictSuspect] - 1, "one kind of evidence");
    }
    if(out->families < 3) {
        cap(&score, &reason, verdict_floor[SkimVerdictLikely] - 1, "needs a 3rd signal");
    }
    if(ev->passes < 2) {
        cap(&score, &reason, verdict_floor[SkimVerdictLikely] - 1, "seen once, sweep again");
    }
    cap(&score, &reason, SKIM_SCORE_CEILING, "never fully certain");

    out->score = score;
    out->cap_reason = reason;
    out->verdict = verdict_for(score);
}

/* ------------------------------------------------------------------ *
 * Wording. Kept here so the sweep screen, the detail page and the CSV can
 * never drift into describing the same number three different ways.
 * ------------------------------------------------------------------ */

uint8_t skim_signal_weight(SkimSignal s) {
    return (s < SkimSignalCount) ? weight[s] : 0;
}

SkimFamily skim_signal_family(SkimSignal s) {
    return (s < SkimSignalCount) ? family_of[s] : SkimFamilyIdentity;
}

uint8_t skim_verdict_floor(SkimVerdict v) {
    return (v < SkimVerdictCount) ? verdict_floor[v] : 0;
}

const char* skim_family_name(SkimFamily f) {
    switch(f) {
    case SkimFamilyIdentity:
        return "identity";
    case SkimFamilyDeclaration:
        return "declaration";
    case SkimFamilyBehaviour:
        return "behaviour";
    default:
        return "?";
    }
}

const char* skim_verdict_name(SkimVerdict v) {
    switch(v) {
    case SkimVerdictClear:
        return "ORDINARY";
    case SkimVerdictNote:
        return "NOTE";
    case SkimVerdictSuspect:
        return "SUSPECT";
    case SkimVerdictLikely:
        return "SKIMMER?";
    default:
        return "?";
    }
}

const char* skim_verdict_line(SkimVerdict v) {
    switch(v) {
    case SkimVerdictClear:
        return "Looks like ordinary\nconsumer Bluetooth.";
    case SkimVerdictNote:
        return "One thing stood out.\nProbably nothing.";
    case SkimVerdictSuspect:
        return "Several signals line up.\nWorth a second sweep.";
    case SkimVerdictLikely:
        return "This looks like a bridge\nmodule bolted to something.";
    default:
        return "";
    }
}

const char* skim_verdict_advice(SkimVerdict v) {
    switch(v) {
    case SkimVerdictClear:
        return "Nothing to do. Cover the\nPIN pad anyway - most\nskimmers are not radios.";
    case SkimVerdictNote:
        return "Carry on, but prefer the\nchip or the tap. A stripe\nis the only thing at risk.";
    case SkimVerdictSuspect:
        return "Do not swipe. Use tap or\nchip, or pay inside, and\nsweep once more first.";
    case SkimVerdictLikely:
        return "Pay inside. Tell the staff\nwhich pump. Do not open\nanything or touch wires.";
    default:
        return "";
    }
}

const char* skim_signal_short(SkimSignal s) {
    switch(s) {
    case SkimSignalName:
        return "Module name";
    case SkimSignalOui:
        return "Module MAC";
    case SkimSignalOuiOdd:
        return "Odd MAC";
    case SkimSignalCod:
        return "No class";
    case SkimSignalNameless:
        return "No name";
    case SkimSignalFixed:
        return "Still here";
    case SkimSignalClose:
        return "Close by";
    default:
        return "?";
    }
}

const char* skim_signal_label(SkimSignal s) {
    switch(s) {
    case SkimSignalName:
        return "Known module name";
    case SkimSignalOui:
        return "Module maker's MAC";
    case SkimSignalOuiOdd:
        return "Odd MAC prefix";
    case SkimSignalCod:
        return "Declares no class";
    case SkimSignalNameless:
        return "Findable but nameless";
    case SkimSignalFixed:
        return "Still here, pass after pass";
    case SkimSignalClose:
        return "Close enough to be here";
    default:
        return "?";
    }
}

const char* skim_signal_why(SkimSignal s) {
    switch(s) {
    case SkimSignalName:
        return "The factory name of a\ntwo-dollar serial bridge.\nRenaming it takes one AT\ncommand, so it is a hint.";
    case SkimSignalOui:
        return "The first three bytes of\nthe MAC belong to a firm\nthat makes serial bridge\nmodules and little else.";
    case SkimSignalOuiOdd:
        return "A MAC shaped like a date,\nor one the device chose\nitself. Clone firmware\ndoes this; products do not.";
    case SkimSignalCod:
        return "Real products say what\nthey are: phone, headset,\nlaptop. This left the\nfield blank, as modules do.";
    case SkimSignalNameless:
        return "It made itself findable,\nthen would not say what\nit is. A headset pairing\nhas a name. This has none.";
    case SkimSignalFixed:
        return "It was there pass after\npass. People walk away.\nSomething wired into a\npump does not.";
    case SkimSignalClose:
        return "Loud enough to be inside\nthis pump, not in a car\nacross the forecourt.\nRSSI range is rough.";
    default:
        return "";
    }
}

/* Eight characters is not a style choice: it is how much room the sweep screen
 * has for a headline in the primary font next to the card. A longer word here
 * would run off the panel, so the test suite holds the length. */
const char* skim_sweep_headline(SkimVerdict worst, bool any_devices) {
    if(!any_devices) return "QUIET";
    if(worst <= SkimVerdictClear) return "NO MATCH";
    return skim_verdict_name(worst);
}

const char* skim_sweep_subline(SkimVerdict worst, bool any_devices) {
    if(!any_devices) return "No Bluetooth at all.\nCheck the companion is on.";
    switch(worst) {
    case SkimVerdictClear:
        return "Nothing here matches a\nknown skimmer radio.";
    case SkimVerdictNote:
        return "One device stood out.\nOpen it and see why.";
    case SkimVerdictSuspect:
        return "Pay inside or use the tap.\nSweep again to confirm.";
    case SkimVerdictLikely:
        return "Do not swipe. Pay inside\nand tell the station.";
    default:
        return "";
    }
}
