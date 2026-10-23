#include "mesh.h"
#include <esp_timer.h>
#include "role.h"
#include "mesh_config.h"
#include "mesh_log.h"
#include "geo.h"
#include "timesync.h"
#include "neighbor.h"
#include "store.h"
#include "txq.h"
#include "mac.h"
#include "phone.h"
#include "report.h"

#define GPS_FRESH_US 10000000LL

MeshStats stats;

static bool s_gps_ever = false;
static MyPos s_gps_pos;
static int64_t s_gps_fix_us = 0;
static uint32_t s_gps_unix = 0;
static int64_t s_gps_unix_at = 0;

const char* msg_state_name(uint8_t s) {
  switch (s) {
    case MS_QUEUED: return "QUEUED";
    case MS_STORED: return "STORED";
    case MS_TRANSMITTING: return "TRANSMITTING";
    case MS_SENT_TO_MESH: return "SENT_TO_MESH";
    case MS_DELIVERED: return "DELIVERED";
    case MS_WAITING: return "WAITING";
    case MS_FAILED: return "FAILED";
  }
  return "?";
}

uint8_t my_role() {
#if IS_HELPER
  return ROLE_HELPER;
#elif IS_DONGLE
  return ROLE_DONGLE;
#else
  return cfg.relay_role;
#endif
}

// algorithm design §11: live GPS fix; after a fix is lost, the last GPS
// position without the fix bit; before any fix, the flash position.
MyPos mesh_my_pos() {
  MyPos p;
  if (s_gps_ever) {
    p = s_gps_pos;
    p.fix = esp_timer_get_time() - s_gps_fix_us < GPS_FRESH_US;
    if (!p.fix) p.acc_dm = ACC16_NONE;
    return p;
  }
  p.lat = cfg.lat_e7;
  p.lon = cfg.lon_e7;
  p.alt = cfg.alt_m;
  p.acc_dm = ACC16_NONE;
  p.fix = false;
  return p;
}

void mesh_gps_fix(int32_t lat, int32_t lon, int16_t alt, uint16_t acc_dm) {
  int64_t now = esp_timer_get_time();
  bool first = !s_gps_ever;
  s_gps_pos.lat = lat;
  s_gps_pos.lon = lon;
  s_gps_pos.alt = alt;
  s_gps_pos.acc_dm = acc_dm;
  s_gps_pos.fix = true;
  s_gps_fix_us = now;
  s_gps_ever = true;
  if (first) {
    log_json("gps_fix", "\"lat\":%.6f,\"lon\":%.6f,\"acc_dm\":%u", lat / 1e7, lon / 1e7, (unsigned)acc_dm);
    mac_request_ini(now); // INI again on the first fix
  }
}

void mesh_gps_time(uint32_t unix_s) {
  s_gps_unix = unix_s;
  s_gps_unix_at = esp_timer_get_time();
}

uint32_t mesh_unix_time() {
  if (!s_gps_unix) return 0;
  return s_gps_unix + (uint32_t)((esp_timer_get_time() - s_gps_unix_at) / 1000000);
}

void mesh_own_state(uint32_t key, uint8_t state, uint16_t next_hop) {
  if (key_src(key) != cfg.node_id) return;
#if IS_DONGLE
  phone_status(key, state, next_hop);
#else
  (void)next_hop;
#endif
  char ks[11];
  key_str(key, ks);
  log_json("msg_state", "\"key\":\"%s\",\"state\":\"%s\"", ks, msg_state_name(state));
}

void mesh_send_ctl(uint8_t type, uint16_t from, uint16_t to, uint32_t key) {
  CtlPkt c;
  c.type = type;
  c.from = from;
  c.to = to;
  c.orig_source = key_src(key);
  c.message_id = key_mid(key);
  uint8_t buf[CTL_LEN];
  ctl_encode(c, buf);
  radio_tx(radio_channel(), buf, CTL_LEN); // the sender is waiting on this channel
  stats.tx++;
}

