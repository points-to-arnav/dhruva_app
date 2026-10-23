#include "packet.h"

static void put16(uint8_t* p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = v >> (8 * i);
}

static uint16_t get16(const uint8_t* p) { return p[0] | (uint16_t)p[1] << 8; }

static uint32_t get32(const uint8_t* p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Polynomial 0x07, init 0x00, over every byte before the crc8 byte.
uint8_t crc8(const uint8_t* p, size_t n) {
  uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

static int expected_len(uint8_t type, const uint8_t* buf, int len) {
  switch (type) {
    case PT_HELLO: return HELLO_LEN;
    case PT_INI: return INI_LEN;
    case PT_ACK:
    case PT_DELIVERY:
    case PT_DELIVERY_ACK: return CTL_LEN;
    case PT_DATA: {
      if (len < 28) return -1;
      uint8_t n = buf[27];
      if (n < 1 || n > PATH_MAX_N) return -1;
      return DATA_LEN(n);
    }
  }
  return -1;
}

int pkt_validate(const uint8_t* buf, int len) {
  if (len < 2) return PV_SHORT;
  if (crc8(buf, len - 1) != buf[len - 1]) return PV_CRC;
  if ((buf[0] >> 4) != PROTO_VER) return PV_VERSION;
  uint8_t type = buf[0] & 0x0F;
  if (expected_len(type, buf, len) != len) return PV_LENGTH;
  return type;
}

int beacon_encode(const Beacon& b, uint8_t* o) {
  int i = 0;
  o[i++] = PROTO_VER << 4 | (b.ini ? PT_INI : PT_HELLO);
  put16(o + i, b.node_id); i += 2;
  o[i++] = b.role_flags;
  if (b.ini) { put16(o + i, b.helper_id); i += 2; }
  put32(o + i, (uint32_t)b.lat); i += 4;
  put32(o + i, (uint32_t)b.lon); i += 4;
  put16(o + i, (uint16_t)b.alt); i += 2;
  put16(o + i, b.pos_acc_dm); i += 2;
  if (b.ini) { put32(o + i, b.gps_time); i += 4; }
  else o[i++] = b.seq;
  put16(o + i, b.sf_offset_ms); i += 2;
  o[i] = crc8(o, i);
  return i + 1;
}

bool beacon_decode(const uint8_t* buf, int len, Beacon& b) {
  uint8_t type = buf[0] & 0x0F;
  if ((type != PT_HELLO || len != HELLO_LEN) && (type != PT_INI || len != INI_LEN)) return false;
  memset(&b, 0, sizeof b);
  b.ini = type == PT_INI;
  int i = 1;
  b.node_id = get16(buf + i); i += 2;
  b.role_flags = buf[i++];
  if (b.ini) { b.helper_id = get16(buf + i); i += 2; }
  b.lat = (int32_t)get32(buf + i); i += 4;
  b.lon = (int32_t)get32(buf + i); i += 4;
  b.alt = (int16_t)get16(buf + i); i += 2;
  b.pos_acc_dm = get16(buf + i); i += 2;
  if (b.ini) { b.gps_time = get32(buf + i); i += 4; }
  else b.seq = buf[i++];
  b.sf_offset_ms = get16(buf + i);
  return true;
}

int data_encode(const DataPkt& p, uint8_t* o) {
  o[0] = PROTO_VER << 4 | PT_DATA;
  put16(o + 1, p.source_id);
  put16(o + 3, p.message_id);
  put16(o + 5, p.destination_id);
  put16(o + 7, p.next_hop_id);
  o[9] = p.ttl;
  o[10] = p.people;
  o[11] = p.flags;
  put32(o + 12, (uint32_t)p.lat);
  put32(o + 16, (uint32_t)p.lon);
  put16(o + 20, (uint16_t)p.alt);
  o[22] = p.pos_acc;
  put32(o + 23, p.timestamp);
  o[27] = p.path_count;
  for (int k = 0; k < p.path_count; k++) put16(o + 28 + 2 * k, p.path[k]);
  int n = 28 + 2 * p.path_count;
  o[n] = crc8(o, n);
  return n + 1;
}

bool data_decode(const uint8_t* buf, int len, DataPkt& p) {
  if ((buf[0] & 0x0F) != PT_DATA || len < DATA_LEN(1)) return false;
  memset(&p, 0, sizeof p);
  p.source_id = get16(buf + 1);
  p.message_id = get16(buf + 3);
  p.destination_id = get16(buf + 5);
  p.next_hop_id = get16(buf + 7);
  p.ttl = buf[9];
  p.people = buf[10];
  p.flags = buf[11];
  p.lat = (int32_t)get32(buf + 12);
  p.lon = (int32_t)get32(buf + 16);
  p.alt = (int16_t)get16(buf + 20);
  p.pos_acc = buf[22];
  p.timestamp = get32(buf + 23);
  p.path_count = buf[27];
  if (p.path_count < 1 || p.path_count > PATH_MAX_N || len != DATA_LEN(p.path_count)) return false;
  for (int k = 0; k < p.path_count; k++) p.path[k] = get16(buf + 28 + 2 * k);
  return true;
}

int ctl_encode(const CtlPkt& c, uint8_t* o) {
  o[0] = PROTO_VER << 4 | c.type;
  put16(o + 1, c.from);
  put16(o + 3, c.to);
  put16(o + 5, c.orig_source);
  put16(o + 7, c.message_id);
  o[9] = crc8(o, 9);
  return CTL_LEN;
}

bool ctl_decode(const uint8_t* buf, int len, CtlPkt& c) {
  if (len != CTL_LEN) return false;
  c.type = buf[0] & 0x0F;
  if (c.type != PT_ACK && c.type != PT_DELIVERY && c.type != PT_DELIVERY_ACK) return false;
  c.from = get16(buf + 1);
  c.to = get16(buf + 3);
  c.orig_source = get16(buf + 5);
  c.message_id = get16(buf + 7);
  return true;
}

void key_str(uint32_t key, char* out) {
  snprintf(out, 11, "%04X-%05u", (unsigned)key_src(key), (unsigned)key_mid(key));
}

// packet design §8: T_sym = 2^9 / 125 kHz = 4096 us, preamble 8 + 4.25 symbols,
// payload 8 + ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * 5 symbols.
uint32_t airtime_us(int len) {
  const int sf = 9;
  const uint32_t tsym = 4096;
  int num = 8 * len - 4 * sf + 28 + 16;
  int den = 4 * sf;
  int n = num > 0 ? (num + den - 1) / den : 0;
  uint32_t symbols = 8 + n * 5;
  return (4 * 8 + 17) * tsym / 4 + symbols * tsym; // (8 + 4.25) * tsym = 50176
}

const char* type_name(uint8_t type) {
  switch (type) {
    case PT_HELLO: return "HELLO";
    case PT_DATA: return "DATA";
    case PT_ACK: return "ACK";
    case PT_DELIVERY: return "DELIVERY";
    case PT_INI: return "INI";
    case PT_DELIVERY_ACK: return "DELIVERY_ACK";
  }
  return "?";
}
