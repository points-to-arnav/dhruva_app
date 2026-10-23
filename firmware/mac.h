// Hybrid TDM + FDM channel access (design/algorithm-design.md §2, §7):
// control window on ch 0 (HELLO / INI), data window in 800 ms pairs on the
// receivers' home channels, guard slot for flash erase and housekeeping.
#pragma once
#include <Arduino.h>
#include "packet.h"

void mac_begin(int64_t now);
void mac_tick(int64_t now);
void mac_on_ack(const CtlPkt& c, int64_t now);
void mac_on_delivery_ack(const CtlPkt& c, int64_t now);
void mac_request_ini(int64_t now); // relay's first GPS fix
uint8_t mac_pairs_per_sf();
