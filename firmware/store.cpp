#include "store.h"
#include <esp_partition.h>
#include "packet.h"
#include "mesh_log.h"

// Partition `mesh` (partitions.csv): message log, then seen-set log.
#define PART_SUBTYPE 0x40
#define SECTOR 4096
#define REC_SIZE 64
#define REC_PER_SECTOR (SECTOR / REC_SIZE)            // 64
#define MSG_SECTORS (MSG_RECS / REC_PER_SECTOR)       // 32 -> 128 KB
#define SEEN_PER_SECTOR (SECTOR / 4)                  // 1024
#define SEEN_SECTORS 12                               // 48 KB, >= 11,264 keys kept after an erase
#define SEEN_KEYS (SEEN_SECTORS * SEEN_PER_SECTOR)
#define MSG_BASE 0
#define SEEN_BASE (MSG_SECTORS * SECTOR)
#define ERASED_TARGET 2                               // erased sectors kept ahead of each head
#define REC_MAGIC 0xA5
#define SEEN_EMPTY 0xFFFFFFFFUL

// Record layout (64 B). magic is written last, so a torn write never looks valid.
#define R_MAGIC 0
#define R_STATE 1
#define R_DSTATE 2
#define R_OWN 3     // 0x00 own, 0xFF relayed
#define R_PREV 4
#define R_NEXT 6
#define R_LEN 8
#define R_DATA 9    // raw DATA as received, <= 51 B
#define R_HELPER 60

MsgIdx msg_idx[MSG_RECS];
static uint32_t s_seen[SEEN_KEYS];
static const esp_partition_t* s_part = nullptr;
static uint8_t s_buf[SECTOR];

// A ring of sectors. Slots from head to the end of the head's sector are
// erased when head_ready; the erased_ahead sectors after it are fully erased;
// the sector after those holds the oldest data.
struct Ring {
  int head;
  bool head_ready;
  int erased_ahead;
  int sectors;
  int per_sector;
};
static Ring s_msg = {0, true, 0, MSG_SECTORS, REC_PER_SECTOR};
static Ring s_seen = {0, true, 0, SEEN_SECTORS, SEEN_PER_SECTOR};

static uint16_t get16(const uint8_t* p) { return p[0] | (uint16_t)p[1] << 8; }

static bool flash_read(uint32_t off, void* p, size_t n) { return esp_partition_read(s_part, off, p, n) == ESP_OK; }
static bool flash_write(uint32_t off, const void* p, size_t n) { return esp_partition_write(s_part, off, p, n) == ESP_OK; }
static bool flash_erase(uint32_t off) { return esp_partition_erase_range(s_part, off, SECTOR) == ESP_OK; }
static uint32_t rec_off(int i) { return MSG_BASE + (uint32_t)i * REC_SIZE; }

static void ring_advance(Ring& r) {
  r.head = (r.head + 1) % (r.sectors * r.per_sector);
  if (r.head % r.per_sector == 0) { // entered the next sector
    if (r.erased_ahead > 0) {
      r.erased_ahead--;
      r.head_ready = true;
    } else {
      r.head_ready = false; // must be erased before the next write
    }
  }
}

// Head = the start of the longest run of unused slots that follows a used one.
static void ring_find_head(Ring& r, const bool* used) {
  int n = r.sectors * r.per_sector;
  int best = -1, best_run = -1;
  bool any = false;
  for (int i = 0; i < n; i++) {
    if (used[i]) { any = true; continue; }
    if (!used[(i + n - 1) % n]) continue;
    int run = 0;
    while (run < n && !used[(i + run) % n]) run++;
    if (run > best_run) { best_run = run; best = i; }
  }
  if (!any) best = 0;
  if (best < 0) { // completely full: the oldest data is unknown, start at 0
    r.head = 0;
    r.head_ready = false;
    r.erased_ahead = 0;
    return;
  }
  r.head = best;
  int s0 = best / r.per_sector;
  r.head_ready = true;
  for (int i = best; i < (s0 + 1) * r.per_sector; i++)
    if (used[i]) r.head_ready = false;
  r.erased_ahead = 0;
  for (int k = 1; k < r.sectors; k++) {
    int s = (s0 + k) % r.sectors;
    bool empty = true;
    for (int i = s * r.per_sector; i < (s + 1) * r.per_sector && empty; i++)
      if (used[i]) empty = false;
    if (!empty) break;
    r.erased_ahead++;
  }
}

