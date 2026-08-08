#include "skim_sigs.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * Names
 *
 * These are factory defaults. A skimmer builder who renames the module
 * defeats this table completely -- which is exactly why a name match on its
 * own is capped well below a verdict in skim_score.c. It is one signal.
 * ------------------------------------------------------------------ */

static const SkimNameSig name_table[] = {
    /* --- BR/EDR serial bridges: the classic skimmer radio --- */
    {"hc03*", "HC-03 serial bridge"},
    {"hc04*", "HC-04 serial bridge"},
    {"hc05*", "HC-05 serial bridge"},
    {"hc06*", "HC-06 serial bridge"},
    {"hc42*", "HC-42 serial bridge"},
    {"hc20100601", "HC-06 factory default name"},
    {"linvor", "HC-06 factory firmware"},
    {"bt04a", "BT-04A serial bridge"},
    {"bt05", "BT-05 serial bridge"},
    {"bt06", "BT-06 serial bridge"},
    {"zs040", "ZS-040 breakout board"},
    {"jdy30", "JDY-30 serial bridge"},
    {"jdy31", "JDY-31 serial bridge"},
    {"jdy32", "JDY-32 serial bridge"},
    {"jdy33", "JDY-33 serial bridge"},
    {"sppca", "SPP-CA serial bridge"},
    {"rnbt*", "Roving Networks RN-41/42"},
    {"firefly*", "Roving Networks RN-42"},
    {"bolutek*", "Bolutek module"},
    {"czhc05*", "HC-05 clone"},
    {"btbee*", "BTBee serial bridge"},
    {"iteadbt*", "ITEAD serial bridge"},
    {"btm*", "Rayson BTM serial bridge"},

    /* --- BLE bridges: the newer, smaller, cheaper generation --- */
    {"hm10*", "HM-10 BLE bridge"},
    {"hm11*", "HM-11 BLE bridge"},
    {"hmsoft*", "HM-1x factory firmware"},
    {"at09", "AT-09 BLE bridge"},
    {"mltbt05", "MLT-BT05 BLE bridge"},
    {"cc41a", "CC41-A BLE bridge"},
    {"jdy08", "JDY-08 BLE bridge"},
    {"jdy09", "JDY-09 BLE bridge"},
    {"jdy10", "JDY-10 BLE bridge"},
    {"jdy16", "JDY-16 BLE bridge"},
    {"jdy23", "JDY-23 BLE bridge"},
    {"dsdtech*", "DSD TECH BLE bridge"},
};

#define NAME_TABLE_LEN (sizeof(name_table) / sizeof(name_table[0]))

char* skim_name_normalise(const char* name, char* out, size_t out_len) {
    size_t n = 0;
    if(out_len == 0) return out;
    if(name) {
        for(const char* p = name; *p && n + 1 < out_len; p++) {
            char c = *p;
            if(c >= 'A' && c <= 'Z') {
                out[n++] = (char)(c - 'A' + 'a');
            } else if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out[n++] = c;
            }
            /* everything else -- '-', '_', ' ', '.' -- is punctuation noise */
        }
    }
    out[n] = '\0';
    return out;
}

const SkimNameSig* skim_sig_name_lookup(const char* name) {
    char norm[SKIM_NAME_MAX * 2 + 1];
    skim_name_normalise(name, norm, sizeof(norm));
    if(norm[0] == '\0') return NULL;

    for(size_t i = 0; i < NAME_TABLE_LEN; i++) {
        const char* pat = name_table[i].pattern;
        size_t len = strlen(pat);
        if(len > 0 && pat[len - 1] == '*') {
            if(strncmp(norm, pat, len - 1) == 0) return &name_table[i];
        } else if(strcmp(norm, pat) == 0) {
            return &name_table[i];
        }
    }
    return NULL;
}

const SkimNameSig* skim_sig_name_table(size_t* count) {
    if(count) *count = NAME_TABLE_LEN;
    return name_table;
}

/* ------------------------------------------------------------------ *
 * Addresses
 *
 * Two-byte families first, because the module makers hold whole runs of
 * consecutive blocks and listing every one of them would be a page of
 * duplicated rows that still missed the next one.
 * ------------------------------------------------------------------ */

typedef struct {
    uint8_t b0, b1;
    const char* vendor;
} OuiFamily;

/* Shenzhen Bolutek's 98:D3:xx runs are, in practice, the HC-05/HC-06 clone
 * population: the same die in a hundred differently-silkscreened breakouts. */
static const OuiFamily oui_family[] = {
    {0x98, 0xD3, "Bolutek"},
};

typedef struct {
    uint8_t b0, b1, b2;
    const char* vendor;
} OuiExact;

