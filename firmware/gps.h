// Relay only: NEO-6M on UART1 (RX = GPIO20, 9600 baud). A small NMEA reader
// (GGA for the fix, RMC for date and time) feeds mesh_gps_fix() and
// mesh_gps_time() (design/algorithm-design.md §11).
#pragma once
#include <Arduino.h>

void gps_begin();
void gps_poll();