bool mesh_create_message(uint8_t people, uint8_t flags, int32_t lat, int32_t lon, int16_t alt, uint8_t pos_acc,
                         uint32_t timestamp) {
  int64_t now = esp_timer_get_time();
  uint16_t mid = config_next_message_id();
  uint32_t key = msg_key(cfg.node_id, mid);
  mesh_own_state(key, MS_QUEUED, 0);

  DataPkt p;
  memset(&p, 0, sizeof p);
  p.source_id = cfg.node_id;
  p.message_id = mid;
  p.destination_id = cfg.helper_id;
  p.next_hop_id = ID_BROADCAST; // set per attempt
  p.ttl = TTL_START;
  p.people = people;
  p.flags = flags & 0x1F;
  p.lat = lat;
  p.lon = lon;
  p.alt = alt;
  p.pos_acc = pos_acc;
  p.timestamp = timestamp;
  p.path_count = 1;
  p.path[0] = cfg.node_id;
  uint8_t raw[PKT_MAX];
  int n = data_encode(p, raw);

  char ks[11];
  key_str(key, ks);
  if (store_append(raw, n, ID_NONE, true, ST_STORED) < 0) {
    stats.store_full++;
    log_json("store_full", "\"key\":\"%s\"", ks);
    mesh_own_state(key, MS_FAILED, 0);
    return false;
  }
  seen_add(key);
  log_json("msg_new", "\"key\":\"%s\",\"people\":%u,\"flags\":\"0x%02X\"", ks, (unsigned)people, (unsigned)p.flags);
  mesh_own_state(key, MS_STORED, 0);
  txq_add(JOB_OWN, key, now);
  if (!ts_synced()) mesh_own_state(key, MS_WAITING, 0); // goes out once a HELLO is heard
  return true;
}

// --- receive (algorithm design §6) ----------------------------------------

static void on_beacon(const Beacon& b, const RxFrame& f, int64_t now) {
  if (b.node_id == cfg.node_id) return;
  if (cfg.range_km > 0) { // demo range filter, every role
    MyPos me = mesh_my_pos();
    if (geo_dist_m(me.lat, me.lon, b.lat, b.lon) > cfg.range_km * 1000) {
      stats.range_dropped++;
      return;
    }
  }
  nbr_on_beacon(b, f, now);
  timesync_on_beacon(b, f);
  if (b.ini && b.helper_id != cfg.helper_id) {
    log_json("helper_mismatch", "\"from\":\"0x%04X\",\"their_helper\":\"0x%04X\"", (unsigned)b.node_id,
             (unsigned)b.helper_id);
  }
}

// Store before ACK.
static void on_data(const DataPkt& p, const RxFrame& f, int64_t now) {
  uint32_t key = msg_key(p.source_id, p.message_id);
  uint16_t prev = p.path[p.path_count - 1];
  char ks[11];
  key_str(key, ks);

  if (seen_has(key)) { // a retransmission: our ACK was lost
    mesh_send_ctl(PT_ACK, cfg.node_id, prev, key);
    stats.dup++;
    log_json("dup", "\"key\":\"%s\",\"from\":\"0x%04X\"", ks, (unsigned)prev);
    return;
  }
#if IS_DONGLE
  return; // dongles never accept DATA
#endif
  if (!store_can_accept()) { // no ACK: the sender keeps it and routes elsewhere
    stats.store_full++;
    log_json("store_full", "\"key\":\"%s\"", ks);
    return;
  }
  int i = store_append(f.buf, f.len, prev, false, ST_STORED);
  if (i < 0) {
    stats.store_full++;
    return;
  }
  seen_add(key);
  mesh_send_ctl(PT_ACK, cfg.node_id, prev, key);
  log_json("data_rx", "\"key\":\"%s\",\"from\":\"0x%04X\",\"rssi\":%d,\"snr\":%.1f,\"ch\":%u,\"ttl\":%u,\"hops\":%u", ks,
           (unsigned)prev, f.rssi, f.snr, (unsigned)f.ch, (unsigned)p.ttl, (unsigned)(p.path_count - 1));

#if IS_HELPER
  // The helper is the final stop for every DATA.
  store_set_state(i, ST_DELIVERED);
  store_set_dstate(i, DS_OWED);
  stats.delivered++;
  report_data(p, f);
  char path[PATH_MAX_N * 9 + 12];
  int o = 0;
  for (int k = 0; k < p.path_count; k++) o += snprintf(path + o, sizeof path - o, "\"0x%04X\",", (unsigned)p.path[k]);
  snprintf(path + o, sizeof path - o, "\"0x%04X\"", (unsigned)cfg.node_id);
  log_json("delivered", "\"key\":\"%s\",\"path\":[%s]", ks, path);
  txq_add(JOB_DELIVERY, key, now);
#else
  if (p.ttl == 0 || p.path_count >= PATH_MAX_N) {
    store_set_state(i, ST_FAILED);
    log_json("ttl_expired", "\"key\":\"%s\"", ks);
    return;
  }
  txq_add(JOB_FORWARD, key, now);
#endif
}