static const OuiExact oui_exact[] = {
    {0x00, 0x06, 0x66, "Roving Networks"}, /* RN-41 / RN-42, "FireFly" */
    {0x00, 0x12, 0x6F, "Rayson"}, /* BTM-1xx serial modules */
};

#define OUI_FAMILY_LEN (sizeof(oui_family) / sizeof(oui_family[0]))
#define OUI_EXACT_LEN (sizeof(oui_exact) / sizeof(oui_exact[0]))

SkimOuiInfo skim_sig_oui_lookup(const uint8_t mac[6], SkimRadio radio) {
    SkimOuiInfo info = {SkimOuiNone, ""};
    if(!mac) return info;

    for(size_t i = 0; i < OUI_EXACT_LEN; i++) {
        if(mac[0] == oui_exact[i].b0 && mac[1] == oui_exact[i].b1 &&
           mac[2] == oui_exact[i].b2) {
            info.klass = SkimOuiModule;
            info.vendor = oui_exact[i].vendor;
            return info;
        }
    }
    for(size_t i = 0; i < OUI_FAMILY_LEN; i++) {
        if(mac[0] == oui_family[i].b0 && mac[1] == oui_family[i].b1) {
            info.klass = SkimOuiModule;
            info.vendor = oui_family[i].vendor;
            return info;
        }
    }

    /* A MAC shaped like a year and a month. Unbranded clone firmware ships
     * with these instead of buying a block, so 20:15:.. through 20:19:.. turn
     * up on an implausible number of loose modules. IEEE does assign real
     * OUIs in that range, so this is worth a nudge and not much more. */
    if(mac[0] == 0x20 && mac[1] >= 0x13 && mac[1] <= 0x19) {
        info.klass = SkimOuiDateCoded;
        info.vendor = "date-coded";
        return info;
    }
    /* All zeroes is not an address, it is an uninitialised buffer. */
    if(mac[0] == 0 && mac[1] == 0 && mac[2] == 0) {
        info.klass = SkimOuiDateCoded;
        info.vendor = "null prefix";
        return info;
    }

    /* Bit 1 of the first octet: the address was chosen rather than assigned.
     * On BR/EDR that is unusual. On LE it is how every modern phone behaves,
     * so it says nothing and is not reported. */
    if(radio == SkimRadioClassic && (mac[0] & 0x02)) {
        info.klass = SkimOuiLocal;
        info.vendor = "self-assigned";
        return info;
    }

    return info;
}

/* ------------------------------------------------------------------ *
 * Class of device
 * ------------------------------------------------------------------ */

SkimCodClass skim_sig_cod_class(uint32_t cod) {
    if(cod == SKIM_COD_NONE) return SkimCodAbsent;
    /* No services, no class, no minor: the field was never written. */
    if((cod & 0x00FFFFFFUL) == 0) return SkimCodUncategorised;
    uint32_t major = (cod >> 8) & 0x1F;
    if(major == 0x1F) return SkimCodUncategorised;
    return SkimCodOrdinary;
}

const char* skim_sig_cod_name(uint32_t cod) {
    if(cod == SKIM_COD_NONE) return "not reported";
    if((cod & 0x00FFFFFFUL) == 0) return "Uncategorised";
    switch((cod >> 8) & 0x1F) {
    case 0x00:
        return "Miscellaneous";
    case 0x01:
        return "Computer";
    case 0x02:
        return "Phone";
    case 0x03:
        return "Network AP";
    case 0x04:
        return "Audio/Video";
    case 0x05:
        return "Peripheral";
    case 0x06:
        return "Imaging";
    case 0x07:
        return "Wearable";
    case 0x08:
        return "Toy";
    case 0x09:
        return "Health";
    case 0x1F:
    default:
        return "Uncategorised";
    }
}

/* ------------------------------------------------------------------ *
 * Formatting
 * ------------------------------------------------------------------ */

static const char HEX[] = "0123456789ABCDEF";

void skim_mac_str(const uint8_t mac[6], char* out, size_t out_len) {
    if(!out || out_len == 0) return;
    if(!mac || out_len < 18) {
        out[0] = '\0';
        return;
    }
    size_t n = 0;
    for(int i = 0; i < 6; i++) {
        out[n++] = HEX[mac[i] >> 4];
        out[n++] = HEX[mac[i] & 0x0F];
        if(i < 5) out[n++] = ':';
    }
    out[n] = '\0';
}

void skim_oui_str(const uint8_t mac[6], char* out, size_t out_len) {
    if(!out || out_len == 0) return;
    if(!mac || out_len < 9) {
        out[0] = '\0';
        return;
    }
    size_t n = 0;
    for(int i = 0; i < 3; i++) {
        out[n++] = HEX[mac[i] >> 4];
        out[n++] = HEX[mac[i] & 0x0F];
        if(i < 2) out[n++] = ':';
    }
    out[n] = '\0';
}
