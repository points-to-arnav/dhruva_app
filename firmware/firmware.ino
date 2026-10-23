// Dhruva Jaal LoRa mesh: one firmware for every module (dongle, relay, helper).
//
// 1. Pick the module in section 1 and give it its own ID and position in section 2.
// 2. Arduino IDE: board "ESP32C3 Dev Module", USB CDC On Boot: Enabled,
//    libraries "LoRa" by Sandeep Mistry and "ArduinoJson" by Benoit Blanchon (v7).
// 3. Flash. Open the serial monitor at 115200 and type "help".
//
// How it works: design/plan.md, design/packet-design.md, design/algorithm-design.md.

#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <stdarg.h>

// ============ 1. Which module is this? Set exactly one to 1. ============
#define IS_DONGLE 0  // plugged into the phone: sends the SOS, never relays
#define IS_RELAY  1  // balloon or ground node: stores and forwards, NEO-6M GPS
#define IS_HELPER 0  // rescue base: receives, prints to the PC, confirms delivery

// ============ 2. This module's ID and position ============
// IDs: helper 0x0001-0x000F, balloon 0x0010-0x0FFF, ground 0x1000-0x1FFF,
// dongle 0x2000-0xFFFE. Every module needs its own ID.
#if IS_HELPER
#define MY_ID 0x0001
const double MY_LAT = 27.752000, MY_LON = 85.320000;  // the helper's fixed position
#elif IS_RELAY
#define MY_ID 0x0011
#define RELAY_IS_BALLOON 1                             // 0 = ground node
const double MY_LAT = 27.716000, MY_LON = 85.320000;  // used until the GPS has a fix
#else
#define MY_ID 0x2001
const double MY_LAT = 27.680000, MY_LON = 85.320000;  // only for DEMO_RANGE_KM: the SOS carries the phone's position
#endif
const int MY_ALT = 1300;  // metres

// ============ 3. The network: the same on every module ============
#define HELPER_ID 0x0001
const double HELPER_LAT = 27.752000, HELPER_LON = 85.320000;
#define DEMO_RANGE_KM 0            // demo only: ignore HELLOs from farther than this, 0 = off
#define TX_POWER 5                 // dBm: 2-5 on a table, up to 20 in the field
#define HELLO_INTERVAL_MS 10000UL  // 10 s for the demo, 60 s in deployment

#if IS_DONGLE + IS_RELAY + IS_HELPER != 1
#error "Set exactly one of IS_DONGLE, IS_RELAY, IS_HELPER to 1"
#endif
#if !ARDUINO_USB_CDC_ON_BOOT
#error "Tools > USB CDC On Boot must be Enabled (Serial is the C3's USB port)"
#endif

// ============ Wiring (ESP32-C3 Super Mini) ============
#define PIN_DIO0 2
#define PIN_RST 3
#define PIN_MOSI 4
#define PIN_MISO 5
#define PIN_SCK 6
#define PIN_NSS 7
#define PIN_GPS_RX 20  // NEO-6M TX, relays only

// ============ Radio (design/packet-design.md section 7) ============
#define FREQUENCY 433175000L  // inside India's 433.05-434.79 MHz band
#define SPREADING_FACTOR 9
#define BANDWIDTH 125E3
#define CODING_RATE 5         // 4/5
#define SYNC_WORD 0x12

// ============ Packets (design/packet-design.md) ============
// DATA travels as one JSON object (it starts with '{'); HELLO, INI, ACK,
// DELIVERY and DELIVERY_ACK are short binary packets (first byte 0x11-0x16).
#define PROTO_VER 1
#define T_HELLO 0x1
#define T_ACK 0x3
#define T_DELIVERY 0x4
#define T_INI 0x5
#define T_DELIVERY_ACK 0x6

#define HELLO_LEN 18
#define INI_LEN 23
#define CTL_LEN 10            // ACK, DELIVERY, DELIVERY_ACK
#define PATH_MAX_N 11
#define LORA_MAX 255          // largest LoRa payload; a JSON DATA with a full path is ~250 B
#define TTL_START 10
#define BROADCAST 0xFFFF
#define NO_FIX_8 0xFF         // DATA position_acc
#define NO_FIX_16 0xFFFF      // HELLO / INI position_acc

#define ROLE_GROUND 0
#define ROLE_HELPER 1
#define ROLE_DONGLE 2
#define ROLE_BALLOON 3

// ============ Timing (design/algorithm-design.md) ============
#define SLOT_MS 1300UL            // fits the longest packet (JSON DATA, ~1.23 s)
#define ACK_TIMEOUT_MS SLOT_MS    // counted from the end of our TX
#define MAX_RETRIES 5             // per neighbour, then the next neighbour
#define WAITING_RETRY_MS 60000UL  // retry waiting messages and forget failed neighbours
#define DELIVERY_ROUNDS 5         // DELIVERY: rounds of 5 retries, 60 s apart, then give up
#define HELLO_JITTER_MS (HELLO_INTERVAL_MS / 6)
#define STALE_AFTER_MS (3 * HELLO_INTERVAL_MS + HELLO_INTERVAL_MS / 2)  // 3 HELLOs missed
#define FORGET_AFTER_MS (10 * HELLO_INTERVAL_MS)
#define GPS_FRESH_MS 10000UL

// ============ Sizes ============
#if IS_DONGLE
#define MAX_MSGS 16
#else
#define MAX_MSGS 50
#endif
#define SEEN_MAX 200
#define MAX_NBRS 16
#define MAX_FAILED 8

// ============ Types ============
struct DataPkt {
  uint16_t src, mid, dst, nextHop;
  uint8_t ttl, people, flags;
  int32_t lat, lon;  // degrees * 1e7, the victim's position
  int16_t alt;
  uint8_t acc;       // metres, NO_FIX_8 = no fix
  uint32_t time;     // Unix seconds, creation time
  uint8_t pathN;
  uint16_t path[PATH_MAX_N];
};

// ACK: from = ack_from, to = ack_to. DELIVERY: from = helper_id, to = to_id.
struct Ctl {
  uint8_t type;
  uint16_t from, to, src, mid;
};

struct Beacon {  // HELLO or INI
  bool ini;
  uint16_t id;
  uint8_t roleFlags;
  uint16_t helperId;  // INI only
  int32_t lat, lon;
  int16_t alt;
  uint16_t accDm;
  uint32_t gpsTime;   // INI only
  uint8_t seq;        // HELLO only
};

enum : uint8_t { NB_HEARD, NB_ACTIVE, NB_STALE };

struct Neighbor {
  bool used;
  uint16_t id;
  uint8_t role;
  int32_t lat, lon;
  int rssi;
  float snr;              // average (EWMA)
  unsigned long lastSeen;
  uint8_t hellos;
  uint16_t ackBits;       // last 10 sends, bit 0 = newest, 1 = ACKed
  uint8_t ackN;
  uint8_t state;
};

