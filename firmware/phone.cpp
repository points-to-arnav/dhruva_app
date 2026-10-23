#include "phone.h"
#include "role.h"

#if IS_DONGLE
#include <math.h>
#include <esp_timer.h>
#include "mesh_config.h"
#include "mesh_log.h"
#include "mesh.h"
#include "neighbor.h"
#include "packet.h"

#define INFO_CHECK_US 500000LL
#define STATUS_MEMORY 16

static int s_nodes_reported = -1;
static int64_t s_info_checked_us = 0;

// Last state sent per message, so each state goes to the app once.
static struct {
  uint32_t key;
  uint8_t state;
} s_sent[STATUS_MEMORY];
static int s_sent_next = 0;

void phone_status(uint32_t key, uint8_t state, uint16_t next_hop) {
  int slot = -1;
  for (int k = 0; k < STATUS_MEMORY; k++)
    if (s_sent[k].key == key) slot = k;
  if (slot >= 0 && s_sent[slot].state == state) return;
  if (slot < 0) {
    slot = s_sent_next;
    s_sent_next = (s_sent_next + 1) % STATUS_MEMORY;
    s_sent[slot].key = key;
  }
  s_sent[slot].state = state;
  char ks[11];
  key_str(key, ks);
  if (state == MS_SENT_TO_MESH) Serial.printf("STATUS|%s|%s|0x%04X\n", ks, msg_state_name(state), (unsigned)next_hop);
  else Serial.printf("STATUS|%s|%s\n", ks, msg_state_name(state));
}

static void send_info() {
  // Battery is "-" until a battery-sense pin is wired (design/plan.md §12).
  Serial.printf("INFO|-|%d\n", s_nodes_reported < 0 ? 0 : s_nodes_reported);
}

static bool reject(const char* why) {
  log_line("SEND rejected: %s", why);
  return true;
}

// SEND|<people>|<flags>|<lat>|<lon>|<alt>|<acc>|<unix_time>
static bool handle_send(char* line) {
  char* f[9];
  int n = 0;
  for (char* p = line; n < 9;) { // split on '|', keeping empty fields
    f[n++] = p;
    char* bar = strchr(p, '|');
    if (!bar) break;
    *bar = 0;
    p = bar + 1;
  }
  if (n != 8) return reject("expected 8 fields");
  char* end;
  long people = strtol(f[1], &end, 10);
  if (*end || people < 1 || people > 100) return reject("people must be 1-100");
  const char* fl = f[2];
  if (fl[0] == '0' && (fl[1] == 'x' || fl[1] == 'X')) fl += 2;
  unsigned long flags = strtoul(fl, &end, 16);
  if (*end || !*fl || flags > 0x1F) return reject("flags must be 0x00-0x1F");
  double lat = strtod(f[3], &end);
  if (*end || lat < -90 || lat > 90) return reject("bad latitude");
  double lon = strtod(f[4], &end);
  if (*end || lon < -180 || lon > 180) return reject("bad longitude");
  long alt = strtol(f[5], &end, 10);
  if (*end) return reject("bad altitude");
  long acc = strtol(f[6], &end, 10);
  if (*end || acc < 0 || acc > 255) return reject("acc must be 0-255");
  unsigned long ts = strtoul(f[7], &end, 10);
  if (*end) return reject("bad time");

  int32_t lat_e7 = (int32_t)lround(lat * 1e7);
  int32_t lon_e7 = (int32_t)lround(lon * 1e7);
  if (alt < INT16_MIN) alt = INT16_MIN;
  if (alt > INT16_MAX) alt = INT16_MAX;
  // Phone has no fix: the dongle's stored position if it has one (still "no fix").
  if (acc == ACC8_NONE && lat_e7 == 0 && lon_e7 == 0 && cfg.pos_set) {
    lat_e7 = cfg.lat_e7;
    lon_e7 = cfg.lon_e7;
    alt = cfg.alt_m;
  }
  mesh_create_message((uint8_t)people, (uint8_t)flags, lat_e7, lon_e7, (int16_t)alt, (uint8_t)acc, (uint32_t)ts);
  return true;
}

bool phone_line(char* line) {
  if (strcmp(line, "INFO?") == 0) {
    s_nodes_reported = nbr_candidates(esp_timer_get_time());
    send_info();
    return true;
  }
  if (strncmp(line, "SEND|", 5) == 0) return handle_send(line);
  return false;
}

void phone_tick(int64_t now) {
  if (now - s_info_checked_us < INFO_CHECK_US) return;
  s_info_checked_us = now;
  int nodes = nbr_candidates(now);
  if (nodes == s_nodes_reported) return;
  s_nodes_reported = nodes;
  send_info();
}

#else

bool phone_line(char*) { return false; }
void phone_status(uint32_t, uint8_t, uint16_t) {}
void phone_tick(int64_t) {}

#endif