static bool s_used[MSG_RECS > SEEN_KEYS ? MSG_RECS : SEEN_KEYS];

static void scan_messages() {
  for (int s = 0; s < MSG_SECTORS; s++) {
    flash_read(MSG_BASE + s * SECTOR, s_buf, SECTOR);
    for (int r = 0; r < REC_PER_SECTOR; r++) {
      const uint8_t* p = s_buf + r * REC_SIZE;
      int i = s * REC_PER_SECTOR + r;
      bool used = false;
      for (int k = 0; k < REC_SIZE && !used; k++) used = p[k] != 0xFF;
      s_used[i] = used;
      MsgIdx& m = msg_idx[i];
      memset(&m, 0, sizeof m);
      uint8_t len = p[R_LEN];
      if (p[R_MAGIC] != REC_MAGIC || len < DATA_LEN(1) || len > PKT_MAX || (p[R_DATA] & 0x0F) != PT_DATA) continue;
      m.key = msg_key(get16(p + R_DATA + 1), get16(p + R_DATA + 3));
      m.state = p[R_STATE];
      m.dstate = p[R_DSTATE];
      m.own = p[R_OWN] == 0x00;
      m.prev_hop = get16(p + R_PREV);
      m.next_hop = get16(p + R_NEXT);
      m.helper_id = get16(p + R_HELPER);
      m.valid = true;
    }
  }
  ring_find_head(s_msg, s_used);
  // A power cut during a relocation can leave two copies: keep the newer.
  for (int i = 0; i < MSG_RECS; i++) {
    if (!msg_idx[i].valid) continue;
    int age_i = (s_msg.head - i + MSG_RECS) % MSG_RECS;
    for (int j = i + 1; j < MSG_RECS; j++) {
      if (!msg_idx[j].valid || msg_idx[j].key != msg_idx[i].key) continue;
      int age_j = (s_msg.head - j + MSG_RECS) % MSG_RECS;
      if (age_j < age_i) { msg_idx[i].valid = false; break; }
      msg_idx[j].valid = false;
    }
  }
}

static void scan_seen() {
  for (int s = 0; s < SEEN_SECTORS; s++) flash_read(SEEN_BASE + s * SECTOR, s_seen + s * SEEN_PER_SECTOR, SECTOR);
  for (int i = 0; i < SEEN_KEYS; i++) s_used[i] = s_seen[i] != SEEN_EMPTY;
  ring_find_head(s_seen, s_used);
}

bool store_begin() {
  s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)PART_SUBTYPE, "mesh");
  if (!s_part || s_part->size < SEEN_BASE + SEEN_SECTORS * SECTOR) {
    s_part = nullptr;
    return false;
  }
  scan_messages();
  scan_seen();
  log_line("store: %d messages, head %d (%d sectors erased ahead), seen head %d", store_count(ST_STORED) +
           store_count(ST_SENT) + store_count(ST_DELIVERED) + store_count(ST_FAILED), s_msg.head,
           s_msg.erased_ahead, s_seen.head);
  return true;
}

bool store_ready() { return s_part != nullptr; }

int store_find(uint32_t key) {
  for (int i = 0; i < MSG_RECS; i++)
    if (msg_idx[i].valid && msg_idx[i].key == key) return i;
  return -1;
}

int store_count(uint8_t state) {
  int c = 0;
  for (auto& m : msg_idx)
    if (m.valid && m.state == state) c++;
  return c;
}

// Never evicted: not handed off yet, or a DELIVERY still owed.
static bool precious(const MsgIdx& m) { return m.valid && (m.state == ST_STORED || m.dstate == DS_OWED); }

