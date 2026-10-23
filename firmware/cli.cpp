#include "cli.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <math.h>
#include <stdarg.h>
#include "role.h"
#include "mesh_config.h"
#include "mesh_log.h"
#include "mesh.h"
#include "radio.h"
#include "timesync.h"
#include "neighbor.h"
#include "store.h"
#include "txq.h"
#include "mac.h"
#include "phone.h"

#define CLI_LINE_MAX 160

static char s_line[CLI_LINE_MAX];
static int s_len = 0;

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.printf("# %s\n", buf);
}

static void help() {
  say("show cfg | nbr | msgs | stats | sync");
  say("set id 0x0011                       (after save + reboot)");
#if IS_RELAY
  say("set role balloon|ground             (after save + reboot)");
#endif
  say("set helper 0x0001 27.752000 85.320000");
  say("set pos 27.716000 85.320000 1300");
  say("set txp 3 | set range 5 | set txlat 0");
  say("set sf_slots 25 | set ctrl_slots 4  (after save + reboot, same on every node)");
  say("save | reboot | erase store");
#if !IS_HELPER
  say("send <people> <flags_hex>           (test message)");
#endif
}

static double deg(int32_t e7) { return e7 / 1e7; }

static void show_cfg() {
  const MeshConfig& c = cfg;
  say("id 0x%04X, role %s, home ch %u", (unsigned)c.node_id,
      IS_HELPER ? "helper" : IS_DONGLE ? "dongle" : (c.relay_role == ROLE_GROUND ? "ground" : "balloon"),
      (unsigned)home_ch(c.node_id));
  say("helper 0x%04X at %.6f %.6f", (unsigned)c.helper_id, deg(c.helper_lat_e7), deg(c.helper_lon_e7));
  if (c.pos_set) say("pos %.6f %.6f %d m", deg(c.lat_e7), deg(c.lon_e7), (int)c.alt_m);
  else say("pos not set");
  say("txp %d dBm, range %.1f km, txlat %ld us", (int)c.txp_dbm, c.range_km, (long)c.tx_latency_us);
  say("superframe %u slots, %u control, %u pairs", (unsigned)c.sf_slots, (unsigned)c.ctrl_slots,
      (unsigned)mac_pairs_per_sf());
  if (memcmp(&cfg, &cfg_next, sizeof cfg)) say("(unsaved or pending-reboot changes)");
}

static void show_nbr() {
  int64_t now = esp_timer_get_time();
  int n = 0;
  for (auto& b : nbrs) {
    if (b.state == NB_EMPTY) continue;
    n++;
    say("0x%04X %-6s%s role %u rssi %d snr %.1f progress %.0f m ack %.0f%% (%lu/%lu) missed %u age %lds%s",
        (unsigned)b.id, nbr_state_name(b.state), b.fwd_set ? "*" : " ", (unsigned)b.role, b.rssi, b.snr_avg,
        b.progress_m, nbr_ack_rate(b) * 100, (unsigned long)b.acked, (unsigned long)b.sent, (unsigned)b.missed,
        (long)((now - b.last_seen_us) / 1000000), b.gps_fix ? " gps" : "");
  }
  say("%d neighbours (* = forwarding set)", n);
}

static const char* st_name(uint8_t s) {
  switch (s) {
    case ST_STORED: return "STORED";
    case ST_SENT: return "SENT_TO_MESH";
    case ST_DELIVERED: return "DELIVERED";
    case ST_FAILED: return "FAILED";
  }
  return "?";
}

static void show_msgs() {
  int shown = 0;
  for (int i = 0; i < MSG_RECS; i++) {
    const MsgIdx& m = msg_idx[i];
    if (!m.valid) continue;
    if (++shown > 40) continue;
    char ks[11];
    key_str(m.key, ks);
    say("%s %-12s %s prev 0x%04X next 0x%04X%s", ks, st_name(m.state), m.own ? "own" : "   ", (unsigned)m.prev_hop,
        (unsigned)m.next_hop, m.dstate == DS_OWED ? " delivery owed" : "");
  }
  say("%d stored (first 40 shown), %d jobs, %d stored not handed off", shown, txq_count(), store_count(ST_STORED));
}

