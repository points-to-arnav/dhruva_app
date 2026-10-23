#include "mac.h"
#include <esp_random.h>
#include <esp_timer.h>
#include "role.h"
#include "mesh_config.h"
#include "mesh_log.h"
#include "mesh.h"
#include "radio.h"
#include "timesync.h"
#include "neighbor.h"
#include "routing.h"
#include "store.h"
#include "txq.h"

#define GUARD_US 25000LL
#define JITTER_STEP_US 12000LL
#define LATE_START_US 100000LL         // too late in a pair to fit DATA + ACK: wait for the next one
#define INI_BOOT_WINDOW_US 2000000LL
#define WAITING_RETRY_US 60000000LL
#define DELIVERY_ROUND_WAIT_US 60000000LL
#define STATS_EVERY_US 60000000LL

enum ExState : uint8_t { EX_IDLE, EX_WAIT_START, EX_WAIT_ACK };

// The one exchange this node may run per pair.
static struct {
  ExState st;
  Job* job;
  uint32_t key;
  uint8_t kind;
  uint16_t target;
  int64_t t_start;
  int64_t t_deadline;
} ex;

static int64_t s_pair_done = -1;   // start of the last pair handled
static int64_t s_hello_sf = -1;    // superframe whose HELLO slot was picked
static bool s_hello_done;
static int s_hello_slot;
static int64_t s_hello_jitter;
static int64_t s_house_sf = -1;
static int64_t s_last_house_us, s_last_wake_us, s_last_stats_us;
static bool s_ini_pending = !IS_DONGLE; // INI once at boot (infra only)
static int64_t s_ini_at;
static uint8_t s_hello_seq;
static uint32_t s_nbr_version_seen;

static int64_t rand_between(int64_t lo, int64_t hi) { return lo + (int64_t)(esp_random() % (uint32_t)(hi - lo + 1)); }

static bool near(int64_t a, int64_t b, int64_t tol) { return a > b - tol && a < b + tol; }

static uint8_t my_home() { return home_ch(cfg.node_id); }

uint8_t mac_pairs_per_sf() { return (cfg.sf_slots - cfg.ctrl_slots - 1) / 2; }

void mac_begin(int64_t now) {
  ex.st = EX_IDLE;
  s_ini_at = now + rand_between(0, INI_BOOT_WINDOW_US);
  s_last_house_us = s_last_wake_us = s_last_stats_us = now;
  radio_rx(CH_CONTROL);
}

void mac_request_ini(int64_t now) {
#if !IS_DONGLE
  s_ini_pending = true;
  s_ini_at = now;
#else
  (void)now;
#endif
}

// HELLO or INI on ch 0, after CAD. False if the channel was busy.
static bool send_beacon(bool ini) {
  if (radio_cad_busy(CH_CONTROL)) {
    stats.cad_busy++;
    return false;
  }
  MyPos me = mesh_my_pos();
  Beacon b;
  memset(&b, 0, sizeof b);
  b.ini = ini;
  b.node_id = cfg.node_id;
  b.role_flags = rf_make(my_role(), me.fix, ts_depth());
  b.helper_id = cfg.helper_id;
  b.lat = me.lat;
  b.lon = me.lon;
  b.alt = me.alt;
  b.pos_acc_dm = me.acc_dm;
  b.gps_time = mesh_unix_time();
  b.seq = s_hello_seq;
  b.sf_offset_ms = ts_offset_ms(esp_timer_get_time());
  uint8_t buf[INI_LEN];
  int n = beacon_encode(b, buf);
  radio_tx(CH_CONTROL, buf, n);
  stats.tx++;
  if (!ini) s_hello_seq++;
  else log_json("ini_tx", "\"depth\":%u,\"fix\":%s", (unsigned)ts_depth(), me.fix ? "true" : "false");
  return true;
}

static void housekeeping(int64_t now) {
  nbr_tick(now);
  nbr_recompute();
  store_housekeep();
  txq_refill(now);
}

