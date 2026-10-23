#include "timeutil.h"

// Civil calendar from/to days since 1970-01-01 (H. Hinnant's algorithms).
void unix_to_utc(uint32_t t, char* out, size_t n) {
  int32_t z = (int32_t)(t / 86400) + 719468;
  uint32_t sod = t % 86400;
  int32_t era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  snprintf(out, n, "%04d-%02u-%02u %02u:%02u:%02u UTC", (int)y, (unsigned)m, (unsigned)d,
           (unsigned)(sod / 3600), (unsigned)(sod / 60 % 60), (unsigned)(sod % 60));
}

uint32_t utc_to_unix(int year, int month, int day, int hour, int minute, int second) {
  int32_t y = year - (month <= 2 ? 1 : 0);
  int32_t era = y / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int32_t days = era * 146097 + (int32_t)doe - 719468;
  return (uint32_t)days * 86400 + hour * 3600 + minute * 60 + second;
}