static void show_stats() {
  say("rx %lu tx %lu crc_hw %lu crc8 %lu bad %lu overheard %lu dup %lu", (unsigned long)stats.rx,
      (unsigned long)stats.tx, (unsigned long)stats.rx_crc_hw, (unsigned long)stats.rx_crc8,
      (unsigned long)stats.rx_bad, (unsigned long)stats.overheard, (unsigned long)stats.dup);
  say("cad_busy %lu range_dropped %lu store_full %lu forwarded %lu delivered %lu", (unsigned long)stats.cad_busy,
      (unsigned long)stats.range_dropped, (unsigned long)stats.store_full, (unsigned long)stats.forwarded,
      (unsigned long)stats.delivered);
}

static void show_sync() {
  if (!ts_synced()) {
    say("unsynced: listening on ch 0 for a HELLO");
    return;
  }
  int64_t now = esp_timer_get_time();
  int64_t pos = now - ts_sf_start(now);
  say("synced, depth %u, parent 0x%04X, %ld ms into the superframe (slot %ld)", (unsigned)ts_depth(),
      (unsigned)ts_parent(), (long)(pos / 1000), (long)(pos / SLOT_US));
}

static bool parse_id(const char* s, uint16_t& out) {
  if (!s) return false;
  char* end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end || v == 0 || v >= 0xFFFF) return false;
  out = (uint16_t)v;
  return true;
}

static bool parse_deg(const char* s, double lim, int32_t& out) {
  if (!s) return false;
  char* end;
  double v = strtod(s, &end);
  if (*end || v < -lim || v > lim) return false;
  out = (int32_t)lround(v * 1e7);
  return true;
}

static void cmd_set(char* key, char* a, char* b, char* c) {
  if (!key || !a) return say("set what? (help)");
  char* end;
  if (!strcmp(key, "id")) {
    uint16_t id;
    if (!parse_id(a, id)) return say("bad id");
    cfg_next.node_id = id;
    const char* want = IS_HELPER ? "0x0001-0x000F" : IS_DONGLE ? "0x2000-0xFFFE" : "0x0010-0x1FFF";
    bool in_range = IS_HELPER ? id <= 0x000F : IS_DONGLE ? id >= 0x2000 : (id >= 0x0010 && id <= 0x1FFF);
    if (!in_range) say("warning: this role's IDs are %s", want);
    return say("id 0x%04X after save + reboot", (unsigned)id);
  }
#if IS_RELAY
  if (!strcmp(key, "role")) {
    if (!strcmp(a, "balloon")) cfg_next.relay_role = ROLE_BALLOON;
    else if (!strcmp(a, "ground")) cfg_next.relay_role = ROLE_GROUND;
    else return say("role is balloon or ground");
    return say("role %s after save + reboot", a);
  }
#endif
  if (!strcmp(key, "helper")) {
    uint16_t id;
    int32_t lat, lon;
    if (!parse_id(a, id) || id > 0x000F || !parse_deg(b, 90, lat) || !parse_deg(c, 180, lon))
      return say("set helper <0x0001-0x000F> <lat> <lon>");
    cfg.helper_id = cfg_next.helper_id = id;
    cfg.helper_lat_e7 = cfg_next.helper_lat_e7 = lat;
    cfg.helper_lon_e7 = cfg_next.helper_lon_e7 = lon;
    nbr_recompute();
    return say("ok");
  }
  if (!strcmp(key, "pos")) {
    int32_t lat, lon;
    if (!parse_deg(a, 90, lat) || !parse_deg(b, 180, lon)) return say("set pos <lat> <lon> [alt]");
    long alt = c ? strtol(c, &end, 10) : cfg_next.alt_m;
    cfg.lat_e7 = cfg_next.lat_e7 = lat;
    cfg.lon_e7 = cfg_next.lon_e7 = lon;
    cfg.alt_m = cfg_next.alt_m = (int16_t)alt;
    cfg.pos_set = cfg_next.pos_set = 1;
    nbr_recompute();
    return say("ok");
  }
  if (!strcmp(key, "txp")) {
    long v = strtol(a, &end, 10);
    if (*end || v < 2 || v > 20) return say("txp 2-20 dBm");
    cfg.txp_dbm = cfg_next.txp_dbm = (int8_t)v;
    radio_set_txp(v);
    return say("ok");
  }
  if (!strcmp(key, "range")) {
    double v = strtod(a, &end);
    if (*end || v < 0) return say("range in km, 0 = off");
    cfg.range_km = cfg_next.range_km = (float)v;
    return say("ok");
  }
  if (!strcmp(key, "txlat")) {
    long v = strtol(a, &end, 10);
    if (*end) return say("txlat in microseconds");
    cfg.tx_latency_us = cfg_next.tx_latency_us = v;
    return say("ok");
  }
  if (!strcmp(key, "sf_slots") || !strcmp(key, "ctrl_slots")) {
    long v = strtol(a, &end, 10);
    if (*end || v < 2 || v > 250) return say("2-250");
    if (key[0] == 's') cfg_next.sf_slots = (uint8_t)v;
    else cfg_next.ctrl_slots = (uint8_t)v;
    return say("%s %ld after save + reboot (same on every node)", key, v);
  }
  say("unknown setting %s (help)", key);
}

