#include "skim_db.h"

#include <string.h>

struct SkimDb {
    FuriMutex* mutex;
    SkimDevice dev[SKIM_MAX_DEVICES];
    size_t count;
    uint16_t pass;
    uint8_t close_dbm;
    /* Worst-first ranking, rebuilt lazily so a busy forecourt does not sort
     * the table once per inquiry response. */
    uint8_t order[SKIM_MAX_DEVICES];
    bool order_stale;
};

SkimDb* skim_db_alloc(void) {
    SkimDb* db = malloc(sizeof(SkimDb));
    memset(db, 0, sizeof(SkimDb));
    db->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    db->pass = 1;
    db->close_dbm = 70;
    return db;
}

void skim_db_free(SkimDb* db) {
    furi_assert(db);
    furi_mutex_free(db->mutex);
    free(db);
}

void skim_db_reset(SkimDb* db) {
    furi_assert(db);
    furi_mutex_acquire(db->mutex, FuriWaitForever);
    memset(db->dev, 0, sizeof(db->dev));
    db->count = 0;
    db->pass = 1;
    db->order_stale = true;
    furi_mutex_release(db->mutex);
}

void skim_db_set_close_dbm(SkimDb* db, uint8_t close_dbm) {
    furi_assert(db);
    furi_mutex_acquire(db->mutex, FuriWaitForever);
    if(db->close_dbm != close_dbm) {
        db->close_dbm = close_dbm;
        /* The threshold moved, so every verdict in the table is out of date. */
        for(size_t i = 0; i < db->count; i++) {
            SkimEvidence ev = {
                .last = db->dev[i].last,
                .best_rssi = db->dev[i].best_rssi,
                .passes = db->dev[i].passes,
                .close_dbm = close_dbm,
            };
            skim_score(&ev, &db->dev[i].assess);
        }
        db->order_stale = true;
    }
    furi_mutex_release(db->mutex);
}

/* ------------------------------------------------------------------ */

static int find_locked(const SkimDb* db, const uint8_t mac[6]) {
    for(size_t i = 0; i < db->count; i++) {
        if(memcmp(db->dev[i].last.mac, mac, 6) == 0) return (int)i;
    }
    return -1;
}

/* When the table is full, the device to lose is the quietest one that has
 * scored nothing -- never a flagged device, whatever its age. */
static int evict_locked(const SkimDb* db) {
    int worst = -1;
    for(size_t i = 0; i < db->count; i++) {
        if(db->dev[i].assess.verdict > SkimVerdictClear) continue;
        if(worst < 0 || db->dev[i].best_rssi < db->dev[worst].best_rssi) worst = (int)i;
    }
    return worst;
}

bool skim_db_observe(SkimDb* db, const SkimSighting* s) {
    furi_assert(db);
    if(!s) return false;

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    int idx = find_locked(db, s->mac);
    if(idx < 0) {
        if(db->count < SKIM_MAX_DEVICES) {
            idx = (int)db->count++;
        } else {
            idx = evict_locked(db);
            if(idx < 0) {
                /* Every slot is flagged. Losing one of those to make room for
                 * a newcomer would be the wrong trade. */
                furi_mutex_release(db->mutex);
                return false;
            }
        }
        memset(&db->dev[idx], 0, sizeof(SkimDevice));
        db->dev[idx].first_tick = furi_get_tick();
        db->dev[idx].best_rssi = -128;
        db->dev[idx].last_pass = 0;
    }

    SkimDevice* d = &db->dev[idx];
    SkimVerdict before = d->assess.verdict;

    d->last = *s;
    d->last_tick = furi_get_tick();
    d->sightings++;
    if(s->rssi > d->best_rssi) d->best_rssi = s->rssi;
    /* One pass, one count -- an inquiry cycle that hears the same module six
     * times has still only seen it once. */
    if(d->last_pass != db->pass) {
        d->last_pass = db->pass;
        if(d->passes < 0xFFFF) d->passes++;
    }

    SkimEvidence ev = {
        .last = d->last,
        .best_rssi = d->best_rssi,
        .passes = d->passes,
        .close_dbm = db->close_dbm,
    };
    skim_score(&ev, &d->assess);

    bool raised = d->assess.verdict > before;
    db->order_stale = true;

    furi_mutex_release(db->mutex);
    return raised;
}

void skim_db_next_pass(SkimDb* db) {
    furi_assert(db);
    furi_mutex_acquire(db->mutex, FuriWaitForever);
    if(db->pass < 0xFFFF) db->pass++;
    furi_mutex_release(db->mutex);
}

uint16_t skim_db_pass(const SkimDb* db) {
    furi_assert(db);
    SkimDb* d = (SkimDb*)db;
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    uint16_t p = d->pass;
    furi_mutex_release(d->mutex);
    return p;
}

size_t skim_db_count(const SkimDb* db) {
    furi_assert(db);
    SkimDb* d = (SkimDb*)db;
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    size_t n = d->count;
    furi_mutex_release(d->mutex);
    return n;
}

size_t skim_db_flagged(const SkimDb* db) {
    furi_assert(db);
    SkimDb* d = (SkimDb*)db;
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    size_t n = 0;
    for(size_t i = 0; i < d->count; i++)
        if(d->dev[i].assess.verdict > SkimVerdictClear) n++;
    furi_mutex_release(d->mutex);
    return n;
}

/* Insertion sort over an index array: worst score first, then loudest, then
 * oldest. At thirty-two entries this is far cheaper than keeping it sorted. */
static void reorder_locked(SkimDb* db) {
    if(!db->order_stale) return;
    for(size_t i = 0; i < db->count; i++) db->order[i] = (uint8_t)i;

    for(size_t i = 1; i < db->count; i++) {
        uint8_t key = db->order[i];
        size_t j = i;
        while(j > 0) {
            const SkimDevice* a = &db->dev[db->order[j - 1]];
            const SkimDevice* b = &db->dev[key];
            bool swap = false;
            if(b->assess.score != a->assess.score) {
                swap = b->assess.score > a->assess.score;
            } else if(b->best_rssi != a->best_rssi) {
                swap = b->best_rssi > a->best_rssi;
            } else {
                swap = b->first_tick < a->first_tick;
            }
            if(!swap) break;
            db->order[j] = db->order[j - 1];
            j--;
        }
        db->order[j] = key;
    }
    db->order_stale = false;
}

bool skim_db_get(const SkimDb* db, size_t rank, SkimDevice* out) {
    furi_assert(db);
    SkimDb* d = (SkimDb*)db;
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    bool ok = false;
    reorder_locked(d);
    if(rank < d->count) {
        if(out) *out = d->dev[d->order[rank]];
        ok = true;
    }
    furi_mutex_release(d->mutex);
    return ok;
}

bool skim_db_worst(const SkimDb* db, SkimDevice* out) {
    return skim_db_get(db, 0, out);
}

uint16_t skim_device_staleness(const SkimDb* db, const SkimDevice* dev) {
    furi_assert(db);
    if(!dev) return 0;
    uint16_t pass = skim_db_pass(db);
    return (pass > dev->last_pass) ? (uint16_t)(pass - dev->last_pass) : 0;
}
