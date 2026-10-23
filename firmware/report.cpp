#include "report.h"
#include "role.h"

#if IS_HELPER
#include <math.h>
#include "timeutil.h"

static const char* role_from_id(uint16_t id) {
  if (id >= 0x0001 && id <= 0x000F) return "helper";
  if (id >= 0x0010 && id <= 0x0FFF) return "balloon";
  if (id >= 0x1000 && id <= 0x1FFF) return "ground";
  if (id >= 0x2000 && id <= 0xFFFE) return "dongle";
  return "unknown";
}

static const char* yes_no(uint8_t flags, int n) { return (flags >> n) & 1 ? "YES" : "NO"; }

void report_data(const DataPkt& p, const RxFrame& f) {
  char ks[11];
  key_str(msg_key(p.source_id, p.message_id), ks);
  char when[32];
  if (p.timestamp) unix_to_utc(p.timestamp, when, sizeof when);
  char path[PATH_MAX_N * 10];
  int o = 0;
  for (int k = 0; k < p.path_count; k++)
    o += snprintf(path + o, sizeof path - o, k ? " -> 0x%04X" : "0x%04X", (unsigned)p.path[k]);

  Serial.printf("\n============== DATA RECEIVED ==============\n");
  Serial.printf("type / version : DATA / v%u (0x%02X)\n", (unsigned)PROTO_VER, (unsigned)(PROTO_VER << 4 | PT_DATA));
  Serial.printf("message id     : %s\n", ks);
  Serial.printf("source         : 0x%04X (%s)\n", (unsigned)p.source_id, role_from_id(p.source_id));
  Serial.printf("destination    : 0x%04X (%s)\n", (unsigned)p.destination_id, role_from_id(p.destination_id));
  Serial.printf("next hop       : 0x%04X\n", (unsigned)p.next_hop_id);
  Serial.printf("ttl            : %u\n", (unsigned)p.ttl);
  Serial.printf("hops           : %u\n", (unsigned)(p.path_count - 1));
  Serial.printf("-------------------------------------------\n");
  Serial.printf("people         : %u\n", (unsigned)p.people);
  Serial.printf("trapped        : %s\n", yes_no(p.flags, 0));
  Serial.printf("injured        : %s\n", yes_no(p.flags, 1));
  Serial.printf("fire           : %s\n", yes_no(p.flags, 2));
  Serial.printf("flood          : %s\n", yes_no(p.flags, 3));
  Serial.printf("water/food     : %s\n", yes_no(p.flags, 4));
  Serial.printf("-------------------------------------------\n");
  Serial.printf("latitude       : %.6f %c\n", fabs(p.lat / 1e7), p.lat < 0 ? 'S' : 'N');
  Serial.printf("longitude      : %.6f %c\n", fabs(p.lon / 1e7), p.lon < 0 ? 'W' : 'E');
  Serial.printf("altitude       : %d m\n", (int)p.alt);
  if (p.pos_acc == ACC8_NONE) Serial.printf("position acc.  : no fix\n");
  else Serial.printf("position acc.  : %u m\n", (unsigned)p.pos_acc);
  if (p.timestamp) Serial.printf("timestamp      : %s (%lu)\n", when, (unsigned long)p.timestamp);
  else Serial.printf("timestamp      : not set\n");
  Serial.printf("-------------------------------------------\n");
  Serial.printf("path           : %s (%u nodes)\n", path, (unsigned)p.path_count);
  Serial.printf("last hop       : 0x%04X, RSSI %d dBm, SNR %.1f dB\n", (unsigned)p.path[p.path_count - 1], f.rssi, f.snr);
  Serial.printf("crc8           : OK\n");
  Serial.printf("===========================================\n");
}

#else

void report_data(const DataPkt&, const RxFrame&) {}

#endif
