// USB logs (design/plan.md §5, "Logs"): readable lines start with "# ", JSON
// events with "@". Neither starts with STATUS or INFO, so the phone app
// ignores both.
#pragma once
#include <Arduino.h>

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// @{"t":<ms>,"node":"0x0011","ev":"<ev>",<fields>}; fields may be "".
void log_json(const char* ev, const char* fields, ...) __attribute__((format(printf, 2, 3)));
