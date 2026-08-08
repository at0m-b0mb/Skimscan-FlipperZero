#include "skim_link.h"

#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <expansion/expansion.h>
#include <string.h>
#include <stdlib.h>

#define LINK_BAUD 115200
#define LINK_RX_STREAM 1024
#define LINK_LINE_MAX 128
#define LINK_STACK 2048
#define LINK_FW_MAX 12

const char* const skim_port_labels[SkimPortCount] = {"USART", "LPUART"};
const char* const skim_port_pins[SkimPortCount] = {"TX 13 / RX 14", "TX 15 / RX 16"};

struct SkimLink {
    FuriThread* thread;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    Expansion* expansion;
    volatile bool running;
    volatile bool online;
    volatile uint8_t caps;
    char firmware[LINK_FW_MAX + 1];

    SkimLinkDeviceCallback device_cb;
    SkimLinkPassCallback pass_cb;
    SkimLinkStatusCallback status_cb;
    SkimLinkStateCallback state_cb;
    void* cb_context;
};

SkimLink* skim_link_alloc(void) {
    SkimLink* link = malloc(sizeof(SkimLink));
    memset(link, 0, sizeof(SkimLink));
    return link;
}

void skim_link_free(SkimLink* link) {
    furi_assert(link);
    skim_link_stop(link);
    free(link);
}

void skim_link_set_callbacks(
    SkimLink* link,
    SkimLinkDeviceCallback device_cb,
    SkimLinkPassCallback pass_cb,
    SkimLinkStatusCallback status_cb,
    SkimLinkStateCallback state_cb,
    void* context) {
    furi_assert(link);
    link->device_cb = device_cb;
    link->pass_cb = pass_cb;
    link->status_cb = status_cb;
    link->state_cb = state_cb;
    link->cb_context = context;
}

/* ---------------- parsing ---------------- */

