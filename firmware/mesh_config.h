// Node configuration, set over USB and kept in NVS (design/plan.md §5).
#pragma once
#include <Arduino.h>

struct MeshConfig {
  uint16_t version;
  uint16_t node_id;
  uint8_t relay_role;           // ROLE_BALLOON or ROLE_GROUND (relays only)
  uint16_t helper_id;
  int32_t helper_lat_e7, helper_lon_e7;
  int32_t lat_e7, lon_e7;       // fixed position (helper, dongle) or GPS fallback (relay)
  int16_t alt_m;
  uint8_t pos_set;
  int8_t txp_dbm;
  float range_km;               // DEMO_RANGE_KM, 0 = off
  uint8_t sf_slots, ctrl_slots; // superframe layout, the same on every node
  int32_t tx_latency_us;        // TX_LATENCY_US for time sync
};

// The running config. `set` commands edit cfg_next; `save` writes cfg_next.
// Settings that are safe to change live go into both; the ID, role and
// superframe layout only take effect after a reboot.
extern MeshConfig cfg;
extern MeshConfig cfg_next;

void config_defaults(MeshConfig& c);
bool config_valid(const MeshConfig& c, const char** why);
void config_load();
bool config_save();
uint16_t config_next_message_id();
