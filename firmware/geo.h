// Distances for geographic routing and the demo range filter.
#pragma once
#include <Arduino.h>

// Horizontal great-circle distance in metres between two points in degrees * 1e7.
float geo_dist_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