// Message states, also sent to the phone (design/algorithm-design.md section 8).
enum : uint8_t { MS_QUEUED, MS_STORED, MS_TRANSMITTING, MS_SENT_TO_MESH, MS_DELIVERED, MS_WAITING, MS_FAILED };
const char* STATE_NAMES[] = {"QUEUED", "STORED", "TRANSMITTING", "SENT_TO_MESH", "DELIVERED", "WAITING", "FAILED"};

// DELIVERY progress of a stored message.
enum : uint8_t { D_NONE, D_OWED, D_DONE, D_GAVE_UP };

// The part of a message that is kept in flash.
struct Saved {
  uint8_t state;     // MS_STORED, MS_SENT_TO_MESH, MS_DELIVERED or MS_FAILED
  uint8_t delivery;  // D_*
  bool own;
  uint16_t prevHop;  // last path entry when received, 0 for own messages
  uint16_t nextHop;  // who ACKed it, 0 until handed off
  uint16_t helperId; // from the DELIVERY
  uint32_t order;    // age, for eviction
  DataPkt pkt;       // the DATA as received
};

struct Msg {
  bool used;
  uint32_t key;      // source_id << 16 | message_id
  Saved s;
  // retry state, RAM only
  uint16_t target;
  uint8_t retries, rounds, nFailed;
  uint16_t failed[MAX_FAILED];
  unsigned long nextTryMs;
  bool waiting;
  uint8_t reported;  // last state sent to the phone
};

// ============ State ============
Preferences prefs;
Msg msgs[MAX_MSGS];
Neighbor nbrs[MAX_NBRS];
struct {
  uint16_t head;
  uint32_t keys[SEEN_MAX];
} seen;  // keys of messages already accepted, kept after they leave msgs[]

uint32_t nextOrder = 1;
int32_t myLat, myLon, helperLat, helperLon;
int16_t myAlt;
uint16_t myAccDm = NO_FIX_16;
bool gpsEver = false;
unsigned long lastFixMs = 0;
uint8_t helloSeq = 0;
bool iniPending = !IS_DONGLE;  // INI once after boot, and again on the first GPS fix
unsigned long nextBeaconMs = 0;
unsigned long lastSecondMs = 0, lastWakeMs = 0;
int nodesReported = -1;
char usbLine[160];
int usbLen = 0;

// ============ Small helpers ============
int32_t toE7(double deg) { return (int32_t)lround(deg * 1e7); }

void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }
uint16_t get16(const uint8_t* p) { return p[0] | (uint16_t)p[1] << 8; }
uint32_t get32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// CRC-8, polynomial 0x07, init 0x00.
uint8_t crc8(const uint8_t* p, int n) {
  uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

uint32_t makeKey(uint16_t src, uint16_t mid) { return (uint32_t)src << 16 | mid; }

// "2001-00042"
void keyText(uint32_t key, char* out) { snprintf(out, 11, "%04X-%05u", (unsigned)(key >> 16), (unsigned)(key & 0xFFFF)); }

// Horizontal distance in metres between two points in degrees * 1e7.
double distanceM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  const double k = 1e-7 * PI / 180.0;
  double dLat = ((double)lat2 - lat1) * k, dLon = ((double)lon2 - lon1) * k;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * k) * cos(lat2 * k) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * 6371000.0 * asin(sqrt(a > 1 ? 1 : a));
}

// "2025-10-23 11:00:05 UTC"
void utcText(uint32_t t, char* out, int n) {
  long z = t / 86400 + 719468, era = z / 146097;
  unsigned long doe = z - era * 146097, sod = t % 86400;
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
  long y = yoe + era * 400 + (m <= 2);
  snprintf(out, n, "%04ld-%02u-%02u %02lu:%02lu:%02lu UTC", y, m, d, sod / 3600, sod / 60 % 60, sod % 60);
}

// One JSON event per line, for the dashboard and the simulation: @{"t":..,"node":..,"ev":..,...}
void logEvent(const char* ev, const char* fmt, ...) {
  char body[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof body, fmt, ap);
  va_end(ap);
  Serial.printf("@{\"t\":%lu,\"node\":\"0x%04X\",\"ev\":\"%s\"%s%s}\n", millis(), (unsigned)MY_ID, ev,
                body[0] ? "," : "", body);
}

uint8_t myRole() {
#if IS_HELPER
  return ROLE_HELPER;
#elif IS_DONGLE
  return ROLE_DONGLE;
#elif RELAY_IS_BALLOON
  return ROLE_BALLOON;
#else
  return ROLE_GROUND;
#endif
}

bool gpsFresh() { return gpsEver && millis() - lastFixMs < GPS_FRESH_MS; }

// ============ Packet encode / decode ============
// Binary packets: returns the type if CRC, version and length are right, else -1.
int packetType(const uint8_t* b, int n) {
  if (n < 2 || crc8(b, n - 1) != b[n - 1] || (b[0] >> 4) != PROTO_VER) return -1;
  int t = b[0] & 0x0F, want = -1;
  if (t == T_HELLO) want = HELLO_LEN;
  else if (t == T_INI) want = INI_LEN;
  else if (t == T_ACK || t == T_DELIVERY || t == T_DELIVERY_ACK) want = CTL_LEN;
  return n == want ? t : -1;
}

// IDs travel as 4 hex digits ("2001") to keep a full-path DATA under 255 bytes.
String idHex(uint16_t id) {
  char b[5];
  snprintf(b, sizeof b, "%04X", id);
  return String(b);
}

bool parseId(JsonVariantConst v, uint16_t& out) {
  const char* s = v | "";
  char* end;
  unsigned long id = strtoul(s, &end, 16);
  if (!*s || *end || strlen(s) > 4) return false;
  out = id;
  return true;
}

// DATA as JSON, e.g.
// {"t":"DATA","src":"2001","mid":42,"dst":"0001","nh":"0011","ttl":10,"ppl":2,"flg":3,
//  "lat":27.680000,"lon":85.320000,"alt":1300,"acc":255,"ts":1761217205,"path":["2001"]}
// Returns its length.
int encodeData(const DataPkt& p, char* out, int size) {
  JsonDocument doc;
  doc["t"] = "DATA";
  doc["src"] = idHex(p.src);
  doc["mid"] = p.mid;
  doc["dst"] = idHex(p.dst);
  doc["nh"] = idHex(p.nextHop);
  doc["ttl"] = p.ttl;
  doc["ppl"] = p.people;
  doc["flg"] = p.flags;
  doc["lat"] = serialized(String(p.lat / 1e7, 6));
  doc["lon"] = serialized(String(p.lon / 1e7, 6));
  doc["alt"] = p.alt;
  doc["acc"] = p.acc;
  doc["ts"] = p.time;
  JsonArray path = doc["path"].to<JsonArray>();
  for (int i = 0; i < p.pathN; i++) path.add(idHex(p.path[i]));
  return serializeJson(doc, out, size);
}