// --- exchanges -----------------------------------------------------------

static void set_waiting(Job* j, int64_t now) {
  if (j->waiting) return;
  j->waiting = true;
  j->waiting_since_us = now;
  char ks[11];
  key_str(j->key, ks);
  log_json("waiting", "\"key\":\"%s\"", ks);
  mesh_own_state(j->key, MS_WAITING, 0);
}

static uint16_t pick_target(Job* j, int64_t now) {
  int i = store_find(j->key);
  if (i < 0) {
    txq_remove(j);
    return ID_NONE;
  }
  if (j->kind == JOB_DELIVERY) { // fixed: back along the stored path
    uint16_t prev = msg_idx[i].prev_hop;
    if (prev == ID_NONE || prev == ID_BROADCAST) txq_remove(j);
    return prev;
  }
  uint8_t raw[PKT_MAX];
  DataPkt p;
  int n = store_read(i, raw);
  if (n < 0 || !data_decode(raw, n, p)) {
    txq_remove(j);
    return ID_NONE;
  }
  return route_next_hop(p.path, p.path_count, j->failed, j->n_failed, now);
}

static void start_exchange(int64_t now, int64_t pair_start) {
  for (int tries = 0; tries < TXQ_MAX; tries++) {
    Job* j = txq_next_due(pair_start);
    if (!j) return;
    if (j->target == ID_NONE) j->target = pick_target(j, now);
    if (!j->used) continue;
    if (j->target == ID_NONE) {
      set_waiting(j, now);
      continue;
    }
    ex.st = EX_WAIT_START;
    ex.job = j;
    ex.key = j->key;
    ex.kind = j->kind;
    ex.target = j->target;
    ex.t_start = pair_start + GUARD_US + rand_between(0, 3) * JITTER_STEP_US;
    ex.t_deadline = pair_start + PAIR_US - GUARD_US;
    return;
  }
}

static int build_packet(uint8_t* out) {
  int i = store_find(ex.key);
  if (i < 0) return -1;
  if (ex.kind == JOB_DELIVERY) {
    CtlPkt c;
    c.type = PT_DELIVERY;
#if IS_HELPER
    c.from = cfg.node_id;
#else
    c.from = msg_idx[i].helper_id != 0xFFFF ? msg_idx[i].helper_id : cfg.helper_id;
#endif
    c.to = ex.target;
    c.orig_source = key_src(ex.key);
    c.message_id = key_mid(ex.key);
    return ctl_encode(c, out);
  }
  uint8_t raw[PKT_MAX];
  DataPkt p;
  int n = store_read(i, raw);
  if (n < 0 || !data_decode(raw, n, p)) return -1;
  if (ex.kind == JOB_FORWARD) {
    if (p.ttl == 0 || p.path_count >= PATH_MAX_N) return -1;
    p.ttl--;
    p.path[p.path_count++] = cfg.node_id;
  }
  p.next_hop_id = ex.target;
  return data_encode(p, out);
}

static void exchange_done() {
  ex.st = EX_IDLE;
  radio_rx(my_home());
}

static void exchange_ok(int64_t now) {
  (void)now;
  char ks[11];
  key_str(ex.key, ks);
  nbr_ack_result(ex.target, true);
  int i = store_find(ex.key);
  if (ex.kind == JOB_DELIVERY) {
    if (i >= 0) store_set_dstate(i, DS_DONE);
    log_json("delivery_ack_rx", "\"key\":\"%s\",\"from\":\"0x%04X\"", ks, (unsigned)ex.target);
  } else {
    if (i >= 0) {
      store_set_next_hop(i, ex.target);
      if (msg_idx[i].state == ST_STORED) store_set_state(i, ST_SENT); // handed off
    }
    if (ex.kind == JOB_FORWARD) stats.forwarded++;
    log_json("ack_rx", "\"key\":\"%s\",\"from\":\"0x%04X\",\"retries\":%u", ks, (unsigned)ex.target,
             (unsigned)ex.job->retries);
    mesh_own_state(ex.key, MS_SENT_TO_MESH, ex.target);
  }
  txq_remove(ex.job);
  exchange_done();
}

