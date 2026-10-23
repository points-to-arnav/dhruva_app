// Superframe clock, rooted at the helper (design/algorithm-design.md §2-3).
#pragma once
#include <Arduino.h>
#include "packet.h"
#include "radio.h"

#define SLOT_US 400000LL
#define PAIR_US (2 * SLOT_US)

int64_t sf_len_us();                 // one superframe = one HELLO interval
bool ts_synced();
uint8_t ts_depth();                  // 0 helper, 1..14 hops, DEPTH_UNSYNCED
uint16_t ts_parent();
int64_t ts_sf_start(int64_t now);    // start of the superframe containing now (synced only)
uint16_t ts_offset_ms(int64_t now);  // sf_offset_ms for a beacon sent now

void timesync_begin(int64_t now);
void timesync_on_beacon(const Beacon& b, const RxFrame& f);
void timesync_tick(int64_t now);