// Parses a JSON DATA; false if it isn't one or a field is missing.
bool decodeData(const uint8_t* b, int n, DataPkt& p) {
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)b, n)) return false;
  if (strcmp(doc["t"] | "", "DATA") != 0) return false;
  JsonArrayConst path = doc["path"];
  if (path.isNull() || path.size() < 1 || path.size() > PATH_MAX_N) return false;
  const char* nums[] = {"mid", "ttl", "ppl", "flg", "lat", "lon", "alt", "acc", "ts"};
  for (const char* k : nums)
    if (!doc[k].is<double>()) return false;
  if (!parseId(doc["src"], p.src) || !parseId(doc["dst"], p.dst) || !parseId(doc["nh"], p.nextHop)) return false;
  p.mid = doc["mid"];
  p.ttl = doc["ttl"];
  p.people = doc["ppl"];
  p.flags = doc["flg"];
  p.lat = toE7(doc["lat"].as<double>());
  p.lon = toE7(doc["lon"].as<double>());
  p.alt = doc["alt"];
  p.acc = doc["acc"];
  p.time = doc["ts"];
  p.pathN = path.size();
  for (int i = 0; i < p.pathN; i++)
    if (!parseId(path[i], p.path[i])) return false;
  return true;
}

void encodeCtl(const Ctl& c, uint8_t* o) {
  o[0] = PROTO_VER << 4 | c.type;
  put16(o + 1, c.from);
  put16(o + 3, c.to);
  put16(o + 5, c.src);
  put16(o + 7, c.mid);
  o[9] = crc8(o, 9);
}

Ctl decodeCtl(const uint8_t* b) {
  Ctl c = {(uint8_t)(b[0] & 0x0F), get16(b + 1), get16(b + 3), get16(b + 5), get16(b + 7)};
  return c;
}

int encodeBeacon(const Beacon& bc, uint8_t* o) {
  int i = 0;
  o[i++] = PROTO_VER << 4 | (bc.ini ? T_INI : T_HELLO);
  put16(o + i, bc.id); i += 2;
  o[i++] = bc.roleFlags;
  if (bc.ini) { put16(o + i, bc.helperId); i += 2; }
  put32(o + i, bc.lat); i += 4;
  put32(o + i, bc.lon); i += 4;
  put16(o + i, bc.alt); i += 2;
  put16(o + i, bc.accDm); i += 2;
  if (bc.ini) { put32(o + i, bc.gpsTime); i += 4; }
  else o[i++] = bc.seq;
  o[i] = crc8(o, i);
  return i + 1;
}

void decodeBeacon(const uint8_t* b, Beacon& bc) {
  int i = 1;
  bc.ini = (b[0] & 0x0F) == T_INI;
  bc.id = get16(b + i); i += 2;
  bc.roleFlags = b[i++];
  bc.helperId = 0;
  if (bc.ini) { bc.helperId = get16(b + i); i += 2; }
  bc.lat = get32(b + i); i += 4;
  bc.lon = get32(b + i); i += 4;
  bc.alt = get16(b + i); i += 2;
  bc.accDm = get16(b + i); i += 2;
  bc.gpsTime = 0;
  bc.seq = 0;
  if (bc.ini) bc.gpsTime = get32(b + i);
  else bc.seq = b[i];
}

// ============ Radio ============
void radioSetup() {
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  LoRa.setPins(PIN_NSS, PIN_RST, PIN_DIO0);
  if (!LoRa.begin(FREQUENCY)) {
    Serial.println("# LoRa initialization failed! Check the wiring.");
    while (true) delay(1000);
  }
  LoRa.setTxPower(TX_POWER);
  LoRa.setSpreadingFactor(SPREADING_FACTOR);
  LoRa.setSignalBandwidth(BANDWIDTH);
  LoRa.setCodingRate4(CODING_RATE);
  LoRa.setPreambleLength(8);
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.enableCrc();
}

void radioSend(const uint8_t* buf, int len) {
  LoRa.beginPacket();
  LoRa.write(buf, len);
  LoRa.endPacket();  // waits until the packet is out
}

// Polls for a packet; returns its length, 0 if none. buf holds LORA_MAX + 1 bytes.
int radioReceive(uint8_t* buf, int& rssi, float& snr) {
  int n = LoRa.parsePacket();
  if (n <= 0) return 0;
  int len = 0;
  while (LoRa.available() && len < LORA_MAX) buf[len++] = LoRa.read();
  rssi = LoRa.packetRssi();
  snr = LoRa.packetSnr();
  return len;
}

// SX1278 registers for CAD, which the LoRa library has no simple call for.
uint8_t readReg(uint8_t addr) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_NSS, LOW);
  SPI.transfer(addr & 0x7F);
  uint8_t v = SPI.transfer(0);
  digitalWrite(PIN_NSS, HIGH);
  SPI.endTransaction();
  return v;
}

void writeReg(uint8_t addr, uint8_t v) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_NSS, LOW);
  SPI.transfer(addr | 0x80);
  SPI.transfer(v);
  digitalWrite(PIN_NSS, HIGH);
  SPI.endTransaction();
}

// Listen before talk: Channel Activity Detection (~8 ms at SF9).
// True if someone is transmitting.
bool channelBusy() {
  LoRa.idle();
  writeReg(0x12, 0xFF);         // clear IRQ flags
  writeReg(0x01, 0x80 | 0x07);  // LoRa mode, CAD
  unsigned long t0 = millis();
  uint8_t flags = 0;
  while (!(flags & 0x04) && millis() - t0 < 50) flags = readReg(0x12);  // CadDone
  writeReg(0x12, 0xFF);
  LoRa.idle();  // the next parsePacket() goes back to receiving
  return !(flags & 0x04) || (flags & 0x01);  // CadDetected, or no answer
}

// ============ Neighbours (design/algorithm-design.md section 3) ============
Neighbor* findNbr(uint16_t id) {
  for (auto& n : nbrs)
    if (n.used && n.id == id) return &n;
  return nullptr;
}

float ackRate(const Neighbor& n) {
  if (n.ackN == 0) return 1.0;  // untried links get tried
  return (float)__builtin_popcount(n.ackBits & ((1 << n.ackN) - 1)) / n.ackN;
}

// My distance to the helper minus theirs.
double progressOf(const Neighbor& n) {
  double mine = MY_ID == HELPER_ID ? 0 : distanceM(myLat, myLon, helperLat, helperLon);
  double theirs = (n.id == HELPER_ID || n.role == ROLE_HELPER) ? 0 : distanceM(n.lat, n.lon, helperLat, helperLon);
  return mine - theirs;
}

// Routing order: progress > 0 first, then ACK success rate, then SNR.
bool better(const Neighbor& a, const Neighbor& b) {
  double pa = progressOf(a), pb = progressOf(b);
  if ((pa > 0) != (pb > 0)) return pa > 0;
  if (ackRate(a) != ackRate(b)) return ackRate(a) > ackRate(b);
  return a.snr > b.snr;
}

