#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }

/* yield() is inert by default, matching its real meaning (cooperate with
 * other tasks, do not advance time). A test that busy-waits on a timeout
 * (e.g. HearthLink::readLine's wait loop, which polls millis() and calls
 * yield() between reads) would otherwise spin forever against a clock
 * nothing else advances. Such a test must opt in explicitly by setting
 * g_yieldAdvanceMs before the call and resetting it to 0 afterwards, so the
 * dependency on simulated time is visible at the call site that needs it
 * rather than hidden inside yield() itself. */
extern uint32_t g_yieldAdvanceMs;
inline void yield() { g_millis += g_yieldAdvanceMs; }

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
