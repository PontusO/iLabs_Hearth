/*
 * ArduinoShim.cpp - definitions for the host-test stand-ins declared in
 * ArduinoShim.h.
 */
#include "ArduinoShim.h"
#include <stdio.h>
#include <stdarg.h>

uint32_t g_millis = 0;

size_t Print::printf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) {
    return 0;
  }
  return write(buf);
}
