/*
 * Skimscan - ESP32 companion firmware
 * -----------------------------------
 * The Flipper Zero's Bluetooth stack can advertise but cannot inquire, and it
 * has no BR/EDR radio at all. Skimmer modules live on BR/EDR. So this board is
 * the radio: it runs a general inquiry, then an LE scan, and streams what it
 * heard to the Flipper over UART. All of the judgement happens on the Flipper.
 *
 * Hardware : a CLASSIC ESP32 (ESP32-WROOM-32 or similar).
 *            ESP32-S2, -S3 and -C3 have no Bluetooth Classic and will not do.
 *            The official Flipper WiFi devboard is an S2 - it will not do either.
 *
 * Wiring   : ESP32 UART0  <->  Flipper GPIO, 115200 8N1
 *              Flipper 13 (TX) -> ESP32 RX0
 *              Flipper 14 (RX) <- ESP32 TX0
 *              Flipper  8 (GND) - ESP32 GND
 *              Flipper  1 (5V)  - ESP32 5V / VIN
 *            UART0 is also the USB programming port, so the board talks to
 *            the computer or to the Flipper, never to both at once.
 *
 * Wire protocol (see helpers/skim_link.h on the Flipper side):
 *   ESP32 -> Flipper:
 *     SKHELLO,<fw>,<caps>                        caps bit0 BR/EDR, bit1 LE
 *     SKD,<mac12hex>,<rssi>,<cod6hex|->,<radio>,<name>     radio 0 BR/EDR, 1 LE
 *     SKPASS,<n>                                 a full cycle finished
 *     SKSTATE,<0|1>                              idle / scanning
 *     SKERR,<text>
 *   Flipper -> ESP32:
 *     START | STOP | PING | MODE:<0|1|2>         0 both, 1 BR/EDR, 2 LE
 *
 * One line per device per pass, carrying the strongest level that pass saw.
 * Everything is buffered until the pass ends rather than streamed live, so a
 * busy forecourt cannot flood the UART and the Flipper's pass counter -- which
 * is what tells a fixture from a passer-by -- stays meaningful.
 */

#include <Arduino.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"

#define SKIMSCAN_FW_VERSION "1.0"

/* Inquiry length is in units of 1.28 s. Eight is ~10 s, which is about as
 * short as a general inquiry can be and still reliably hear a module that is
 * only listening for page scans part of the time. */
#define INQUIRY_UNITS 8
#define LE_SCAN_SECONDS 4

#define TABLE_SIZE 48
#define NAME_MAX 24

enum Mode { MODE_BOTH = 0, MODE_CLASSIC = 1, MODE_LE = 2 };
enum Phase { PHASE_IDLE, PHASE_CLASSIC, PHASE_LE };

static Mode mode = MODE_BOTH;
static Phase phase = PHASE_IDLE;
static bool running = false;
static bool ble_params_ready = false;
static uint32_t pass = 0;
static bool bt_ok = false;

struct Entry {
  uint8_t mac[6];
  int8_t rssi;
  uint32_t cod;   /* 0xFFFFFFFF when not reported */
  uint8_t radio;  /* 0 BR/EDR, 1 LE */
  char name[NAME_MAX + 1];
};

static Entry table[TABLE_SIZE];
static int table_len = 0;

/* ------------------------------------------------------------------ *
 * the per-pass table
 * ------------------------------------------------------------------ */

static void table_clear() {
  table_len = 0;
}

/* Merge a sighting in. The strongest level wins, and a name or a class beats
 * not having one -- an inquiry response often arrives in two parts. */
