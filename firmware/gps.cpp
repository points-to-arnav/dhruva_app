#include "gps.h"
#include "role.h"

#if HAS_GPS
#include <math.h>
#include "pins.h"
#include "mesh.h"
#include "timeutil.h"

#define NMEA_MAX 100
#define NMEA_FIELDS 20
#define METRES_PER_HDOP 2.5 // rough NEO-6M estimate: position_acc ~ HDOP x 2.5 m

static char s_line[NMEA_MAX];
static int s_len = 0;

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "$...*hh": XOR of everything between '$' and '*'.
static bool checksum_ok(const char* s) {
  if (s[0] != '$') return false;
  uint8_t x = 0;
  const char* p = s + 1;
  for (; *p && *p != '*'; p++) x ^= (uint8_t)*p;
  if (*p != '*' || hex_val(p[1]) < 0 || hex_val(p[2]) < 0) return false;
  return x == (hex_val(p[1]) << 4 | hex_val(p[2]));
}

// Splits on ',' in place, keeping empty fields; cuts the "*hh".
static int split(char* s, char** f) {
  char* star = strchr(s, '*');
  if (star) *star = 0;
  int n = 0;
  for (char* p = s; n < NMEA_FIELDS;) {
    f[n++] = p;
    char* comma = strchr(p, ',');
    if (!comma) break;
    *comma = 0;
    p = comma + 1;
  }
  return n;
}

// ddmm.mmmm / dddmm.mmmm + hemisphere -> degrees * 1e7
static bool parse_coord(const char* v, const char* hemi, int32_t& out) {
  if (!*v || !*hemi) return false;
  double raw = atof(v);
  double deg = floor(raw / 100);
  double dd = deg + (raw - deg * 100) / 60;
  if (*hemi == 'S' || *hemi == 'W') dd = -dd;
  out = (int32_t)lround(dd * 1e7);
  return true;
}

// $xxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,...
static void on_gga(char** f, int n) {
  if (n < 10 || atoi(f[6]) < 1) return; // quality 0 = no fix
  int32_t lat, lon;
  if (!parse_coord(f[2], f[3], lat) || !parse_coord(f[4], f[5], lon)) return;
  double hdop = *f[8] ? atof(f[8]) : 99;
  double acc_dm = hdop * METRES_PER_HDOP * 10;
  if (acc_dm > 0xFFFE) acc_dm = 0xFFFE;
  long alt = lround(atof(f[9]));
  if (alt < INT16_MIN) alt = INT16_MIN;
  if (alt > INT16_MAX) alt = INT16_MAX;
  mesh_gps_fix(lat, lon, (int16_t)alt, (uint16_t)acc_dm);
}

// $xxRMC,hhmmss.ss,A,lat,N,lon,E,speed,course,ddmmyy,...
static void on_rmc(char** f, int n) {
  if (n < 10 || f[2][0] != 'A' || strlen(f[1]) < 6 || strlen(f[9]) != 6) return;
  auto two = [](const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
  uint32_t t = utc_to_unix(2000 + two(f[9] + 4), two(f[9] + 2), two(f[9]), two(f[1]), two(f[1] + 2), two(f[1] + 4));
  mesh_gps_time(t);
}

static void handle_sentence() {
  if (!checksum_ok(s_line)) return;
  char* f[NMEA_FIELDS];
  int n = split(s_line, f);
  if (strlen(f[0]) != 6) return;
  const char* kind = f[0] + 3; // skip "$GP" / "$GN"
  if (strcmp(kind, "GGA") == 0) on_gga(f, n);
  else if (strcmp(kind, "RMC") == 0) on_rmc(f, n);
}

void gps_begin() { Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, -1); }

void gps_poll() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '$') s_len = 0;
    if (c == '\r' || c == '\n') {
      if (s_len > 0) {
        s_line[s_len] = 0;
        handle_sentence();
      }
      s_len = 0;
    } else if (s_len < NMEA_MAX - 1) {
      s_line[s_len++] = c;
    }
  }
}

#else

void gps_begin() {}
void gps_poll() {}

#endif