static void on_delivery(const CtlPkt& c, int64_t now) {
  uint32_t key = msg_key(c.orig_source, c.message_id);
  int i = store_find(key);
  uint16_t ack_to = (i >= 0 && msg_idx[i].next_hop != 0xFFFF) ? msg_idx[i].next_hop : ID_BROADCAST;
  mesh_send_ctl(PT_DELIVERY_ACK, cfg.node_id, ack_to, key);
  char ks[11];
  key_str(key, ks);
  if (i < 0) { // evicted: the chain stops here
    log_json("delivery_no_record", "\"key\":\"%s\"", ks);
    return;
  }
  if (msg_idx[i].state == ST_DELIVERED) return; // duplicate: our DELIVERY_ACK was lost
  store_set_state(i, ST_DELIVERED);
  // It got there even if our hand-off ACK was lost: stop sending it.
  txq_remove(txq_find(JOB_OWN, key));
  txq_remove(txq_find(JOB_FORWARD, key));
  log_json("delivery_rx", "\"key\":\"%s\",\"helper\":\"0x%04X\"", ks, (unsigned)c.from);
  if (c.orig_source == cfg.node_id) {
    mesh_own_state(key, MS_DELIVERED, 0);
    return;
  }
  store_set_helper(i, c.from);
  store_set_dstate(i, DS_OWED);
  txq_add(JOB_DELIVERY, key, now);
}

void mesh_on_frame(const RxFrame& f) {
  int64_t now = esp_timer_get_time();
  if (f.crc_err) {
    stats.rx_crc_hw++;
    return;
  }
  int type = pkt_validate(f.buf, f.len);
  if (type < 0) {
    if (type == PV_CRC) stats.rx_crc8++;
    else stats.rx_bad++;
    return;
  }
  stats.rx++;
  switch (type) {
    case PT_HELLO:
    case PT_INI: {
      Beacon b;
      if (beacon_decode(f.buf, f.len, b)) on_beacon(b, f, now);
      break;
    }
    case PT_DATA: {
      DataPkt p;
      if (!data_decode(f.buf, f.len, p)) break;
      if (p.next_hop_id != cfg.node_id) { // only the named next hop takes it
        stats.overheard++;
        break;
      }
      on_data(p, f, now);
      break;
    }
    case PT_ACK: {
      CtlPkt c;
      if (ctl_decode(f.buf, f.len, c) && c.to == cfg.node_id) mac_on_ack(c, now);
      break;
    }
    case PT_DELIVERY: {
      CtlPkt c;
      if (ctl_decode(f.buf, f.len, c) && c.to == cfg.node_id) on_delivery(c, now);
      break;
    }
    case PT_DELIVERY_ACK: {
      CtlPkt c;
      if (ctl_decode(f.buf, f.len, c)) mac_on_delivery_ack(c, now);
      break;
    }
  }
}

void mesh_begin() {
  int64_t now = esp_timer_get_time();
  timesync_begin(now);
  mac_begin(now);
  txq_refill(now); // messages that survived a reboot
  const char* role = IS_HELPER ? "helper" : IS_DONGLE ? "dongle" : (cfg.relay_role == ROLE_GROUND ? "ground" : "balloon");
  log_line("Dhruva Jaal node 0x%04X (%s), home ch %u, helper 0x%04X, superframe %u slots / %u control",
           (unsigned)cfg.node_id, role, (unsigned)home_ch(cfg.node_id), (unsigned)cfg.helper_id,
           (unsigned)cfg.sf_slots, (unsigned)cfg.ctrl_slots);
  log_json("boot", "\"role\":\"%s\",\"ch\":%u,\"jobs\":%d", role, (unsigned)home_ch(cfg.node_id), txq_count());
}

void mesh_tick(int64_t now) {
  timesync_tick(now);
  mac_tick(now);
}
