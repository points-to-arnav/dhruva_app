#include "geo.h"
#include <math.h>

float geo_dist_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  const double k = 1e-7 * M_PI / 180.0;
  double p1 = lat1 * k, p2 = lat2 * k;
  double dp = ((double)lat2 - lat1) * k; // in double: longitude differences overflow int32
  double dl = ((double)lon2 - lon1) * k;
  double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  if (a > 1) a = 1;
  return (float)(2 * 6371000.0 * asin(sqrt(a)));
}
