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

/* The host build's re-entrancy gate (final review I4): a flag a test holds
 * true while it wants the link to read as busy, so a nested drain (the one a
 * sketch callback reaches from inside a URC dispatch) returns before it
 * touches the wire, exactly as while the link's own _busy latch is held.
 * HearthLink::busy() consults it on the host build; the device build has no
 * such flag (the latch is the gate there). Default false; every test but the
 * F2a I4 case leaves it false. */
extern bool g_linkBusyHeld;

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

#include <vector>

/* GPIO stand-ins (U1). The core drives the co-processor lines only when
 * HEARTH_HAS_GPIO is defined (test_gpio_reset builds with it); every call
 * is recorded so a test can assert the exact sequence. Enumerators, not
 * macros, as in the Arduino core, so nothing else is rewritten. */
enum { LOW = 0, HIGH = 1 };
enum { INPUT = 0, OUTPUT = 1 };
struct ShimGpioEvent {
  char kind;  /* 'm' pinMode, 'w' digitalWrite */
  int pin;
  int value;
};
extern std::vector<ShimGpioEvent> g_gpioLog;
inline void pinMode(int pin, int mode) { g_gpioLog.push_back({'m', pin, mode}); }
inline void digitalWrite(int pin, int value) { g_gpioLog.push_back({'w', pin, value}); }
