#include "txq.h"
#include "store.h"

Job jobs[TXQ_MAX];

Job* txq_find(uint8_t kind, uint32_t key) {
  for (auto& j : jobs)
    if (j.used && j.kind == kind && j.key == key) return &j;
  return nullptr;
}

Job* txq_add(uint8_t kind, uint32_t key, int64_t now) {
  if (Job* j = txq_find(kind, key)) return j;
  for (auto& j : jobs) {
    if (j.used) continue;
    memset(&j, 0, sizeof j);
    j.used = true;
    j.kind = kind;
    j.key = key;
    j.not_before_us = now;
    j.created_us = now;
    return &j;
  }
  return nullptr; // full: the message stays in flash until txq_refill()
}

void txq_remove(Job* j) {
  if (j) j->used = false;
}

Job* txq_next_due(int64_t t) {
  Job* best = nullptr;
  for (auto& j : jobs) {
    if (!j.used || j.waiting || j.not_before_us > t) continue;
    if (!best || j.kind < best->kind || (j.kind == best->kind && j.created_us < best->created_us)) best = &j;
  }
  return best;
}

void txq_wake(bool clear_failed) {
  for (auto& j : jobs) {
    if (!j.used || !j.waiting) continue;
    j.waiting = false;
    if (clear_failed && j.kind != JOB_DELIVERY) { // recovered neighbours get tried again
      j.n_failed = 0;
      j.target = 0;
      j.retries = 0;
    }
  }
}

void txq_refill(int64_t now) {
  for (int i = 0; i < MSG_RECS; i++) {
    const MsgIdx& m = msg_idx[i];
    if (!m.valid) continue;
    if (m.state == ST_STORED && !txq_add(m.own ? JOB_OWN : JOB_FORWARD, m.key, now)) return;
    if (m.dstate == DS_OWED && !txq_add(JOB_DELIVERY, m.key, now)) return;
  }
}

int txq_count() {
  int c = 0;
  for (auto& j : jobs)
    if (j.used) c++;
  return c;
}
