// Dhruva Jaal LoRa mesh: one firmware for every module.
//
// 1. Pick the module in role.h: IS_DONGLE, IS_RELAY or IS_HELPER.
// 2. Arduino IDE: board "ESP32C3 Dev Module", USB CDC On Boot: Enabled,
//    library "LoRa" by Sandeep Mistry. partitions.csv in this folder is
//    picked up automatically.
// 3. Flash, open the serial monitor, `help`.
//
// Design: design/plan.md, design/packet-design.md, design/algorithm-design.md.

#include <esp_timer.h>
#include "role.h"
#include "pins.h"
#include "mesh_config.h"
#include "mesh_log.h"
#include "radio.h"
#include "store.h"
#include "mesh.h"
#include "cli.h"
#include "gps.h"
#include "phone.h"

#if !ARDUINO_USB_CDC_ON_BOOT
#error "Tools > USB CDC On Boot must be Enabled: Serial is the C3's native USB port"
#endif

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0); // never block the radio timing when nothing reads USB
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  config_load();
  if (!store_begin()) log_line("store: no 'mesh' partition, check partitions.csv: nothing can be stored or ACKed");
  if (!radio_begin()) {
    // Keep the command line alive so the board can still be inspected.
    for (;;) {
      log_line("radio: SX1278 not found, check the wiring (design/plan.md section 3)");
      for (int i = 0; i < 20; i++) {
        cli_poll();
        digitalWrite(PIN_LED, i % 2 ? HIGH : LOW);
        delay(100);
      }
    }
  }
  gps_begin();
  mesh_begin();
}

void loop() {
  cli_poll();
  RxFrame f;
  if (radio_poll(f)) mesh_on_frame(f);
  gps_poll();
  int64_t now = esp_timer_get_time();
  mesh_tick(now);
  phone_tick(now);
}