static uint8_t hex_nibble(char c) {
    if(c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if(c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if(c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFF;
}

static bool parse_mac(const char* s, uint8_t mac[6]) {
    if(strlen(s) < 12) return false;
    for(int i = 0; i < 6; i++) {
        uint8_t hi = hex_nibble(s[i * 2]);
        uint8_t lo = hex_nibble(s[i * 2 + 1]);
        if(hi == 0xFF || lo == 0xFF) return false;
        mac[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parse_hex24(const char* s, uint32_t* out) {
    uint32_t v = 0;
    int n = 0;
    for(const char* p = s; *p; p++, n++) {
        uint8_t d = hex_nibble(*p);
        if(d == 0xFF) return false;
        v = (v << 4) | d;
        if(n > 7) return false;
    }
    if(n == 0) return false;
    *out = v & 0x00FFFFFFUL;
    return true;
}

/* Split in place into up to `max` comma-separated tokens; the last token keeps
 * whatever remains, which is how a device name is allowed to be anything. */
static size_t split_csv(char* s, char** tok, size_t max) {
    if(max == 0) return 0;
    size_t n = 0;
    tok[n++] = s;
    char* p = s;
    while(n < max) {
        char* c = strchr(p, ',');
        if(!c) break;
        *c = '\0';
        p = c + 1;
        tok[n++] = p;
    }
    return n;
}

static void copy_name(char* dst, const char* src) {
    size_t n = 0;
    if(src) {
        for(const char* p = src; *p && n < SKIM_NAME_MAX; p++) {
            /* Anything unprintable would corrupt the row it is drawn into. */
            dst[n++] = (*p >= 0x20 && *p < 0x7F) ? *p : '?';
        }
    }
    dst[n] = '\0';
}

static void link_parse_line(SkimLink* link, char* line) {
    if(strncmp(line, "SKD,", 4) == 0) {
        char* tok[5];
        size_t n = split_csv(line + 4, tok, 5);
        if(n < 4) return; /* the name is optional, the rest is not */

        SkimSighting s;
        memset(&s, 0, sizeof(s));
        if(!parse_mac(tok[0], s.mac)) return;
        s.rssi = (int8_t)atoi(tok[1]);
        uint32_t cod = 0;
        s.cod = parse_hex24(tok[2], &cod) ? cod : SKIM_COD_NONE;
        s.radio = (atoi(tok[3]) == 1) ? SkimRadioLe : SkimRadioClassic;
        copy_name(s.name, (n >= 5) ? tok[4] : "");
        if(link->device_cb) link->device_cb(link->cb_context, &s);

    } else if(strncmp(line, "SKPASS,", 7) == 0) {
        if(link->pass_cb) link->pass_cb(link->cb_context, (uint16_t)atoi(line + 7));

    } else if(strncmp(line, "SKSTATE,", 8) == 0) {
        if(link->state_cb) link->state_cb(link->cb_context, atoi(line + 8) != 0);

    } else if(strncmp(line, "SKHELLO,", 8) == 0) {
        char* tok[2];
        size_t n = split_csv(line + 8, tok, 2);
        size_t len = strlen(tok[0]);
        if(len > LINK_FW_MAX) len = LINK_FW_MAX;
        memcpy(link->firmware, tok[0], len);
        link->firmware[len] = '\0';
        /* A companion that does not declare its radios is assumed to have
         * both, because that is what the shipped firmware builds. */
        link->caps = (n >= 2) ? (uint8_t)atoi(tok[1]) : (SKIM_CAP_CLASSIC | SKIM_CAP_LE);
        link->online = true;
        if(link->status_cb) link->status_cb(link->cb_context, link->firmware, link->caps);
    }
}

/* ---------------- worker / ISR ---------------- */

static int32_t link_worker(void* context) {
    SkimLink* link = context;
    char line[LINK_LINE_MAX];
    size_t pos = 0;
    uint8_t buf[64];

    while(link->running) {
        size_t got = furi_stream_buffer_receive(link->rx_stream, buf, sizeof(buf), 50);
        for(size_t i = 0; i < got; i++) {
            char c = (char)buf[i];
            if(c == '\n' || c == '\r') {
                if(pos > 0) {
                    line[pos] = '\0';
                    link_parse_line(link, line);
                    pos = 0;
                }
            } else if(pos < sizeof(line) - 1) {
                line[pos++] = c;
            } else {
                pos = 0; /* overflow: drop the malformed line whole */
            }
        }
    }
    return 0;
}

static void link_rx_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    SkimLink* link = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(link->rx_stream, &data, 1, 0);
    }
}

/* ---------------- control ---------------- */

void skim_link_start(SkimLink* link, SkimPort port) {
    furi_assert(link);
    if(link->running) return;

    /* The Expansion service squats on the USART looking for modules. */
    link->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(link->expansion);

    link->rx_stream = furi_stream_buffer_alloc(LINK_RX_STREAM, 1);
    link->running = true;
    link->online = false;
    link->caps = 0;
    link->firmware[0] = '\0';

    link->thread = furi_thread_alloc_ex("SkimscanUart", LINK_STACK, link_worker, link);
    furi_thread_start(link->thread);

    link->serial = furi_hal_serial_control_acquire(
        (port == SkimPortLpuart) ? FuriHalSerialIdLpuart : FuriHalSerialIdUsart);
    furi_check(link->serial);
    furi_hal_serial_init(link->serial, LINK_BAUD);
    furi_hal_serial_async_rx_start(link->serial, link_rx_isr, link, false);

    skim_link_send(link, "PING\n");
}

void skim_link_stop(SkimLink* link) {
    furi_assert(link);
    if(!link->running) return;

    /* Ask the companion to stand down before the port goes away, so it is not
     * left inquiring into a disconnected wire. */
    skim_link_send(link, "STOP\n");

    link->running = false;

    if(link->serial) {
        furi_hal_serial_async_rx_stop(link->serial);
        furi_hal_serial_deinit(link->serial);
        furi_hal_serial_control_release(link->serial);
        link->serial = NULL;
    }
    if(link->thread) {
        furi_thread_join(link->thread);
        furi_thread_free(link->thread);
        link->thread = NULL;
    }
    if(link->rx_stream) {
        furi_stream_buffer_free(link->rx_stream);
        link->rx_stream = NULL;
    }
    if(link->expansion) {
        expansion_enable(link->expansion);
        furi_record_close(RECORD_EXPANSION);
        link->expansion = NULL;
    }
    link->online = false;
}

bool skim_link_is_running(const SkimLink* link) {
    furi_assert(link);
    return link->running;
}

bool skim_link_is_online(const SkimLink* link) {
    furi_assert(link);
    return link->online;
}

uint8_t skim_link_caps(const SkimLink* link) {
    furi_assert(link);
    return link->caps;
}

const char* skim_link_firmware(const SkimLink* link) {
    furi_assert(link);
    return link->firmware;
}

void skim_link_send(SkimLink* link, const char* cmd) {
    furi_assert(link);
    if(!link->running || !link->serial || !cmd) return;
    furi_hal_serial_tx(link->serial, (const uint8_t*)cmd, strlen(cmd));
}

void skim_link_set_mode(SkimLink* link, uint8_t mode) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "MODE:%u\n", (unsigned)(mode > 2 ? 0 : mode));
    skim_link_send(link, cmd);
}