static void exchange_failed(int64_t now) {
  Job* j = ex.job;
  char ks[11];
  key_str(ex.key, ks);
  nbr_ack_result(ex.target, false);
  j->retries++;
  if (j->retries <= MAX_RETRIES) {
    int64_t span = j->retries >= 4 ? 16 : (1 << j->retries); // random(1 .. min(2^k, 16)) pairs
    j->not_before_us = now + rand_between(1, span) * PAIR_US;
    log_json("retry", "\"key\":\"%s\",\"to\":\"0x%04X\",\"n\":%u", ks, (unsigned)ex.target, (unsigned)j->retries);
  } else if (j->kind == JOB_DELIVERY) {
    j->retries = 0;
    j->rounds++;
    if (j->rounds >= DELIVERY_ROUNDS) {
      int i = store_find(ex.key);
      if (i >= 0) store_set_dstate(i, DS_GAVE_UP);
      log_json("delivery_gave_up", "\"key\":\"%s\",\"to\":\"0x%04X\"", ks, (unsigned)ex.target);
      txq_remove(j);
    } else {
      j->not_before_us = now + DELIVERY_ROUND_WAIT_US;
    }
  } else {
    // 5 retries failed: this neighbour is out for this message, try the next.
    if (j->n_failed < JOB_FAILED_MAX) j->failed[j->n_failed++] = ex.target;
    log_json("next_candidate", "\"key\":\"%s\",\"failed\":\"0x%04X\"", ks, (unsigned)ex.target);
    j->target = ID_NONE;
    j->retries = 0;
    j->not_before_us = now;
  }
  exchange_done();
}

static void exchange_tick(int64_t now) {
  if (!ex.job->used || ex.job->key != ex.key) { // removed meanwhile (e.g. DELIVERY arrived)
    exchange_done();
    return;
  }
  if (ex.st == EX_WAIT_START) {
    if (now < ex.t_start) return;
    uint8_t buf[PKT_MAX];
    int n = build_packet(buf); // before CAD, so CAD -> TX is short
    if (n <= 0) {
      txq_remove(ex.job);
      exchange_done();
      return;
    }
    uint8_t ch = home_ch(ex.target);
    if (radio_cad_busy(ch)) { // not a retry
      stats.cad_busy++;
      ex.job->not_before_us = now + rand_between(1, 4) * PAIR_US;
      exchange_done();
      return;
    }
    if (ex.kind != JOB_DELIVERY) mesh_own_state(ex.key, MS_TRANSMITTING, 0);
    radio_tx(ch, buf, n);
    stats.tx++;
    char ks[11];
    key_str(ex.key, ks);
    log_json(ex.kind == JOB_DELIVERY ? "delivery_tx" : "data_tx", "\"key\":\"%s\",\"to\":\"0x%04X\",\"ch\":%u", ks,
             (unsigned)ex.target, (unsigned)ch);
    ex.st = EX_WAIT_ACK; // stay on the target's channel for the ACK
    return;
  }
  if (now > ex.t_deadline) exchange_failed(now);
}

void mac_on_ack(const CtlPkt& c, int64_t now) {
  if (ex.st != EX_WAIT_ACK || ex.kind == JOB_DELIVERY) return;
  if (c.from != ex.target || c.to != cfg.node_id || msg_key(c.orig_source, c.message_id) != ex.key) return;
  exchange_ok(now);
}

// DELIVERY carries no sender field, so the DELIVERY_ACK is matched on ack_from
// and the key only (packet design §6).
void mac_on_delivery_ack(const CtlPkt& c, int64_t now) {
  if (ex.st != EX_WAIT_ACK || ex.kind != JOB_DELIVERY) return;
  if (c.from != ex.target || msg_key(c.orig_source, c.message_id) != ex.key) return;
  exchange_ok(now);
}