bool heardRecently(const Neighbor& n) { return n.used && millis() - n.lastSeen <= STALE_AFTER_MS; }

int nodesInRange() {
  int c = 0;
  for (auto& n : nbrs)
    if (heardRecently(n)) c++;
  return c;
}

const char* nbrStateName(uint8_t s) { return s == NB_ACTIVE ? "ACTIVE" : s == NB_STALE ? "STALE" : "HEARD"; }

void logNbr(const Neighbor& n, const char* why) {
  logEvent("nbr", "\"id\":\"0x%04X\",\"state\":\"%s\",\"why\":\"%s\",\"rssi\":%d,\"snr\":%.1f", n.id,
           nbrStateName(n.state), why, n.rssi, n.snr);
}

// Waiting messages try again (a neighbour appeared). clearFailed: also retry
// neighbours that failed before.
void wakeWaiting(bool clearFailed) {
  for (auto& m : msgs) {
    if (!m.used || !m.waiting) continue;
    m.waiting = false;
    if (clearFailed) {
      m.nFailed = 0;
      m.target = 0;
      m.retries = 0;
    }
  }
}

void onBeacon(const Beacon& b, int rssi, float snr) {
  if (b.id == MY_ID) return;
  if (DEMO_RANGE_KM > 0 && distanceM(myLat, myLon, b.lat, b.lon) > DEMO_RANGE_KM * 1000.0) return;

  Neighbor* n = findNbr(b.id);
  bool isNew = !n;
  if (isNew) {
    for (auto& x : nbrs)  // a free entry, else the oldest one that isn't ACTIVE
      if (!x.used || (x.state != NB_ACTIVE && (!n || x.lastSeen < n->lastSeen))) {
        n = &x;
        if (!x.used) break;
      }
    if (!n) return;
    memset(n, 0, sizeof *n);
    n->used = true;
    n->id = b.id;
    n->state = NB_HEARD;
    n->snr = snr;
  }
  bool moved = !isNew && (n->lat != b.lat || n->lon != b.lon);
  uint8_t old = n->state;
  if (n->state == NB_STALE) {  // back on probation: 3 fresh HELLOs, clean ACK history
    n->state = NB_HEARD;
    n->hellos = 0;
    n->ackBits = 0;
    n->ackN = 0;
  }
  n->role = b.roleFlags & 0x07;
  n->lat = b.lat;  // balloons drift: always take the latest position
  n->lon = b.lon;
  n->rssi = rssi;
  n->snr += 0.25 * (snr - n->snr);
  n->lastSeen = millis();
  if (n->hellos < 255) n->hellos++;
  if (n->state == NB_HEARD && n->hellos >= 3 && n->snr > -10) n->state = NB_ACTIVE;
  else if (n->state == NB_ACTIVE && n->snr <= -10) n->state = NB_HEARD;

  if (isNew) logNbr(*n, "heard");
  else if (n->state != old) logNbr(*n, "hellos/snr");
#if IS_DONGLE
  wakeWaiting(false);  // any node heard is a candidate for a dongle
#else
  if ((n->state == NB_ACTIVE && old != NB_ACTIVE) || (moved && n->state == NB_ACTIVE)) wakeWaiting(false);
#endif
  if (b.ini && b.helperId != HELPER_ID)
    logEvent("helper_mismatch", "\"from\":\"0x%04X\",\"their_helper\":\"0x%04X\"", b.id, b.helperId);
}

void ackResult(uint16_t id, bool ok) {
  Neighbor* n = findNbr(id);
  if (!n) return;
  n->ackBits = ((n->ackBits << 1) | (ok ? 1 : 0)) & 0x3FF;
  if (n->ackN < 10) n->ackN++;
  if (n->state == NB_ACTIVE && n->ackN >= 5 && ackRate(*n) < 0.5) {
    n->state = NB_STALE;
    logNbr(*n, "ack rate");
  }
}

// Every second: neighbours that went quiet.
void nbrTick() {
  for (auto& n : nbrs) {
    if (!n.used) continue;
    unsigned long age = millis() - n.lastSeen;
    if (age > FORGET_AFTER_MS) {
      n.used = false;
    } else if (n.state != NB_STALE && age > STALE_AFTER_MS) {
      n.state = NB_STALE;
      logNbr(n, "silent");
    }
  }
}

// ============ Message store, kept in flash (design/algorithm-design.md section 9) ============
void slotName(int i, char* out) { snprintf(out, 8, "m%d", i); }

void saveMsg(int i) {
  char k[8];
  slotName(i, k);
  prefs.putBytes(k, &msgs[i].s, sizeof(Saved));
}

void resetRetry(Msg& m) {
  m.target = 0;
  m.retries = m.rounds = m.nFailed = 0;
  m.nextTryMs = millis();
  m.waiting = false;
  m.reported = 0xFF;
}

void loadStore() {
  for (int i = 0; i < MAX_MSGS; i++) {
    char k[8];
    slotName(i, k);
    Msg& m = msgs[i];
    m.used = prefs.getBytesLength(k) == sizeof(Saved) && prefs.getBytes(k, &m.s, sizeof(Saved)) == sizeof(Saved);
    if (!m.used) continue;
    m.key = makeKey(m.s.pkt.src, m.s.pkt.mid);
    resetRetry(m);
    if (m.s.order >= nextOrder) nextOrder = m.s.order + 1;
  }
  if (prefs.getBytesLength("seen") == sizeof seen) prefs.getBytes("seen", &seen, sizeof seen);
}

int findMsg(uint32_t key) {
  for (int i = 0; i < MAX_MSGS; i++)
    if (msgs[i].used && msgs[i].key == key) return i;
  return -1;
}

bool seenHas(uint32_t key) {
  if (findMsg(key) >= 0) return true;
  for (int i = 0; i < SEEN_MAX; i++)
    if (seen.keys[i] == key) return true;
  return false;
}

void seenAdd(uint32_t key) {
  seen.keys[seen.head] = key;
  seen.head = (seen.head + 1) % SEEN_MAX;
  prefs.putBytes("seen", &seen, sizeof seen);
}

// A free slot; when full, the oldest delivered (or failed) message, then the
// oldest one already handed on. Never one that still has to be sent.
int freeSlot() {
  int best = -1;
  for (int pass = 0; pass < 3 && best < 0; pass++) {
    for (int i = 0; i < MAX_MSGS; i++) {
      Msg& m = msgs[i];
      if (pass == 0) {
        if (!m.used) return i;
        continue;
      }
      if (m.s.delivery == D_OWED) continue;
      bool ok = pass == 1 ? (m.s.state == MS_DELIVERED || m.s.state == MS_FAILED) : m.s.state == MS_SENT_TO_MESH;
      if (ok && (best < 0 || m.s.order < msgs[best].s.order)) best = i;
    }
  }
  return best;
}

