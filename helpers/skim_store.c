#include "skim_store.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <string.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define SKIM_SETTINGS_PATH APP_DATA_PATH("settings.bin")
#define SKIM_SETTINGS_MAGIC 0x5C
#define SKIM_SETTINGS_VERSION 1
#define SKIM_LOG_PATH APP_DATA_PATH("sweeps.csv")

const uint8_t skim_close_dbm[SKIM_CLOSE_COUNT] = {60, 70, 80};
const char* const skim_close_labels[SKIM_CLOSE_COUNT] = {"-60 dBm", "-70 dBm", "-80 dBm"};
const char* const skim_mode_labels[SkimModeCount] = {"BR/EDR+LE", "BR/EDR", "LE"};
const char* const skim_log_path_display = "apps_data/skimscan/sweeps.csv";

static void ensure_dir(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);
}

void skim_store_settings_save(const SkimSettings* s) {
    furi_assert(s);
    ensure_dir();
    saved_struct_save(
        SKIM_SETTINGS_PATH, s, sizeof(SkimSettings), SKIM_SETTINGS_MAGIC, SKIM_SETTINGS_VERSION);
}

void skim_store_settings_load(SkimSettings* s) {
    furi_assert(s);
    SkimSettings loaded;
    if(!saved_struct_load(
           SKIM_SETTINGS_PATH,
           &loaded,
           sizeof(SkimSettings),
           SKIM_SETTINGS_MAGIC,
           SKIM_SETTINGS_VERSION)) {
        return; /* nothing valid on the card: the caller keeps its defaults */
    }
    /* Never let a file on the SD card index an array. */
    if(loaded.mode >= SkimModeCount) loaded.mode = SkimModeBoth;
    if(loaded.close_index >= SKIM_CLOSE_COUNT) loaded.close_index = 1;
    if(loaded.port >= SkimPortCount) loaded.port = SkimPortUsart;
    *s = loaded;
}

/* ------------------------------------------------------------------ *
 * The sweep log
 * ------------------------------------------------------------------ */

static void append_str(File* file, const char* s) {
    storage_file_write(file, s, strlen(s));
}

/* The fired signals as a compact field: "name|oui|cod|fixed|close". Reading a
 * score back off a spreadsheet six weeks later is useless without them. */
static void signals_field(const SkimAssessment* a, char* out, size_t len) {
    static const char* const short_name[SkimSignalCount] = {
        "name", "oui", "oddmac", "noclass", "noname", "fixed", "close"};
    size_t n = 0;
    out[0] = '\0';
    for(int s = 0; s < SkimSignalCount; s++) {
        if(!skim_fired(a, (SkimSignal)s)) continue;
        int wrote = snprintf(
            out + n, (n < len) ? len - n : 0, "%s%s", (n > 0) ? "|" : "", short_name[s]);
        if(wrote < 0) break;
        n += (size_t)wrote;
        if(n >= len) {
            out[len - 1] = '\0';
            break;
        }
    }
}

size_t skim_store_log_sweep(const SkimDb* db, const char* site_note) {
    furi_assert(db);

    /* Nothing worth reporting is not worth a row. */
    if(skim_db_flagged(db) == 0) return 0;

    ensure_dir();
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    size_t rows = 0;

    bool fresh = !storage_file_exists(storage, SKIM_LOG_PATH);
    if(storage_file_open(file, SKIM_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        if(fresh) {
            append_str(
                file,
                "when,site,mac,name,radio,class,rssi_dbm,passes,score,verdict,signals\r\n");
        }

        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);

        char line[256];
        char mac[20];
        char sigs[64];

        for(size_t rank = 0;; rank++) {
            SkimDevice d;
            if(!skim_db_get(db, rank, &d)) break;
            if(d.assess.verdict == SkimVerdictClear) break; /* sorted worst-first */

            skim_mac_str(d.last.mac, mac, sizeof(mac));
            signals_field(&d.assess, sigs, sizeof(sigs));

            snprintf(
                line,
                sizeof(line),
                "%04u-%02u-%02u %02u:%02u:%02u,%s,%s,%s,%s,%s,%d,%u,%u,%s,%s\r\n",
                (unsigned)dt.year,
                (unsigned)dt.month,
                (unsigned)dt.day,
                (unsigned)dt.hour,
                (unsigned)dt.minute,
                (unsigned)dt.second,
                site_note ? site_note : "",
                mac,
                d.last.name[0] ? d.last.name : "(none)",
                (d.last.radio == SkimRadioLe) ? "LE" : "BR/EDR",
                skim_sig_cod_name(d.last.cod),
                (int)d.best_rssi,
                (unsigned)d.passes,
                (unsigned)d.assess.score,
                skim_verdict_name(d.assess.verdict),
                sigs);
            append_str(file, line);
            rows++;
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return rows;
}