static bool sector_has_precious(int s) {
  for (int k = 0; k < REC_PER_SECTOR; k++)
    if (precious(msg_idx[s * REC_PER_SECTOR + k])) return true;
  return false;
}

static void forget_sector(int s) {
  for (int k = 0; k < REC_PER_SECTOR; k++) msg_idx[s * REC_PER_SECTOR + k].valid = false;
}

// Writes a full record at the head: everything but the magic, then the magic.
static int write_record(uint8_t* rec) {
  if (!s_msg.head_ready) return -1;
  int i = s_msg.head;
  if (!flash_write(rec_off(i) + 1, rec + 1, REC_SIZE - 1)) return -1;
  uint8_t magic = REC_MAGIC;
  if (!flash_write(rec_off(i), &magic, 1)) return -1;
  ring_advance(s_msg);
  return i;
}

static bool relocate(int i) {
  uint8_t rec[REC_SIZE];
  if (!flash_read(rec_off(i), rec, REC_SIZE)) return false;
  int j = write_record(rec);
  if (j < 0) return false;
  msg_idx[j] = msg_idx[i];
  msg_idx[i].valid = false;
  return true;
}

static bool full() {
  log_json("store_full", "\"held\":%d", store_count(ST_STORED));
  return false;
}

// One erase step for the message ring. False if nothing was erased.
static bool msg_erase_step() {
  Ring& r = s_msg;
  int hs = r.head / REC_PER_SECTOR;
  if (!r.head_ready) {
    // No erased space at all: the head's sector holds the oldest data.
    if (sector_has_precious(hs)) return full();
    if (!flash_erase(MSG_BASE + hs * SECTOR)) return false;
    forget_sector(hs);
    r.head_ready = true;
    return true;
  }
  if (r.erased_ahead >= ERASED_TARGET) return false;
  int v = (hs + 1 + r.erased_ahead) % MSG_SECTORS; // oldest data
  if (v == hs) return false;
  // Copy what must not be lost to the head first. Relocation may not take the
  // last free slot before v, or the head would run into v.
  for (int k = 0; k < REC_PER_SECTOR; k++) {
    int i = v * REC_PER_SECTOR + k;
    if (!precious(msg_idx[i])) continue;
    bool last_slot = r.head % REC_PER_SECTOR == REC_PER_SECTOR - 1 && r.erased_ahead == 0;
    if (!r.head_ready || last_slot) return full();
    if (!relocate(i)) return false;
  }
  if (!flash_erase(MSG_BASE + v * SECTOR)) return false;
  forget_sector(v);
  r.erased_ahead++;
  return true;
}

static bool seen_erase_step() {
  Ring& r = s_seen;
  if (!r.head_ready || r.erased_ahead >= ERASED_TARGET) return false;
  int hs = r.head / SEEN_PER_SECTOR;
  int v = (hs + 1 + r.erased_ahead) % SEEN_SECTORS;
  if (v == hs || !flash_erase(SEEN_BASE + v * SECTOR)) return false;
  for (int k = 0; k < SEEN_PER_SECTOR; k++) s_seen[v * SEEN_PER_SECTOR + k] = SEEN_EMPTY;
  r.erased_ahead++;
  return true;
}

bool store_can_accept() {
  if (!s_part) return false;
  return s_msg.head_ready || !sector_has_precious(s_msg.head / REC_PER_SECTOR);
}

int store_append(const uint8_t* data, uint8_t len, uint16_t prev_hop, bool own, uint8_t state) {
  if (!s_part || len < DATA_LEN(1) || len > PKT_MAX) return -1;
  // Only when housekeeping fell behind: a synchronous erase (slow, rare).
  if (!s_msg.head_ready && !msg_erase_step()) return -1;
  uint8_t rec[REC_SIZE];
  memset(rec, 0xFF, sizeof rec);
  rec[R_MAGIC] = REC_MAGIC;
  rec[R_STATE] = state;
  rec[R_OWN] = own ? 0x00 : 0xFF;
  rec[R_PREV] = prev_hop;
  rec[R_PREV + 1] = prev_hop >> 8;
  rec[R_LEN] = len;
  memcpy(rec + R_DATA, data, len);
  int i = write_record(rec);
  if (i < 0) return -1;
  MsgIdx& m = msg_idx[i];
  m.key = msg_key(get16(data + 1), get16(data + 3));
  m.prev_hop = prev_hop;
  m.next_hop = 0xFFFF;
  m.helper_id = 0xFFFF;
  m.state = state;
  m.dstate = DS_NONE;
  m.own = own;
  m.valid = true;
  return i;
}