// Stores a DATA in flash. Returns its slot, or -1 when the store is full.
int storeMsg(const DataPkt& p, uint16_t prevHop, bool own, uint8_t state, uint8_t delivery) {
  int i = freeSlot();
  if (i < 0) return -1;
  Msg& m = msgs[i];
  memset(&m, 0, sizeof m);
  m.used = true;
  m.key = makeKey(p.src, p.mid);
  m.s.state = state;
  m.s.delivery = delivery;
  m.s.own = own;
  m.s.prevHop = prevHop;
  m.s.order = nextOrder++;
  m.s.pkt = p;
  resetRetry(m);
  saveMsg(i);
  return i;
}

void eraseStore() {
  uint16_t mid = prefs.getUShort("mid", 0);  // message IDs must never repeat
  for (auto& m : msgs) m.used = false;
  memset(&seen, 0, sizeof seen);
  prefs.clear();
  prefs.putUShort("mid", mid);
  Serial.println("# message store erased");
}

// ============ Status to the phone (dongle; docs/usb-protocol.md) ============
void statusLine(uint32_t key, uint8_t state, uint16_t nextHop) {
  char k[11];
  keyText(key, k);
#if IS_DONGLE
  // {"type":"status","id":"2001-00042","state":"SENT_TO_MESH","next_hop":"0x0011"}
  if (state == MS_SENT_TO_MESH)
    Serial.printf("{\"type\":\"status\",\"id\":\"%s\",\"state\":\"%s\",\"next_hop\":\"0x%04X\"}\n", k,
                  STATE_NAMES[state], nextHop);
  else Serial.printf("{\"type\":\"status\",\"id\":\"%s\",\"state\":\"%s\"}\n", k, STATE_NAMES[state]);
#endif
  logEvent("msg_state", "\"key\":\"%s\",\"state\":\"%s\"", k, STATE_NAMES[state]);
}

// Reports a state change of one of our own messages, once per state.
void reportState(Msg& m, uint8_t state, uint16_t nextHop) {
  if (!m.s.own || m.reported == state) return;
  m.reported = state;
  statusLine(m.key, state, nextHop);
}

// {"type":"info","battery":null,"nodes":2}. Battery is null until a battery-sense pin is wired.
void sendInfo() {
  Serial.printf("{\"type\":\"info\",\"battery\":null,\"nodes\":%d}\n", nodesReported < 0 ? 0 : nodesReported);
}

// ============ Helper: the DATA block for the PC ============
void printData(const DataPkt& p, const uint8_t* json, int len, int rssi, float snr) {
  char k[11], when[32];
  keyText(makeKey(p.src, p.mid), k);
  if (p.time) utcText(p.time, when, sizeof when);
  Serial.println();
  Serial.println("============== DATA RECEIVED ==============");
  Serial.printf("type           : DATA (JSON, %d bytes)\n", len);
  Serial.printf("message id     : %s\n", k);
  Serial.printf("source         : 0x%04X (%s)\n", p.src, p.src >= 0x2000 ? "dongle" : p.src >= 0x1000 ? "ground" : p.src >= 0x0010 ? "balloon" : "helper");
  Serial.printf("destination    : 0x%04X (helper)\n", p.dst);
  Serial.printf("next hop       : 0x%04X\n", p.nextHop);
  Serial.printf("ttl            : %u\n", p.ttl);
  Serial.printf("hops           : %u\n", p.pathN - 1);
  Serial.println("-------------------------------------------");
  Serial.printf("people         : %u\n", p.people);
  Serial.printf("trapped        : %s\n", p.flags & 0x01 ? "YES" : "NO");
  Serial.printf("injured        : %s\n", p.flags & 0x02 ? "YES" : "NO");
  Serial.printf("fire           : %s\n", p.flags & 0x04 ? "YES" : "NO");
  Serial.printf("flood          : %s\n", p.flags & 0x08 ? "YES" : "NO");
  Serial.printf("water/food     : %s\n", p.flags & 0x10 ? "YES" : "NO");
  Serial.println("-------------------------------------------");
  Serial.printf("latitude       : %.6f %c\n", fabs(p.lat / 1e7), p.lat < 0 ? 'S' : 'N');
  Serial.printf("longitude      : %.6f %c\n", fabs(p.lon / 1e7), p.lon < 0 ? 'W' : 'E');
  Serial.printf("altitude       : %d m\n", p.alt);
  if (p.acc == NO_FIX_8) Serial.println("position acc.  : no fix");
  else Serial.printf("position acc.  : %u m\n", p.acc);
  if (p.time) Serial.printf("timestamp      : %s (%lu)\n", when, (unsigned long)p.time);
  else Serial.println("timestamp      : not set");
  Serial.println("-------------------------------------------");
  Serial.print("path           : ");
  for (int i = 0; i < p.pathN; i++) Serial.printf(i ? " -> 0x%04X" : "0x%04X", p.path[i]);
  Serial.printf(" (%u nodes)\n", p.pathN);
  Serial.printf("last hop       : 0x%04X, RSSI %d dBm, SNR %.1f dB\n", p.path[p.pathN - 1], rssi, snr);
  Serial.println("crc            : OK (LoRa CRC16)");
  Serial.println("-------------------------------------------");
  Serial.printf("json           : %.*s\n", len, (const char*)json);
  Serial.println("===========================================");
}

// ============ Receiving (design/algorithm-design.md section 5) ============
void sendCtl(uint8_t type, uint16_t from, uint16_t to, uint32_t key) {
  Ctl c = {type, from, to, (uint16_t)(key >> 16), (uint16_t)(key & 0xFFFF)};
  uint8_t pkt[CTL_LEN];
  encodeCtl(c, pkt);
  radioSend(pkt, CTL_LEN);  // at once, no CAD: the sender is waiting for it
}

// DATA addressed to us: store first, then ACK.
void onData(const DataPkt& p, const uint8_t* raw, int len, int rssi, float snr) {
  uint32_t key = makeKey(p.src, p.mid);
  uint16_t prev = p.path[p.pathN - 1];
  char k[11];
  keyText(key, k);
  if (seenHas(key)) {  // a repeat: our ACK was lost
    sendCtl(T_ACK, MY_ID, prev, key);
    logEvent("dup", "\"key\":\"%s\",\"from\":\"0x%04X\"", k, prev);
    return;
  }
#if IS_DONGLE
  return;  // dongles never relay
#else
  bool expired = p.ttl == 0 || p.pathN >= PATH_MAX_N;
  uint8_t state = IS_HELPER ? MS_DELIVERED : expired ? MS_FAILED : MS_STORED;
  int i = storeMsg(p, prev, false, state, IS_HELPER ? D_OWED : D_NONE);
  if (i < 0) {  // no ACK: the sender keeps it and tries another node
    logEvent("store_full", "\"key\":\"%s\"", k);
    return;
  }
  sendCtl(T_ACK, MY_ID, prev, key);
  seenAdd(key);
  logEvent("data_rx", "\"key\":\"%s\",\"from\":\"0x%04X\",\"rssi\":%d,\"snr\":%.1f,\"ttl\":%u,\"hops\":%u", k, prev,
           rssi, snr, p.ttl, p.pathN - 1);
#if IS_HELPER
  printData(p, raw, len, rssi, snr);
  char path[PATH_MAX_N * 9 + 10];
  int o = 0;
  for (int j = 0; j < p.pathN; j++) o += snprintf(path + o, sizeof path - o, "\"0x%04X\",", p.path[j]);
  snprintf(path + o, sizeof path - o, "\"0x%04X\"", MY_ID);
  logEvent("delivered", "\"key\":\"%s\",\"path\":[%s]", k, path);
#else
  if (expired) logEvent("ttl_expired", "\"key\":\"%s\"", k);
#endif
#endif
}

