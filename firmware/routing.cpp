#include "routing.h"
#include "role.h"
#include "mesh_config.h"
#include "neighbor.h"
#include "packet.h"

static bool listed(uint16_t id, const uint16_t* ids, uint8_t n) {
  for (uint8_t k = 0; k < n; k++)
    if (ids[k] == id) return true;
  return false;
}

uint16_t route_next_hop(const uint16_t* path, uint8_t path_n, const uint16_t* failed, uint8_t n_failed,
                        int64_t now) {
  auto usable = [&](const Neighbor& n) {
    return n.state != NB_EMPTY && n.id != cfg.node_id && !listed(n.id, path, path_n) && // loop prevention
           !listed(n.id, failed, n_failed);
  };

#if IS_DONGLE
  // Candidates: infra nodes heard in the last 3 HELLO intervals. Highest SNR;
  // within 1 dB of it, the most progress to the helper.
  float max_snr = -1000;
  for (auto& n : nbrs)
    if (usable(n) && nbr_recent(n, now) && n.snr_avg > max_snr) max_snr = n.snr_avg;
  const Neighbor* best = nullptr;
  for (auto& n : nbrs)
    if (usable(n) && nbr_recent(n, now) && n.snr_avg >= max_snr - 1.0f && (!best || n.progress_m > best->progress_m))
      best = &n;
  return best ? best->id : ID_NONE;
#else
  (void)now;
  // 1-3: the forwarding set, progress > 0 first (recovery otherwise),
  // then ACK rate, then SNR.
  const Neighbor* best = nullptr;
  for (auto& n : nbrs)
    if (usable(n) && n.state == NB_ACTIVE && n.fwd_set && (!best || nbr_better(n, *best))) best = &n;
  if (best) return best->id;
  // 4: forwarding set exhausted -> any other ACTIVE neighbour.
  for (auto& n : nbrs)
    if (usable(n) && n.state == NB_ACTIVE && (!best || nbr_better(n, *best))) best = &n;
  return best ? best->id : ID_NONE;
#endif
}
