// Unix time <-> UTC calendar, for GPS time and the helper's printout.
#pragma once
#include <Arduino.h>

// "2026-10-23 11:00:05 UTC"; out needs 24 bytes.
void unix_to_utc(uint32_t t, char* out, size_t n);
uint32_t utc_to_unix(int year, int month, int day, int hour, int minute, int second);