// DELIVERY addressed to us: confirm it, then pass it back along the path.
void onDelivery(const Ctl& c) {
  uint32_t key = makeKey(c.src, c.mid);
  int i = findMsg(key);
  // DELIVERY has no sender field: the node we handed the DATA to is sending it back.
  uint16_t ackTo = (i >= 0 && msgs[i].s.nextHop) ? msgs[i].s.nextHop : BROADCAST;
  sendCtl(T_DELIVERY_ACK, MY_ID, ackTo, key);
  char k[11];
  keyText(key, k);
  if (i < 0) {
    logEvent("delivery_no_record", "\"key\":\"%s\"", k);
    return;
  }
  Msg& m = msgs[i];
  if (m.s.state == MS_DELIVERED) return;  // a repeat: our DELIVERY_ACK was lost
  m.s.state = MS_DELIVERED;               // also stops sending it, if our hand-off ACK was lost
  logEvent("delivery_rx", "\"key\":\"%s\",\"helper\":\"0x%04X\"", k, c.from);
  if (c.src != MY_ID) {
    m.s.helperId = c.from;
    m.s.delivery = D_OWED;
    resetRetry(m);
  }
  saveMsg(i);
  reportState(m, MS_DELIVERED, 0);
}

void handlePacket(const uint8_t* b, int n, int rssi, float snr) {
  if (b[0] == '{') {  // JSON DATA
    DataPkt p;
    if (decodeData(b, n, p) && p.nextHop == MY_ID) onData(p, b, n, rssi, snr);  // only the named next hop takes it
    return;
  }
  int t = packetType(b, n);
  if (t == T_HELLO || t == T_INI) {
    Beacon bc;
    decodeBeacon(b, bc);
    onBeacon(bc, rssi, snr);
  } else if (t == T_DELIVERY) {
    Ctl c = decodeCtl(b);
    if (c.to == MY_ID) onDelivery(c);
  }
  // ACK and DELIVERY_ACK only matter while we wait for one, in exchange().
}

// ============ Sending (design/algorithm-design.md sections 4 and 6) ============
enum : uint8_t { EX_OK, EX_NO_ACK, EX_BUSY };

// Listen before talk, send, then wait one slot for the matching ACK.
uint8_t exchange(const uint8_t* pkt, int len, uint8_t ackType, uint16_t target, uint32_t key) {
  if (channelBusy()) return EX_BUSY;
  radioSend(pkt, len);
  unsigned long t0 = millis();
  while (millis() - t0 < ACK_TIMEOUT_MS) {
    uint8_t b[LORA_MAX + 1];
    int rssi;
    float snr;
    int n = radioReceive(b, rssi, snr);
    if (n == 0) continue;
    if (packetType(b, n) == ackType) {
      Ctl c = decodeCtl(b);
      // DELIVERY_ACK is matched on ack_from + key only (DELIVERY has no sender field).
      if (c.from == target && makeKey(c.src, c.mid) == key && (ackType == T_DELIVERY_ACK || c.to == MY_ID))
        return EX_OK;
    }
    handlePacket(b, n, rssi, snr);  // anything else is handled as usual
  }
  return EX_NO_ACK;
}

// Backoff before retry k: random(1 .. min(2^k, 16)) slots.
unsigned long backoffMs(int k) { return random(1, (k >= 4 ? 16 : 1 << k) + 1) * SLOT_MS; }

bool listed(uint16_t id, const uint16_t* ids, int n) {
  for (int i = 0; i < n; i++)
    if (ids[i] == id) return true;
  return false;
}

// The next hop for a message, or 0 when there is none (it waits).
uint16_t pickNextHop(const Msg& m) {
  const DataPkt& p = m.s.pkt;
  Neighbor* best = nullptr;
#if IS_DONGLE
  // Dongle: nodes heard in the last 3 HELLO intervals. Highest SNR; within
  // 1 dB of it, the most progress to the helper.
  float maxSnr = -1000;
  for (auto& n : nbrs)
    if (heardRecently(n) && !listed(n.id, m.failed, m.nFailed) && n.snr > maxSnr) maxSnr = n.snr;
  for (auto& n : nbrs)
    if (heardRecently(n) && !listed(n.id, m.failed, m.nFailed) && n.snr >= maxSnr - 1 &&
        (!best || progressOf(n) > progressOf(*best)))
      best = &n;
#else
  // Relay / helper: the best ACTIVE neighbour that isn't already on the path
  // (loop prevention) and hasn't failed for this message.
  for (auto& n : nbrs)
    if (n.used && n.state == NB_ACTIVE && !listed(n.id, p.path, p.pathN) && !listed(n.id, m.failed, m.nFailed) &&
        (!best || better(n, *best)))
      best = &n;
#endif
  return best ? best->id : 0;
}

void sendData(int i) {
  Msg& m = msgs[i];
  char k[11];
  keyText(m.key, k);
  if (!m.target) m.target = pickNextHop(m);
  if (!m.target) {
    if (!m.waiting) logEvent("waiting", "\"key\":\"%s\"", k);
    m.waiting = true;  // until a neighbour appears, or 60 s
    reportState(m, MS_WAITING, 0);
    return;
  }
  DataPkt p = m.s.pkt;
  if (!m.s.own) {  // forwarding: one hop used, add ourselves to the path
    p.ttl--;
    p.path[p.pathN++] = MY_ID;
  }
  p.nextHop = m.target;
  char json[LORA_MAX + 1];
  int len = encodeData(p, json, sizeof json);
  reportState(m, MS_TRANSMITTING, 0);

  uint8_t r = exchange((const uint8_t*)json, len, T_ACK, m.target, m.key);
  if (r == EX_BUSY) {  // not counted as a retry
    m.nextTryMs = millis() + random(1, 5) * SLOT_MS;
    return;
  }
  ackResult(m.target, r == EX_OK);
  if (r == EX_OK) {  // handed off
    logEvent("ack_rx", "\"key\":\"%s\",\"from\":\"0x%04X\",\"retries\":%u", k, m.target, m.retries);
    m.s.nextHop = m.target;
    m.s.state = MS_SENT_TO_MESH;
    saveMsg(i);
    reportState(m, MS_SENT_TO_MESH, m.target);
    return;
  }
  m.retries++;
  if (m.retries <= MAX_RETRIES) {
    logEvent("retry", "\"key\":\"%s\",\"to\":\"0x%04X\",\"n\":%u", k, m.target, m.retries);
    m.nextTryMs = millis() + backoffMs(m.retries);
  } else {  // this neighbour is out for this message: try the next one
    logEvent("next_candidate", "\"key\":\"%s\",\"failed\":\"0x%04X\"", k, m.target);
    if (m.nFailed < MAX_FAILED) m.failed[m.nFailed++] = m.target;
    m.target = 0;
    m.retries = 0;
  }
}

