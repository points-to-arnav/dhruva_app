// Persistent message log + seen set on the raw `mesh` flash partition
// (design/algorithm-design.md §10). Store before ACK in ~10 ms: space is
// erased ahead of time, and state changes only clear bits.
#pragma once
#include <Arduino.h>

// Record state byte: each step clears more bits, so it is programmed in place.
enum : uint8_t { ST_EMPTY = 0xFF, ST_STORED = 0xFE, ST_SENT = 0xFC, ST_DELIVERED = 0xF8, ST_FAILED = 0xF0 };
// Delivery state byte.
enum : uint8_t { DS_NONE = 0xFF, DS_OWED = 0xFE, DS_DONE = 0xFC, DS_GAVE_UP = 0xF8 };

#define MSG_RECS 2048

// RAM index of the message log, rebuilt at boot.
struct MsgIdx {
  uint32_t key;
  uint16_t prev_hop;   // last path entry when received (ID_NONE for own messages)
  uint16_t next_hop;   // who ACKed it, 0xFFFF until handed off
  uint16_t helper_id;  // from the DELIVERY, 0xFFFF until delivered
  uint8_t state, dstate;
  bool own;
  bool valid;
};
extern MsgIdx msg_idx[MSG_RECS];

bool store_begin();
bool store_ready();
int store_find(uint32_t key);                // record index, or -1
bool store_can_accept();
int store_append(const uint8_t* data, uint8_t len, uint16_t prev_hop, bool own, uint8_t state);
int store_read(int i, uint8_t* buf);         // raw DATA (PKT_MAX bytes), returns its length or -1
void store_set_state(int i, uint8_t st);
void store_set_dstate(int i, uint8_t ds);
void store_set_next_hop(int i, uint16_t id);
void store_set_helper(int i, uint16_t id);
int store_count(uint8_t state);

bool seen_has(uint32_t key);
bool seen_add(uint32_t key);

// At most one sector erase (<= ~400 ms); call only in the guard slot.
void store_housekeep();
void store_erase_all();
