#include "mesh_log.h"
#include <stdarg.h>
#include "mesh_config.h"

void log_line(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.printf("# %s\n", buf);
}

void log_json(const char* ev, const char* fields, ...) {
  char body[200];
  va_list ap;
  va_start(ap, fields);
  vsnprintf(body, sizeof body, fields, ap);
  va_end(ap);
  Serial.printf("@{\"t\":%lu,\"node\":\"0x%04X\",\"ev\":\"%s\"%s%s}\n", (unsigned long)millis(),
                (unsigned)cfg.node_id, ev, body[0] ? "," : "", body);
}