void sendDelivery(int i) {
  Msg& m = msgs[i];
  char k[11];
  keyText(m.key, k);
  uint16_t target = m.s.prevHop;  // back the way the message came
  if (!target) {
    m.s.delivery = D_GAVE_UP;
    saveMsg(i);
    return;
  }
  Ctl c = {T_DELIVERY, IS_HELPER ? (uint16_t)MY_ID : (m.s.helperId ? m.s.helperId : (uint16_t)HELPER_ID), target,
           (uint16_t)(m.key >> 16), (uint16_t)(m.key & 0xFFFF)};
  uint8_t pkt[CTL_LEN];
  encodeCtl(c, pkt);

  uint8_t r = exchange(pkt, CTL_LEN, T_DELIVERY_ACK, target, m.key);
  if (r == EX_BUSY) {
    m.nextTryMs = millis() + random(1, 5) * SLOT_MS;
    return;
  }
  ackResult(target, r == EX_OK);
  if (r == EX_OK) {
    logEvent("delivery_acked", "\"key\":\"%s\",\"by\":\"0x%04X\"", k, target);
    m.s.delivery = D_DONE;
    saveMsg(i);
    return;
  }
  m.retries++;
  if (m.retries <= MAX_RETRIES) {
    m.nextTryMs = millis() + backoffMs(m.retries);
    return;
  }
  m.retries = 0;
  m.rounds++;
  if (m.rounds >= DELIVERY_ROUNDS) {  // the dongle was switched off or moved away
    logEvent("delivery_gave_up", "\"key\":\"%s\",\"to\":\"0x%04X\"", k, target);
    m.s.delivery = D_GAVE_UP;
    saveMsg(i);
  } else {
    m.nextTryMs = millis() + WAITING_RETRY_MS;
  }
}

// One send per loop: DELIVERY first, then our own messages, then forwarding, oldest first.
void doWork() {
  int best = -1, bestRank = 9;
  for (int i = 0; i < MAX_MSGS; i++) {
    Msg& m = msgs[i];
    if (!m.used || m.waiting || (long)(millis() - m.nextTryMs) < 0) continue;
    int rank;
    if (m.s.delivery == D_OWED) rank = 0;
    else if (m.s.state == MS_STORED) rank = m.s.own ? 1 : 2;
    else continue;
    if (rank < bestRank || (rank == bestRank && m.s.order < msgs[best].s.order)) {
      best = i;
      bestRank = rank;
    }
  }
  if (best < 0) return;
  if (bestRank == 0) sendDelivery(best);
  else sendData(best);
}

// Creates our own SOS message (dongle: from the phone's SEND line).
void createMessage(uint8_t people, uint8_t flags, int32_t lat, int32_t lon, int16_t alt, uint8_t acc, uint32_t time) {
  uint16_t mid = prefs.getUShort("mid", 0) + 1;  // never reused, even after a reboot
  if (mid == 0) mid = 1;
  prefs.putUShort("mid", mid);
  uint32_t key = makeKey(MY_ID, mid);

  DataPkt p = {};
  p.src = MY_ID;
  p.mid = mid;
  p.dst = HELPER_ID;
  p.nextHop = BROADCAST;  // set on each attempt
  p.ttl = TTL_START;
  p.people = people;
  p.flags = flags & 0x1F;
  p.lat = lat;
  p.lon = lon;
  p.alt = alt;
  p.acc = acc;
  p.time = time;
  p.pathN = 1;
  p.path[0] = MY_ID;
  int i = storeMsg(p, 0, true, MS_STORED, D_NONE);
  if (i < 0) {
    statusLine(key, MS_QUEUED, 0);
    statusLine(key, MS_FAILED, 0);
    return;
  }
  seenAdd(key);
  reportState(msgs[i], MS_QUEUED, 0);
  reportState(msgs[i], MS_STORED, 0);
}

// ============ Beacons: HELLO and INI (relays and helper) ============
void sendBeacon(bool ini) {
  Beacon b = {};
  bool fix = gpsFresh();
  b.ini = ini;
  b.id = MY_ID;
  b.roleFlags = myRole() | (fix ? 0x08 : 0);
  b.helperId = HELPER_ID;
  b.lat = myLat;
  b.lon = myLon;
  b.alt = myAlt;
  b.accDm = fix ? myAccDm : NO_FIX_16;
  b.gpsTime = 0;
  b.seq = helloSeq;
  uint8_t pkt[INI_LEN];
  radioSend(pkt, encodeBeacon(b, pkt));
  if (ini) logEvent("ini_tx", "\"gps_fix\":%s", fix ? "true" : "false");
  else helloSeq++;
}

void beaconTick() {
#if !IS_DONGLE
  if ((long)(millis() - nextBeaconMs) < 0) return;
  if (channelBusy()) {
    nextBeaconMs = millis() + random(1, 5) * SLOT_MS;
    return;
  }
  sendBeacon(iniPending);
  iniPending = false;
  nextBeaconMs = millis() + HELLO_INTERVAL_MS + random(0, HELLO_JITTER_MS + 1);
#endif
}

// ============ GPS (relays): NEO-6M, $GPGGA / $GNGGA ============
#if IS_RELAY
char gpsLine[100];
int gpsLen = 0;

// ddmm.mmmm + N/S/E/W -> degrees * 1e7
int32_t nmeaCoord(const char* v, char hemi) {
  double raw = atof(v), deg = floor(raw / 100);
  double dd = deg + (raw - deg * 100) / 60;
  return toE7(hemi == 'S' || hemi == 'W' ? -dd : dd);
}

