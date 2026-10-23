// On-air packet formats, rev 5 (design/packet-design.md).
// Explicit little-endian serialize/deserialize; never cast structs onto buffers.
#pragma once
#include <Arduino.h>

#define PROTO_VER 1

enum : uint8_t {
  PT_HELLO = 0x1,
  PT_DATA = 0x2,
  PT_ACK = 0x3,
  PT_DELIVERY = 0x4,
  PT_INI = 0x5,
  PT_DELIVERY_ACK = 0x6,
};

#define HELLO_LEN 20
#define INI_LEN 25
#define CTL_LEN 10                 // ACK, DELIVERY, DELIVERY_ACK
#define DATA_LEN(n) (29 + 2 * (n))
#define PATH_MAX_N 11
#define PKT_MAX DATA_LEN(PATH_MAX_N) // 51
#define TTL_START 10

#define ID_NONE 0x0000
#define ID_BROADCAST 0xFFFF
#define ACC16_NONE 0xFFFF          // HELLO / INI position_acc: no fix
#define ACC8_NONE 0xFF             // DATA position_acc: no fix
#define DEPTH_ROOT 0
#define DEPTH_UNSYNCED 15

// pkt_validate() failures
enum : int { PV_SHORT = -1, PV_CRC = -2, PV_VERSION = -3, PV_LENGTH = -4 };

// HELLO and INI share everything but helper_id + gps_time (INI) and seq (HELLO).
struct Beacon {
  bool ini;
  uint16_t node_id;
  uint8_t role_flags;
  uint16_t helper_id;   // INI only
  int32_t lat, lon;     // degrees * 1e7
  int16_t alt;          // metres
  uint16_t pos_acc_dm;  // decimetres, ACC16_NONE = no fix
  uint32_t gps_time;    // INI only, Unix s, 0 = no time yet
  uint8_t seq;          // HELLO only
  uint16_t sf_offset_ms;
};

struct DataPkt {
  uint16_t source_id, message_id, destination_id, next_hop_id;
  uint8_t ttl, people, flags;
  int32_t lat, lon;     // victim position, degrees * 1e7
  int16_t alt;
  uint8_t pos_acc;      // metres, capped at 254, ACC8_NONE = no fix
  uint32_t timestamp;   // Unix s, creation time
  uint8_t path_count;
  uint16_t path[PATH_MAX_N];
};

// ACK:          from = ack_from,  to = ack_to (prev_hop)
// DELIVERY:     from = helper_id, to = to_id
// DELIVERY_ACK: from = ack_from,  to = ack_to (stored next_hop, or broadcast)
struct CtlPkt {
  uint8_t type;
  uint16_t from, to;
  uint16_t orig_source, message_id;
};

uint8_t crc8(const uint8_t* p, size_t n);

// role_flags: bits 0-2 role, bit 3 gps_fix, bits 4-7 sync_depth
inline uint8_t rf_make(uint8_t role, bool fix, uint8_t depth) {
  return (role & 0x07) | (fix ? 0x08 : 0) | (uint8_t)(depth << 4);
}
inline uint8_t rf_role(uint8_t rf) { return rf & 0x07; }
inline bool rf_fix(uint8_t rf) { return rf & 0x08; }
inline uint8_t rf_depth(uint8_t rf) { return rf >> 4; }

// Checks CRC8, version and length. Returns the packet type, or a PV_* error.
int pkt_validate(const uint8_t* buf, int len);

int beacon_encode(const Beacon& b, uint8_t* out);
bool beacon_decode(const uint8_t* buf, int len, Beacon& b);
int data_encode(const DataPkt& p, uint8_t* out);
bool data_decode(const uint8_t* buf, int len, DataPkt& p);
int ctl_encode(const CtlPkt& c, uint8_t* out);
bool ctl_decode(const uint8_t* buf, int len, CtlPkt& c);

// Dedup key (source_id, message_id), shown as "2001-00042".
inline uint32_t msg_key(uint16_t src, uint16_t mid) { return (uint32_t)src << 16 | mid; }
inline uint16_t key_src(uint32_t key) { return key >> 16; }
inline uint16_t key_mid(uint32_t key) { return key & 0xFFFF; }
void key_str(uint32_t key, char* out); // out: 11 bytes

// LoRa airtime at SF9 / BW125 / CR4/5 / preamble 8 / CRC on / explicit header.
uint32_t airtime_us(int len);

const char* type_name(uint8_t type);
