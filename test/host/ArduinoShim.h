#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
/* Real yield() just cooperates with other tasks; here it also advances the
 * simulated clock by 1ms, so a blocking wait loop that polls millis() (e.g.
 * HearthLink::readLine's timeout) actually elapses instead of spinning
 * forever against a clock nothing else is advancing. */
inline void yield() { g_millis += 1; }

class String : public std::string {
public:
  String() {}
  String(const char *s) : std::string(s ? s : "") {}
  const char *c_str() const { return std::string::c_str(); }
};

class Print {
public:
  virtual size_t write(uint8_t c) = 0;
  size_t write(const char *s) {
    size_t n = 0;
    while (*s) { n += write((uint8_t)*s++); }
    return n;
  }
  size_t print(const char *s) { return write(s); }
  size_t println(const char *s) { return write(s) + write("\r\n"); }
  size_t printf(const char *fmt, ...);
};

class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}
};
