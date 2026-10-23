#include "mesh_config.h"
#include <Preferences.h>
#include <math.h>
#include "role.h"

#define CFG_VERSION 1
#define CFG_NAMESPACE "dhruva"

MeshConfig cfg;
MeshConfig cfg_next;

static int32_t e7(double deg) { return (int32_t)lround(deg * 1e7); }

// Defaults are the demo setup (design/plan.md §8): helper, relay and dongle
// 4 km apart in a line.
void config_defaults(MeshConfig& c) {
  memset(&c, 0, sizeof c);
  c.version = CFG_VERSION;
  c.node_id = DEFAULT_NODE_ID;
  c.relay_role = ROLE_BALLOON;
  c.helper_id = 0x0001;
  c.helper_lat_e7 = e7(27.752);
  c.helper_lon_e7 = e7(85.32);
#if IS_HELPER
  c.lat_e7 = c.helper_lat_e7;
  c.lon_e7 = c.helper_lon_e7;
  c.pos_set = 1;
#elif IS_RELAY
  c.lat_e7 = e7(27.716);
  c.lon_e7 = e7(85.32);
  c.pos_set = 1;
#else
  c.pos_set = 0; // a dongle's position comes from the phone
#endif
  c.alt_m = 1300;
  c.txp_dbm = 3;
  c.range_km = 0;
  c.sf_slots = 25;
  c.ctrl_slots = 4;
  c.tx_latency_us = 0;
}

bool config_valid(const MeshConfig& c, const char** why) {
  const char* w = nullptr;
  if (c.node_id == 0x0000 || c.node_id == 0xFFFF) w = "node id must be 0x0001-0xFFFE";
  else if (c.helper_id < 0x0001 || c.helper_id > 0x000F) w = "helper id must be 0x0001-0x000F";
  else if (c.ctrl_slots < 2) w = "ctrl_slots must be >= 2 (slot 0 is the helper's)";
  else if (c.sf_slots < c.ctrl_slots + 3) w = "sf_slots must be >= ctrl_slots + 3 (one pair + guard)";
  else if (c.txp_dbm < 2 || c.txp_dbm > 20) w = "txp must be 2-20 dBm";
  else if (c.range_km < 0) w = "range must be >= 0";
  if (why) *why = w;
  return w == nullptr;
}

void config_load() {
  config_defaults(cfg);
  Preferences p;
  if (p.begin(CFG_NAMESPACE, true)) {
    MeshConfig c;
    if (p.getBytesLength("cfg") == sizeof c && p.getBytes("cfg", &c, sizeof c) == sizeof c &&
        c.version == CFG_VERSION && config_valid(c, nullptr)) {
      cfg = c;
    }
    p.end();
  }
  cfg_next = cfg;
}

bool config_save() {
  if (!config_valid(cfg_next, nullptr)) return false;
  Preferences p;
  if (!p.begin(CFG_NAMESPACE, false)) return false;
  bool ok = p.putBytes("cfg", &cfg_next, sizeof cfg_next) == sizeof cfg_next;
  p.end();
  return ok;
}

// Own message counter, persisted so a rebooted dongle never reuses an ID
// (algorithm design §10).
uint16_t config_next_message_id() {
  Preferences p;
  p.begin(CFG_NAMESPACE, false);
  uint16_t id = p.getUShort("mid", 0) + 1;
  if (id == 0) id = 1;
  p.putUShort("mid", id);
  p.end();
  return id;
}
