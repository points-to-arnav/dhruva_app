// Transmit jobs: retries and backoff (design/algorithm-design.md §7).
// Up to TXQ_MAX jobs in RAM; any other stored messages wait in flash and are
// reloaded by txq_refill().
#pragma once
#include <Arduino.h>

enum JobKind : uint8_t { JOB_DELIVERY = 0, JOB_OWN = 1, JOB_FORWARD = 2 }; // priority order

#define TXQ_MAX 32
#define JOB_FAILED_MAX 8
#define MAX_RETRIES 5
#define DELIVERY_ROUNDS 5

struct Job {
  bool used;
  uint8_t kind;
  uint32_t key;
  uint16_t target;         // current next hop / to_id, 0 = pick one
  uint8_t retries;         // 0..MAX_RETRIES for the current target
  uint8_t rounds;          // DELIVERY: rounds of retries so far
  uint8_t n_failed;
  uint16_t failed[JOB_FAILED_MAX]; // targets that failed for this message
  int64_t not_before_us;   // backoff
  int64_t created_us;
  bool waiting;            // no candidate: re-evaluated on neighbour changes and every 60 s
  int64_t waiting_since_us;
};

extern Job jobs[TXQ_MAX];

Job* txq_add(uint8_t kind, uint32_t key, int64_t now);
Job* txq_find(uint8_t kind, uint32_t key);
void txq_remove(Job* j);
// DELIVERY first, then OWN, then FORWARD, oldest first; due and not waiting.
Job* txq_next_due(int64_t t);
void txq_wake(bool clear_failed);
void txq_refill(int64_t now);
int txq_count();
