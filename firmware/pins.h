// ESP32-C3 Super Mini wiring (design/plan.md §3).
#pragma once

#define PIN_LORA_DIO0 2  // strapping pin: if a board won't boot with the module on, move DIO0 to 10
#define PIN_LORA_RST  3
#define PIN_LORA_MOSI 4
#define PIN_LORA_MISO 5
#define PIN_LORA_SCK  6
#define PIN_LORA_NSS  7
#define PIN_GPS_RX    20 // NEO-6M TX -> ESP RX (relays only)
#define PIN_LED       8  // onboard, active low

#define GPS_BAUD 9600
