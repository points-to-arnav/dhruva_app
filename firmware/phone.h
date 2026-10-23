// Dongle only: the phone app's USB protocol (docs/usb-protocol.md).
// SEND|people|flags|lat|lon|alt|acc|unix_time and INFO? in; STATUS and INFO out.
#pragma once
#include <Arduino.h>

// True if the line was a phone protocol line (handled), false for CLI lines.
bool phone_line(char* line);
void phone_status(uint32_t key, uint8_t state, uint16_t next_hop);
void phone_tick(int64_t now); // INFO whenever the number of nodes in range changes
