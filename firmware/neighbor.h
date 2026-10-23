// Neighbour table from HELLO / INI (design/algorithm-design.md §4).
// Dongles use the same table as their candidate list.
#pragma once
#include <Arduino.h>
#include "packet.h"
#include "radio.h"

enum NbrState : uint8_t { NB_EMPTY = 0, NB_HEARD, NB_ACTIVE, NB_STALE };

#define NBR_MAX 32
#define FWD_SET_MAX 4   // outbound forwarding neighbours
#define ACK_WINDOW 10   // ACK success rate over the last 10 sends

struct Neighbor {
  uint16_t id;
  uint8_t role;
  bool gps_fix;
  uint16_t helper_id;     // from INI
  int32_t lat, lon;
  int16_t alt;
  uint16_t acc_dm;
  int16_t rssi;
  float snr, snr_avg;     // last, EWMA (alpha 0.25)
  int64_t last_seen_us;
  uint8_t last_seq;
  bool seq_valid;
  uint8_t hello_count;
  uint16_t missed;        // HELLO sequence gaps
  uint16_t ack_bits;      // last ACK_WINDOW sends, bit 0 = newest, 1 = ACKed
  uint8_t ack_n;
  uint32_t sent, acked, failed;
  uint8_t state;
  float progress_m;       // my distance to the helper minus theirs
  bool fwd_set;           // one of the best FWD_SET_MAX ACTIVE neighbours
};

extern Neighbor nbrs[NBR_MAX];
// Bumps when a neighbour becomes ACTIVE or an ACTIVE one moves: waiting jobs retry.
extern uint32_t nbr_version;

void nbr_on_beacon(const Beacon& b, const RxFrame& f, int64_t now);
void nbr_tick(int64_t now);
void nbr_ack_result(uint16_t id, bool ok);
void nbr_recompute();
Neighbor* nbr_find(uint16_t id);
float nbr_ack_rate(const Neighbor& n);
bool nbr_better(const Neighbor& a, const Neighbor& b); // routing order
bool nbr_recent(const Neighbor& n, int64_t now);       // heard in the last 3 HELLO intervals
int nbr_candidates(int64_t now);                       // dongle INFO "nodes"
const char* nbr_state_name(uint8_t s);
