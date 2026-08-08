/* The link to the companion radio.
 *
 * The Flipper's own Bluetooth stack advertises; it cannot inquire, and it has
 * no BR/EDR at all. Classic inquiry is where the skimmer modules live, so the
 * radio has to be an ESP32 on the GPIO header and this is the wire between
 * them: an RX worker, a line parser, and four commands going the other way.
 *
 * Wire protocol (companion -> Flipper), one line each, LF terminated:
 *   SKHELLO,<fw>,<caps>              caps bit0 = BR/EDR, bit1 = LE
 *   SKD,<mac12hex>,<rssi>,<cod|->,<radio>,<name>
 *   SKPASS,<n>                       an inquiry cycle completed
 *   SKSTATE,<0|1>                    idle / scanning
 *
 * Flipper -> companion:
 *   START | STOP | PING | MODE:<0|1|2>       0 both, 1 BR/EDR only, 2 LE only
 */
#pragma once

#include "skim_sigs.h"

#include <furi.h>

typedef struct SkimLink SkimLink;

typedef void (*SkimLinkDeviceCallback)(void* context, const SkimSighting* sighting);
typedef void (*SkimLinkPassCallback)(void* context, uint16_t pass);
typedef void (*SkimLinkStatusCallback)(void* context, const char* fw, uint8_t caps);
typedef void (*SkimLinkStateCallback)(void* context, bool scanning);

/* Which port the companion is wired to. The default board sits on the
 * standard USART pins; boards that also carry GPS want the LPUART so 13/14
 * stay free. */
typedef enum {
    SkimPortUsart = 0, /* TX 13 / RX 14 */
    SkimPortLpuart = 1, /* TX 15 / RX 16 */
    SkimPortCount,
} SkimPort;

extern const char* const skim_port_labels[SkimPortCount];
extern const char* const skim_port_pins[SkimPortCount];

/* Capability bits reported in SKHELLO. */
#define SKIM_CAP_CLASSIC 0x01
#define SKIM_CAP_LE 0x02

SkimLink* skim_link_alloc(void);
void skim_link_free(SkimLink* link);

void skim_link_set_callbacks(
    SkimLink* link,
    SkimLinkDeviceCallback device_cb,
    SkimLinkPassCallback pass_cb,
    SkimLinkStatusCallback status_cb,
    SkimLinkStateCallback state_cb,
    void* context);

void skim_link_start(SkimLink* link, SkimPort port);
void skim_link_stop(SkimLink* link);
bool skim_link_is_running(const SkimLink* link);

/** True once a SKHELLO has arrived: the companion is real and talking. */
bool skim_link_is_online(const SkimLink* link);
uint8_t skim_link_caps(const SkimLink* link);
const char* skim_link_firmware(const SkimLink* link);

void skim_link_send(SkimLink* link, const char* cmd);
void skim_link_set_mode(SkimLink* link, uint8_t mode);
