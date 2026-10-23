#include "timesync.h"
#include "role.h"
#include "mesh_config.h"
#include "mesh_log.h"

#define JUMP_THRESHOLD_US 50000LL
#define LOST_AFTER_SUPERFRAMES 6

static bool s_synced = false;
static uint8_t s_depth = DEPTH_UNSYNCED;
static uint16_t s_parent = ID_NONE;
static uint8_t s_parent_depth = DEPTH_UNSYNCED;
static int64_t s_ref = 0;            // the start of some superframe
static int64_t s_last_parent_us = 0;

int64_t sf_len_us() { return (int64_t)cfg.sf_slots * SLOT_US; }
bool ts_synced() { return s_synced; }
uint8_t ts_depth() { return s_synced ? s_depth : DEPTH_UNSYNCED; }
uint16_t ts_parent() { return s_parent; }

// Wrap a clock difference into [-superframe/2, superframe/2).
static int64_t wrap(int64_t e) {
  int64_t len = sf_len_us();
  e %= len;
  if (e >= len / 2) e -= len;
  else if (e < -len / 2) e += len;
  return e;
}

int64_t ts_sf_start(int64_t now) {
  int64_t len = sf_len_us();
  int64_t d = now - s_ref;
  int64_t k = d / len;
  if (d < 0 && d % len) k--;
  return s_ref + k * len;
}

uint16_t ts_offset_ms(int64_t now) {
  return s_synced ? (uint16_t)((now - ts_sf_start(now)) / 1000) : 0;
}

static void lose_sync(const char* why) {
  if (!s_synced) return;
  s_synced = false;
  s_depth = DEPTH_UNSYNCED;
  s_parent = ID_NONE;
  s_parent_depth = DEPTH_UNSYNCED;
  log_json("sync_lost", "\"why\":\"%s\"", why);
}

void timesync_begin(int64_t now) {
#if IS_HELPER
  // The helper is the root: superframe 0 starts at boot.
  s_synced = true;
  s_depth = DEPTH_ROOT;
  s_ref = now;
#else
  (void)now;
#endif
}

void timesync_on_beacon(const Beacon& b, const RxFrame& f) {
#if IS_HELPER
  (void)b;
  (void)f;
#else
  uint8_t d = rf_depth(b.role_flags);
  if (d >= DEPTH_UNSYNCED) return;
  bool is_parent = s_synced && b.node_id == s_parent;
  bool closer = s_synced && !is_parent && d < s_parent_depth; // strictly closer to the root
  if (s_synced && !is_parent && !closer) return;

  // Work back from RxDone to the sender's superframe start.
  int64_t t_tx = f.t_rx_us - (int64_t)airtime_us(f.len) - cfg.tx_latency_us;
  int64_t sf_start = t_tx - (int64_t)b.sf_offset_ms * 1000;
  int64_t err = 0;
  if (!s_synced) {
    s_ref = sf_start;
  } else {
    err = wrap(sf_start - s_ref);
    if (!is_parent || llabs(err) > JUMP_THRESHOLD_US) s_ref += err; // jump
    else s_ref += err / 2;                                           // smooth
  }

  if (d + 1 >= DEPTH_UNSYNCED) { // a loop counting up, or too far from the helper
    lose_sync("depth");
    return;
  }
  bool changed = !s_synced || !is_parent || s_depth != d + 1;
  s_synced = true;
  s_parent = b.node_id;
  s_parent_depth = d;
  s_depth = d + 1;
  s_last_parent_us = f.t_rx_us;
  if (changed || llabs(err) > 10000) {
    log_json("sync", "\"parent\":\"0x%04X\",\"depth\":%u,\"err_ms\":%ld", (unsigned)s_parent,
             (unsigned)s_depth, (long)(err / 1000));
  }
#endif
}

void timesync_tick(int64_t now) {
#if !IS_HELPER
  if (s_synced && now - s_last_parent_us > LOST_AFTER_SUPERFRAMES * sf_len_us()) lose_sync("parent silent");
#else
  (void)now;
#endif
}
