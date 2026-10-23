#include "neighbor.h"
#include "role.h"
#include "mesh_config.h"
#include "mesh_log.h"
#include "mesh.h"
#include "geo.h"
#include "timesync.h"

#define ACTIVE_HELLOS 3
#define ACTIVE_SNR_DB -10.0f
#define SNR_ALPHA 0.25f
#define STALE_ACK_MIN_SENDS 5
#define STALE_ACK_RATE 0.5f

Neighbor nbrs[NBR_MAX];
uint32_t nbr_version = 0;

const char* nbr_state_name(uint8_t s) {
  switch (s) {
    case NB_HEARD: return "HEARD";
    case NB_ACTIVE: return "ACTIVE";
    case NB_STALE: return "STALE";
  }
  return "EMPTY";
}

static void log_state(const Neighbor& n, const char* why) {
  log_json("nbr", "\"id\":\"0x%04X\",\"state\":\"%s\",\"why\":\"%s\",\"rssi\":%d,\"snr\":%.1f", (unsigned)n.id,
           nbr_state_name(n.state), why, n.rssi, n.snr_avg);
}

Neighbor* nbr_find(uint16_t id) {
  for (auto& n : nbrs)
    if (n.state != NB_EMPTY && n.id == id) return &n;
  return nullptr;
}

// A free slot, else the oldest non-ACTIVE entry. ACTIVE ones are never pushed out.
static Neighbor* nbr_alloc(int64_t now) {
  Neighbor* oldest = nullptr;
  for (auto& n : nbrs) {
    if (n.state == NB_EMPTY) return &n;
    if (n.state != NB_ACTIVE && (!oldest || now - n.last_seen_us > now - oldest->last_seen_us)) oldest = &n;
  }
  return oldest;
}

float nbr_ack_rate(const Neighbor& n) {
  if (n.ack_n == 0) return 1.0f; // untried links get tried
  uint16_t mask = (1u << n.ack_n) - 1;
  return (float)__builtin_popcount(n.ack_bits & mask) / n.ack_n;
}

// Routing order (algorithm design §5): progress > 0 first, then ACK success
// rate, then SNR.
bool nbr_better(const Neighbor& a, const Neighbor& b) {
  bool pa = a.progress_m > 0, pb = b.progress_m > 0;
  if (pa != pb) return pa;
  float ra = nbr_ack_rate(a), rb = nbr_ack_rate(b);
  if (ra != rb) return ra > rb;
  return a.snr_avg > b.snr_avg;
}

static bool is_helper(const Neighbor& n) { return n.id == cfg.helper_id || n.role == ROLE_HELPER; }

static float dist_to_helper(int32_t lat, int32_t lon) {
  return geo_dist_m(lat, lon, cfg.helper_lat_e7, cfg.helper_lon_e7);
}

static void update_progress(Neighbor& n, float my_dist) {
  float theirs = is_helper(n) ? 0 : dist_to_helper(n.lat, n.lon);
  n.progress_m = my_dist - theirs;
}

// Progress for everyone (my GPS position moves too) and the forwarding set.
void nbr_recompute() {
  MyPos me = mesh_my_pos();
  float my_dist = cfg.node_id == cfg.helper_id ? 0 : dist_to_helper(me.lat, me.lon);
  for (auto& n : nbrs) {
    n.fwd_set = false;
    if (n.state != NB_EMPTY) update_progress(n, my_dist);
  }
  for (int k = 0; k < FWD_SET_MAX; k++) {
    Neighbor* best = nullptr;
    for (auto& n : nbrs)
      if (n.state == NB_ACTIVE && !n.fwd_set && (!best || nbr_better(n, *best))) best = &n;
    if (!best) break;
    best->fwd_set = true;
  }
}