// --- windows -------------------------------------------------------------

static void unsynced_tick(int64_t now) {
  radio_rx(CH_CONTROL);
#if !IS_DONGLE
  if (s_ini_pending && now >= s_ini_at) {
    if (send_beacon(true)) s_ini_pending = false;
    else s_ini_at = now + rand_between(SLOT_US, 4 * SLOT_US);
  }
#endif
  if (now - s_last_house_us >= sf_len_us()) {
    s_last_house_us = now;
    housekeeping(now);
  }
}

static void control_tick(int64_t now, int64_t sf, int slot) {
  radio_rx(CH_CONTROL);
#if !IS_DONGLE
  if (!near(sf, s_hello_sf, SLOT_US)) { // new superframe: pick this one's HELLO slot
    s_hello_sf = sf;
    s_hello_done = false;
    s_hello_slot = IS_HELPER ? 0 : (int)rand_between(1, cfg.ctrl_slots - 1);
    s_hello_jitter = IS_HELPER ? 0 : rand_between(0, 3) * JITTER_STEP_US;
  }
  if (!s_hello_done && slot == s_hello_slot && now >= sf + slot * SLOT_US + GUARD_US + s_hello_jitter) {
    s_hello_done = true; // busy channel: skip this superframe's HELLO
    bool ini = s_ini_pending;
    if (send_beacon(ini) && ini) s_ini_pending = false;
  }
#else
  (void)now;
  (void)sf;
  (void)slot;
#endif
}

static void data_tick(int64_t now, int64_t sf, int pair) {
  radio_rx(my_home());
  int64_t pair_start = sf + (int64_t)(cfg.ctrl_slots + 2 * pair) * SLOT_US;
  if (near(pair_start, s_pair_done, PAIR_US / 2)) return;
  s_pair_done = pair_start;
  if (now > pair_start + LATE_START_US) return;
  start_exchange(now, pair_start);
}

static void guard_tick(int64_t now, int64_t sf) {
  radio_rx(CH_CONTROL);
  if (near(sf, s_house_sf, SLOT_US)) return;
  s_house_sf = sf;
  s_last_house_us = now;
  housekeeping(now); // the only place a flash sector is erased
}

void mac_tick(int64_t now) {
  if (nbr_version != s_nbr_version_seen) { // a neighbour became ACTIVE or moved
    s_nbr_version_seen = nbr_version;
    txq_wake(false);
  }
  if (now - s_last_wake_us >= WAITING_RETRY_US) {
    s_last_wake_us = now;
    txq_wake(true);
  }
  if (now - s_last_stats_us >= STATS_EVERY_US) {
    s_last_stats_us = now;
    int active = 0;
    for (auto& n : nbrs)
      if (n.state == NB_ACTIVE) active++;
    log_json("stats", "\"rx\":%lu,\"tx\":%lu,\"crc_fail\":%lu,\"dup\":%lu,\"nbr_active\":%d,\"jobs\":%d",
             (unsigned long)stats.rx, (unsigned long)stats.tx, (unsigned long)(stats.rx_crc_hw + stats.rx_crc8),
             (unsigned long)stats.dup, active, txq_count());
  }

  if (ex.st != EX_IDLE) {
    exchange_tick(now);
    return;
  }
  if (!ts_synced()) {
    unsynced_tick(now);
    return;
  }
  int64_t sf = ts_sf_start(now);
  int slot = (int)((now - sf) / SLOT_US);
  int pairs = mac_pairs_per_sf();
  if (slot < cfg.ctrl_slots) control_tick(now, sf, slot);
  else if (slot < cfg.ctrl_slots + 2 * pairs) data_tick(now, sf, (slot - cfg.ctrl_slots) / 2);
  else guard_tick(now, sf);
}