static void table_put(const uint8_t* mac, int8_t rssi, uint32_t cod, uint8_t radio,
                      const char* name) {
  for (int i = 0; i < table_len; i++) {
    if (memcmp(table[i].mac, mac, 6) != 0) continue;
    if (rssi > table[i].rssi) table[i].rssi = rssi;
    if (cod != 0xFFFFFFFFu) table[i].cod = cod;
    if (name && name[0] && table[i].name[0] == '\0') {
      strncpy(table[i].name, name, NAME_MAX);
      table[i].name[NAME_MAX] = '\0';
    }
    return;
  }
  if (table_len >= TABLE_SIZE) return;

  Entry* e = &table[table_len++];
  memcpy(e->mac, mac, 6);
  e->rssi = rssi;
  e->cod = cod;
  e->radio = radio;
  e->name[0] = '\0';
  if (name) {
    strncpy(e->name, name, NAME_MAX);
    e->name[NAME_MAX] = '\0';
  }
}

static void table_flush() {
  for (int i = 0; i < table_len; i++) {
    const Entry* e = &table[i];
    char mac[13];
    static const char* H = "0123456789ABCDEF";
    for (int k = 0; k < 6; k++) {
      mac[k * 2] = H[e->mac[k] >> 4];
      mac[k * 2 + 1] = H[e->mac[k] & 0x0F];
    }
    mac[12] = '\0';

    if (e->cod == 0xFFFFFFFFu) {
      Serial.printf("SKD,%s,%d,-,%u,%s\n", mac, (int)e->rssi, e->radio, e->name);
    } else {
      Serial.printf("SKD,%s,%d,%06X,%u,%s\n", mac, (int)e->rssi,
                    (unsigned)(e->cod & 0xFFFFFF), e->radio, e->name);
    }
  }
}

/* A device name is attacker-controlled text on its way into a CSV and a
 * line-based protocol. Commas and newlines would break both. */
static void sanitise(char* dst, const uint8_t* src, int len) {
  int n = 0;
  for (int i = 0; i < len && n < NAME_MAX; i++) {
    char c = (char)src[i];
    if (c == '\0') break;
    dst[n++] = (c >= 0x20 && c < 0x7F && c != ',') ? c : '_';
  }
  dst[n] = '\0';
}

/* ------------------------------------------------------------------ *
 * BR/EDR
 * ------------------------------------------------------------------ */

static void start_classic() {
  table_clear();
  phase = PHASE_CLASSIC;
  Serial.println("SKSTATE,1");
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQUIRY_UNITS, 0);
}

static void start_le();
static void finish_pass();

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
      int8_t rssi = -127;
      uint32_t cod = 0xFFFFFFFFu;
      char name[NAME_MAX + 1] = {0};

      for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t* p = &param->disc_res.prop[i];
        switch (p->type) {
          case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t*)(p->val);
            break;
          case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t*)(p->val);
            break;
          case ESP_BT_GAP_DEV_PROP_BDNAME:
            sanitise(name, (uint8_t*)p->val, p->len);
            break;
          case ESP_BT_GAP_DEV_PROP_EIR: {
            /* Plenty of modules only put their name in the extended inquiry
             * response, so an empty BDNAME is not an unnamed device. */
            if (name[0]) break;
            uint8_t len = 0;
            uint8_t* eir_name = esp_bt_gap_resolve_eir_data(
                (uint8_t*)p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
            if (!eir_name) {
              eir_name = esp_bt_gap_resolve_eir_data(
                  (uint8_t*)p->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
            }
            if (eir_name && len) sanitise(name, eir_name, len);
            break;
          }
          default:
            break;
        }
      }
      table_put(param->disc_res.bda, rssi, cod, 0, name);
      break;
    }

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        if (!running) {
          phase = PHASE_IDLE;
          Serial.println("SKSTATE,0");
          break;
        }
        if (mode == MODE_BOTH) {
          start_le();
        } else {
          finish_pass();
        }
      }
      break;

    default:
      break;
  }
}

/* ------------------------------------------------------------------ *
 * LE
 * ------------------------------------------------------------------ */