void onGpsLine(char* s) {
  // checksum: XOR of everything between '$' and '*'
  char* star = strchr(s, '*');
  if (s[0] != '$' || !star) return;
  uint8_t x = 0;
  for (char* c = s + 1; c < star; c++) x ^= *c;
  if (strtol(star + 1, nullptr, 16) != x) return;
  *star = 0;
  if (strncmp(s + 3, "GGA", 3) != 0) return;
  // $GPGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,...
  char* f[15];
  int n = 0;
  for (char* c = s; n < 15;) {  // split on ',', keeping empty fields
    f[n++] = c;
    c = strchr(c, ',');
    if (!c) break;
    *c++ = 0;
  }
  if (n < 10 || atoi(f[6]) < 1 || !*f[2] || !*f[4]) return;  // quality 0 = no fix
  bool first = !gpsEver;
  myLat = nmeaCoord(f[2], *f[3]);
  myLon = nmeaCoord(f[4], *f[5]);
  myAlt = (int16_t)atof(f[9]);
  double accDm = (*f[8] ? atof(f[8]) : 99) * 25;  // rough: position_acc ~ HDOP x 2.5 m
  myAccDm = accDm > 0xFFFE ? 0xFFFE : (uint16_t)accDm;
  gpsEver = true;
  lastFixMs = millis();
  if (first) {
    logEvent("gps_fix", "\"lat\":%.6f,\"lon\":%.6f", myLat / 1e7, myLon / 1e7);
    iniPending = true;  // INI again on the first fix
    nextBeaconMs = millis();
  }
}

void gpsTick() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '$') gpsLen = 0;
    if (c == '\r' || c == '\n') {
      if (gpsLen) {
        gpsLine[gpsLen] = 0;
        onGpsLine(gpsLine);
      }
      gpsLen = 0;
    } else if (gpsLen < (int)sizeof gpsLine - 1) {
      gpsLine[gpsLen++] = c;
    }
  }
}
#endif

// ============ USB: phone protocol (dongle) and a few commands ============
#if IS_DONGLE
// One JSON object per line from the website (docs/usb-protocol.md):
// {"type":"send","people":3,"flags":3,"lat":27.68,"lon":85.32,"alt":1300,"acc":10,"time":1761217205}
// {"type":"info_request"}
// lat / lon / alt / acc are null when the phone has no position fix.
void handlePhoneJson(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) {
    Serial.println("# bad JSON from USB");
    return;
  }
  const char* type = doc["type"] | "";
  if (strcmp(type, "info_request") == 0) {
    nodesReported = nodesInRange();
    sendInfo();
    return;
  }
  if (strcmp(type, "send") != 0) {
    Serial.printf("# unknown type \"%s\"\n", type);
    return;
  }
  int people = doc["people"] | 0;
  int flags = doc["flags"] | -1;
  if (people < 1 || people > 100 || flags < 0 || flags > 0x1F) {
    Serial.println("# send rejected: people must be 1-100, flags 0-31");
    return;
  }
  int32_t lat = 0, lon = 0;
  int16_t alt = 0;
  uint8_t acc = NO_FIX_8;
  if (doc["lat"].is<double>() && doc["lon"].is<double>()) {
    lat = toE7(doc["lat"].as<double>());
    lon = toE7(doc["lon"].as<double>());
    alt = doc["alt"] | 0;
    acc = constrain(doc["acc"] | 254, 0, 254);
  }
  createMessage(people, flags, lat, lon, alt, acc, doc["time"] | 0UL);
}
#endif

void printStatus() {
  Serial.printf("# node 0x%04X, %d neighbours\n", MY_ID, nodesInRange());
  for (auto& n : nbrs)
    if (n.used)
      Serial.printf("#   0x%04X %-6s rssi %d snr %.1f progress %.0f m ack %.0f%% seen %lus ago\n", n.id,
                    nbrStateName(n.state), n.rssi, n.snr, progressOf(n), ackRate(n) * 100,
                    (millis() - n.lastSeen) / 1000);
  for (auto& m : msgs) {
    if (!m.used) continue;
    char k[11];
    keyText(m.key, k);
    Serial.printf("#   %s %-12s%s prev 0x%04X next 0x%04X%s%s\n", k, STATE_NAMES[m.s.state], m.s.own ? " own" : "",
                  m.s.prevHop, m.s.nextHop, m.s.delivery == D_OWED ? " delivery owed" : "", m.waiting ? " waiting" : "");
  }
}

void handleUsbLine(char* line) {
#if IS_DONGLE
  if (line[0] == '{') {  // the website
    handlePhoneJson(line);
    return;
  }
#endif
  if (strcmp(line, "status") == 0) {
    printStatus();
  } else if (strcmp(line, "erase") == 0) {
    eraseStore();
#if !IS_HELPER
  } else if (strncmp(line, "send ", 5) == 0) {  // test message: send <people> <flags_hex>
    char* rest;
    long people = strtol(line + 5, &rest, 10);
    long flags = strtol(rest, nullptr, 16);
    uint8_t acc = !gpsFresh() ? NO_FIX_8 : myAccDm / 10 > 254 ? 254 : myAccDm / 10;  // decimetres -> metres
    if (people >= 1 && people <= 100 && flags >= 0 && flags <= 0x1F)
      createMessage(people, flags, myLat, myLon, myAlt, acc, 0);
    else Serial.println("# send <people 1-100> <flags_hex 00-1F>");
#endif
  } else {
    Serial.println("# commands: status | erase | send <people> <flags_hex>");
  }
}

void usbTick() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (usbLen < (int)sizeof usbLine - 1) usbLine[usbLen++] = c;
      continue;
    }
    usbLine[usbLen] = 0;
    usbLen = 0;
    if (usbLine[0]) handleUsbLine(usbLine);
  }
}

// ============ Every second ============
void secondTick() {
  if (millis() - lastSecondMs < 1000) return;
  lastSecondMs = millis();
  nbrTick();
  if (millis() - lastWakeMs >= WAITING_RETRY_MS) {  // try waiting messages again, failed neighbours too
    lastWakeMs = millis();
    wakeWaiting(true);
  }
#if IS_DONGLE
  if (nodesInRange() != nodesReported) {  // tell the phone how many nodes it can reach
    nodesReported = nodesInRange();
    sendInfo();
  }
#endif
}

// ============ Main ============
void setup() {
  Serial.setTxBufferSize(2048);  // room for the helper's DATA block
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);      // never stall the radio when nothing reads USB
  delay(500);

  myLat = toE7(MY_LAT);
  myLon = toE7(MY_LON);
  myAlt = MY_ALT;
  helperLat = toE7(HELPER_LAT);
  helperLon = toE7(HELPER_LON);

  prefs.begin("dhruva", false);
  loadStore();  // messages that survived a reboot carry on
  radioSetup();
#if IS_RELAY
  Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, -1);
#endif
  nextBeaconMs = millis() + random(0, 2000);  // INI at a random moment after boot

  Serial.printf("# Dhruva Jaal %s 0x%04X ready\n", IS_HELPER ? "helper" : IS_DONGLE ? "dongle" : "relay", MY_ID);
}

void loop() {
  usbTick();
  uint8_t b[LORA_MAX + 1];
  int rssi;
  float snr;
  int n = radioReceive(b, rssi, snr);
  if (n > 0) handlePacket(b, n, rssi, snr);
#if IS_RELAY
  gpsTick();
#endif
  beaconTick();
  doWork();
  secondTick();
}
