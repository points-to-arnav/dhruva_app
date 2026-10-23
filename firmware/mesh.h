// The mesh node: receive dispatch, store-before-ACK, DELIVERY, own messages
// (design/algorithm-design.md §6, §8, §9).
#pragma once
#include <Arduino.h>
#include "packet.h"
#include "radio.h"

// Message states, also reported to the phone (algorithm design §9).
enum MsgState : uint8_t {
  MS_QUEUED,
  MS_STORED,
  MS_TRANSMITTING,
  MS_SENT_TO_MESH,
  MS_DELIVERED,
  MS_WAITING,
  MS_FAILED,
};
const char* msg_state_name(uint8_t s);

struct MyPos {
  int32_t lat, lon; // degrees * 1e7
  int16_t alt;
  uint16_t acc_dm;  // ACC16_NONE = no fix
  bool fix;
};

struct MeshStats {
  uint32_t rx, tx;
  uint32_t rx_crc_hw, rx_crc8, rx_bad;
  uint32_t overheard, dup, cad_busy, range_dropped, store_full;
  uint32_t forwarded, delivered;
};
extern MeshStats stats;

void mesh_begin();
void mesh_tick(int64_t now);
void mesh_on_frame(const RxFrame& f);

uint8_t my_role();
MyPos mesh_my_pos();
uint32_t mesh_unix_time(); // GPS time, 0 if none

// Relay GPS (algorithm design §11).
void mesh_gps_fix(int32_t lat, int32_t lon, int16_t alt, uint16_t acc_dm);
void mesh_gps_time(uint32_t unix_s);

// Creates an own message: stored before it is reported STORED. False if it
// could not be stored (reported FAILED).
bool mesh_create_message(uint8_t people, uint8_t flags, int32_t lat, int32_t lon, int16_t alt,
                         uint8_t pos_acc, uint32_t timestamp);

// State change of a message; reported to the phone when it is this node's own.
void mesh_own_state(uint32_t key, uint8_t state, uint16_t next_hop);

// Immediate ACK / DELIVERY_ACK on the current channel: no CAD, no backoff.
void mesh_send_ctl(uint8_t type, uint16_t from, uint16_t to, uint32_t key);