static esp_ble_scan_params_t ble_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static void start_le() {
  phase = PHASE_LE;
  if (mode != MODE_BOTH) table_clear();
  Serial.println("SKSTATE,1");
  if (ble_params_ready) {
    esp_ble_gap_start_scanning(LE_SCAN_SECONDS);
  } else {
    esp_ble_gap_set_scan_params(&ble_params); /* start follows on the callback */
  }
}

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      ble_params_ready = true;
      if (running && phase == PHASE_LE) esp_ble_gap_start_scanning(LE_SCAN_SECONDS);
      break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
      if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
        char name[NAME_MAX + 1] = {0};
        uint8_t len = 0;
        uint8_t* adv_name = esp_ble_resolve_adv_data(
            param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
        if (!adv_name) {
          adv_name = esp_ble_resolve_adv_data(
              param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
        }
        if (adv_name && len) sanitise(name, adv_name, len);
        /* LE has no class of device -- that field is BR/EDR only. */
        table_put(param->scan_rst.bda, (int8_t)param->scan_rst.rssi, 0xFFFFFFFFu, 1, name);

      } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
        finish_pass();
      }
      break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
      if (!running) {
        phase = PHASE_IDLE;
        Serial.println("SKSTATE,0");
      }
      break;

    default:
      break;
  }
}

/* ------------------------------------------------------------------ *
 * the cycle
 * ------------------------------------------------------------------ */

static void finish_pass() {
  table_flush();
  pass++;
  Serial.printf("SKPASS,%lu\n", (unsigned long)pass);

  if (!running) {
    phase = PHASE_IDLE;
    Serial.println("SKSTATE,0");
    return;
  }
  if (mode == MODE_LE) {
    start_le();
  } else {
    start_classic();
  }
}

static void start_cycle() {
  running = true;
  if (mode == MODE_LE) {
    start_le();
  } else {
    start_classic();
  }
}

static void stop_cycle() {
  running = false;
  esp_bt_gap_cancel_discovery();
  esp_ble_gap_stop_scanning();
  phase = PHASE_IDLE;
  Serial.println("SKSTATE,0");
}

static void say_hello() {
  uint8_t caps = bt_ok ? 0x03 : 0x00; /* BR/EDR | LE */
  Serial.printf("SKHELLO,%s,%u\n", SKIMSCAN_FW_VERSION, caps);
}

/* ------------------------------------------------------------------ */

void setup() {
  Serial.begin(115200);
  delay(200);

  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  bt_ok = (esp_bt_controller_init(&cfg) == ESP_OK) &&
          (esp_bt_controller_enable(ESP_BT_MODE_BTDM) == ESP_OK) &&
          (esp_bluedroid_init() == ESP_OK) && (esp_bluedroid_enable() == ESP_OK);

  if (!bt_ok) {
    /* Nearly always the wrong chip: an S2, S3 or C3 has no BR/EDR at all. */
    Serial.println("SKERR,no BR/EDR - needs a classic ESP32");
    say_hello();
    return;
  }

  esp_bt_gap_register_callback(bt_gap_cb);
  esp_ble_gap_register_callback(ble_gap_cb);

  /* Listen only: Skimscan is not itself discoverable and never pairs. */
  esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  esp_ble_gap_set_scan_params(&ble_params);

  say_hello();
  start_cycle(); /* auto-arm; the Flipper can STOP and START at will */
}

static void handle_command(String cmd) {
  cmd.trim();
  if (cmd == "START") {
    if (!running && bt_ok) start_cycle();
  } else if (cmd == "STOP") {
    if (running) stop_cycle();
  } else if (cmd == "PING") {
    say_hello();
  } else if (cmd.startsWith("MODE:")) {
    int m = cmd.substring(5).toInt();
    mode = (m == 1) ? MODE_CLASSIC : (m == 2) ? MODE_LE : MODE_BOTH;
    /* Applied at the top of the next pass rather than mid-inquiry, so a pass
     * is never half one thing and half another. */
  }
}

void loop() {
  if (Serial.available()) {
    handle_command(Serial.readStringUntil('\n'));
  }
  delay(5);
}