static void cmd_send(char* a, char* b) {
#if IS_HELPER
  (void)a;
  (void)b;
  say("the helper doesn't originate messages");
#else
  char* end;
  long people = a ? strtol(a, &end, 10) : 0;
  if (!a || *end || people < 1 || people > 100) return say("send <people 1-100> <flags_hex>");
  unsigned long flags = b ? strtoul(b, &end, 16) : 0;
  if (b && (*end || flags > 0x1F)) return say("flags 0x00-0x1F");
  MyPos me = mesh_my_pos();
  uint8_t acc = me.fix ? (uint8_t)(me.acc_dm / 10 > 254 ? 254 : me.acc_dm / 10) : ACC8_NONE;
  mesh_create_message((uint8_t)people, (uint8_t)flags, me.lat, me.lon, me.alt, acc, mesh_unix_time());
#endif
}

static void command(char* line) {
  char* save;
  char* t[5] = {};
  int n = 0;
  for (char* p = strtok_r(line, " \t", &save); p && n < 5; p = strtok_r(nullptr, " \t", &save)) t[n++] = p;
  if (!n) return;
  if (!strcmp(t[0], "help")) return help();
  if (!strcmp(t[0], "show")) {
    if (t[1] && !strcmp(t[1], "cfg")) return show_cfg();
    if (t[1] && !strcmp(t[1], "nbr")) return show_nbr();
    if (t[1] && !strcmp(t[1], "msgs")) return show_msgs();
    if (t[1] && !strcmp(t[1], "stats")) return show_stats();
    if (t[1] && !strcmp(t[1], "sync")) return show_sync();
    return say("show cfg | nbr | msgs | stats | sync");
  }
  if (!strcmp(t[0], "set")) return cmd_set(t[1], t[2], t[3], t[4]);
  if (!strcmp(t[0], "save")) {
    const char* why;
    if (!config_valid(cfg_next, &why)) return say("not saved: %s", why);
    return say("%s", config_save() ? "saved" : "save failed");
  }
  if (!strcmp(t[0], "reboot")) {
    Serial.flush();
    ESP.restart();
  }
  if (!strcmp(t[0], "erase") && t[1] && !strcmp(t[1], "store")) {
    say("erasing the message store...");
    store_erase_all();
    for (auto& j : jobs) j.used = false;
    return say("done");
  }
  if (!strcmp(t[0], "send")) return cmd_send(t[1], t[2]);
  say("unknown command (help)");
}

void cli_poll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (s_len < CLI_LINE_MAX - 1) s_line[s_len++] = c;
      continue;
    }
    s_line[s_len] = 0;
    s_len = 0;
    if (!s_line[0]) continue;
    if (phone_line(s_line)) continue; // dongle: SEND|... and INFO?
    command(s_line);
  }
}