int store_read(int i, uint8_t* buf) {
  if (!s_part || i < 0 || i >= MSG_RECS || !msg_idx[i].valid) return -1;
  uint8_t len;
  if (!flash_read(rec_off(i) + R_LEN, &len, 1) || len > PKT_MAX) return -1;
  if (!flash_read(rec_off(i) + R_DATA, buf, len)) return -1;
  return len;
}

// NOR flash: a byte can only lose bits without an erase.
static bool program_byte(int i, int off, uint8_t cur, uint8_t v) {
  if (v == cur) return false;
  if ((cur & v) != v) {
    log_line("store: illegal state change 0x%02X -> 0x%02X", cur, v);
    return false;
  }
  return flash_write(rec_off(i) + off, &v, 1);
}

void store_set_state(int i, uint8_t st) {
  if (i < 0 || !msg_idx[i].valid) return;
  if (program_byte(i, R_STATE, msg_idx[i].state, st)) msg_idx[i].state = st;
}

void store_set_dstate(int i, uint8_t ds) {
  if (i < 0 || !msg_idx[i].valid) return;
  if (program_byte(i, R_DSTATE, msg_idx[i].dstate, ds)) msg_idx[i].dstate = ds;
}

static void program_id(int i, int off, uint16_t& cur, uint16_t id) {
  if (cur != 0xFFFF) return; // programmed once
  uint8_t b[2] = {(uint8_t)id, (uint8_t)(id >> 8)};
  if (flash_write(rec_off(i) + off, b, 2)) cur = id;
}

void store_set_next_hop(int i, uint16_t id) {
  if (i >= 0 && msg_idx[i].valid) program_id(i, R_NEXT, msg_idx[i].next_hop, id);
}

void store_set_helper(int i, uint16_t id) {
  if (i >= 0 && msg_idx[i].valid) program_id(i, R_HELPER, msg_idx[i].helper_id, id);
}

bool seen_has(uint32_t key) {
  for (int i = 0; i < SEEN_KEYS; i++)
    if (s_seen[i] == key) return true;
  return false;
}

bool seen_add(uint32_t key) {
  if (!s_part) return false;
  Ring& r = s_seen;
  if (!r.head_ready) { // housekeeping fell behind: erase the oldest sector now
    int hs = r.head / SEEN_PER_SECTOR;
    if (!flash_erase(SEEN_BASE + hs * SECTOR)) return false;
    for (int k = 0; k < SEEN_PER_SECTOR; k++) s_seen[hs * SEEN_PER_SECTOR + k] = SEEN_EMPTY;
    r.head_ready = true;
  }
  if (!flash_write(SEEN_BASE + (uint32_t)r.head * 4, &key, 4)) return false;
  s_seen[r.head] = key;
  ring_advance(r);
  return true;
}

void store_housekeep() {
  if (!s_part) return;
  if (!msg_erase_step()) seen_erase_step();
}

void store_erase_all() {
  if (!s_part) return;
  for (int s = 0; s < MSG_SECTORS; s++) flash_erase(MSG_BASE + s * SECTOR);
  for (int s = 0; s < SEEN_SECTORS; s++) flash_erase(SEEN_BASE + s * SECTOR);
  memset(msg_idx, 0, sizeof msg_idx);
  for (int i = 0; i < SEEN_KEYS; i++) s_seen[i] = SEEN_EMPTY;
  s_msg = {0, true, MSG_SECTORS - 1, MSG_SECTORS, REC_PER_SECTOR};
  s_seen = {0, true, SEEN_SECTORS - 1, SEEN_SECTORS, SEEN_PER_SECTOR};
}