void nbr_on_beacon(const Beacon& b, const RxFrame& f, int64_t now) {
  Neighbor* n = nbr_find(b.node_id);
  bool first = !n;
  if (first) {
    n = nbr_alloc(now);
    if (!n) return;
    memset(n, 0, sizeof *n);
    n->id = b.node_id;
    n->state = NB_HEARD;
  }
  bool moved = !first && (n->lat != b.lat || n->lon != b.lon);

  if (!b.ini) {
    if (n->seq_valid) {
      uint8_t gap = (uint8_t)(b.seq - n->last_seq);
      if (gap > 1) n->missed += gap - 1;
    }
    n->last_seq = b.seq;
    n->seq_valid = true;
  }
  n->role = rf_role(b.role_flags);
  n->gps_fix = rf_fix(b.role_flags);
  if (b.ini) n->helper_id = b.helper_id;
  n->lat = b.lat; // balloons drift: always overwrite
  n->lon = b.lon;
  n->alt = b.alt;
  n->acc_dm = b.pos_acc_dm;
  n->rssi = f.rssi;
  n->snr = f.snr;
  n->snr_avg = first ? f.snr : n->snr_avg + SNR_ALPHA * (f.snr - n->snr_avg);
  n->last_seen_us = now;

  uint8_t old = n->state;
  if (n->state == NB_STALE) {
    // Probation: earn ACTIVE again with fresh HELLOs and a clean ACK history.
    n->state = NB_HEARD;
    n->hello_count = 0;
    n->ack_bits = 0;
    n->ack_n = 0;
  }
  if (n->hello_count < 255) n->hello_count++;
  if (n->state == NB_HEARD && n->hello_count >= ACTIVE_HELLOS && n->snr_avg > ACTIVE_SNR_DB) n->state = NB_ACTIVE;
  else if (n->state == NB_ACTIVE && n->snr_avg <= ACTIVE_SNR_DB) n->state = NB_HEARD;

  if (first) log_state(*n, "heard");
  else if (n->state != old) log_state(*n, n->state == NB_ACTIVE ? "hellos" : "snr");
  if ((n->state == NB_ACTIVE && old != NB_ACTIVE) || (moved && n->state == NB_ACTIVE)) nbr_version++;
  nbr_recompute();
}

void nbr_tick(int64_t now) {
  int64_t interval = sf_len_us(); // one HELLO per superframe
  bool changed = false;
  for (auto& n : nbrs) {
    if (n.state == NB_EMPTY) continue;
    int64_t age = now - n.last_seen_us;
    if (age > 10 * interval) {
      log_json("nbr", "\"id\":\"0x%04X\",\"state\":\"EMPTY\",\"why\":\"forgotten\"", (unsigned)n.id);
      n.state = NB_EMPTY;
      changed = true;
    } else if (n.state != NB_STALE && age > 3 * interval + interval / 2) {
      n.state = NB_STALE; // 3 consecutive HELLOs missed
      log_state(n, "silent");
      changed = true;
    }
  }
  if (changed) nbr_recompute();
}

void nbr_ack_result(uint16_t id, bool ok) {
  Neighbor* n = nbr_find(id);
  if (!n) return;
  n->sent++;
  if (ok) n->acked++;
  else n->failed++;
  n->ack_bits = (uint16_t)((n->ack_bits << 1 | (ok ? 1 : 0)) & ((1u << ACK_WINDOW) - 1));
  if (n->ack_n < ACK_WINDOW) n->ack_n++;
  if (n->state == NB_ACTIVE && n->ack_n >= STALE_ACK_MIN_SENDS && nbr_ack_rate(*n) < STALE_ACK_RATE) {
    n->state = NB_STALE;
    log_state(*n, "ack rate");
  }
  nbr_recompute();
}

bool nbr_recent(const Neighbor& n, int64_t now) {
  int64_t interval = sf_len_us();
  return n.state != NB_EMPTY && now - n.last_seen_us <= 3 * interval + interval / 2;
}

int nbr_candidates(int64_t now) {
  int c = 0;
  for (auto& n : nbrs)
    if (nbr_recent(n, now)) c++;
  return c;
}
