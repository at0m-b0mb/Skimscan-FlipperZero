/* Signature tables: the three things an inquiry response tells you about a
 * device, and what each of them is worth knowing.
 *
 * A Bluetooth skimmer is not exotic hardware. It is a two-dollar serial-bridge
 * module -- an HC-05 or one of its clones -- soldered across the card reader's
 * data lines and powered from the pump. Those modules ship with a factory name,
 * a MAC out of a handful of blocks, and a class-of-device field nobody ever
 * bothers to fill in. None of that identifies a skimmer on its own. Together,
 * on something that is still there when you sweep again, it is worth a look.
 *
 * This file is deliberately free of Flipper headers so the whole thing can be
 * compiled and tested on the host. See test/.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** Which radio the device was heard on. */
typedef enum {
    SkimRadioClassic = 0, /* BR/EDR inquiry response */
    SkimRadioLe = 1, /* BLE advertisement */
    SkimRadioCount,
} SkimRadio;

/* Long enough for every factory name in the table and then some; a full BT
 * name is up to 248 bytes and the companion clips it before it hits the wire. */
#define SKIM_NAME_MAX 24

/** One observation of one device, exactly as the companion radio saw it. */
typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint32_t cod; /* 24-bit class of device, or SKIM_COD_NONE */
    SkimRadio radio;
    char name[SKIM_NAME_MAX + 1]; /* "" when the device advertised no name */
} SkimSighting;

/* ------------------------------------------------------------------ *
 * Names
 * ------------------------------------------------------------------ */

typedef struct {
    const char* pattern; /* normalised; a trailing '*' makes it a prefix */
    const char* note; /* what the module actually is */
} SkimNameSig;

/** Lower-case and drop everything that is not a letter or a digit.
 *
 * "HC-05", "hc05" and "HC_05" are the same module wearing different
 * punctuation, and clone firmware is inconsistent about which it uses.
 * `out` is always terminated. Returns `out`.
 */
char* skim_name_normalise(const char* name, char* out, size_t out_len);

/** The module signature this name matches, or NULL. Case/punctuation blind. */
const SkimNameSig* skim_sig_name_lookup(const char* name);

/** The whole table, for the About screen and the tests. */
const SkimNameSig* skim_sig_name_table(size_t* count);

/* ------------------------------------------------------------------ *
 * Addresses
 * ------------------------------------------------------------------ */

typedef enum {
    SkimOuiNone = 0, /* an ordinary, unremarkable address */
    SkimOuiModule, /* a block belonging to a serial-bridge module maker */
    SkimOuiDateCoded, /* 20:16:.. and friends -- a MAC shaped like a date */
    SkimOuiLocal, /* locally administered: chosen, not assigned */
} SkimOuiClass;

typedef struct {
    SkimOuiClass klass;
    const char* vendor; /* "" when there is no name to give */
} SkimOuiInfo;

/** Classify a 6-byte address. Never returns SkimOuiLocal for an LE address:
 *  random addresses are the norm there and carry no information at all. */
SkimOuiInfo skim_sig_oui_lookup(const uint8_t mac[6], SkimRadio radio);

/* ------------------------------------------------------------------ *
 * Class of device
 * ------------------------------------------------------------------ */

/** Sentinel for "the device never told us" -- always the case over LE. */
#define SKIM_COD_NONE 0xFFFFFFFFUL

typedef enum {
    SkimCodOrdinary = 0, /* it says what it is: a phone, a headset, a laptop */
    SkimCodUncategorised, /* major class 0x1F, or all zero: the module default */
    SkimCodAbsent, /* not reported (LE, or a stack that withholds it) */
} SkimCodClass;

SkimCodClass skim_sig_cod_class(uint32_t cod);

/** Human name for the major device class: "Phone", "Audio/Video", ... */
const char* skim_sig_cod_name(uint32_t cod);

/* ------------------------------------------------------------------ *
 * Formatting (shared by the views, the log and the tests)
 * ------------------------------------------------------------------ */

/** "98:D3:31:XX:XX:XX" -- needs 18 bytes. */
void skim_mac_str(const uint8_t mac[6], char* out, size_t out_len);

/** "98:D3:31" -- needs 9 bytes. */
void skim_oui_str(const uint8_t mac[6], char* out, size_t out_len);
