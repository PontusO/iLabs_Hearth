/*
 * Hearth.h - the Hearth global: link configuration, diagnostics and the
 * link-event surface for the AT+MT link to the ESP32-C6 co-processor.
 *
 * Everything here has no arduino-esp32 counterpart. It lives on Hearth
 * rather than on the Matter-named class (Task 5) because the naming rule
 * bars adding members to a Matter-named class: that surface must stay
 * exactly what upstream defines, nothing more. See CLAUDE.md, "Naming and
 * legal constraint".
 */
#pragma once

#include <functional>
#include <Arduino.h>
#include "HearthLink.h"

/*
 * Link used when begin() is skipped entirely. Serial1 is the documented
 * *assumption* for the Challenger's host <-> co-processor UART, not a
 * verified fact: a later task confirms the real wiring on hardware. A board
 * variant can override this with -DHEARTH_DEFAULT_SERIAL=... before this
 * header is first included.
 */
#ifndef HEARTH_DEFAULT_SERIAL
#define HEARTH_DEFAULT_SERIAL Serial1
#endif

enum hearthEvent_t {
  HEARTH_LINK_UP = 0,
  HEARTH_LINK_DOWN,
  HEARTH_COPROCESSOR_REBOOTED,
  HEARTH_PROTOCOL_ERROR,
};

class HearthClass {
public:
  using hearthEventCB = std::function<void(hearthEvent_t)>;

  HearthClass();

  /*
   * Start the underlying link on `serial`, e.g. Hearth.begin(Serial1). The
   * caller is responsible for having already started `serial` at the
   * intended baud (Stream itself has no begin()); `baud` is accepted for
   * interface parity and is the value the fallback below uses. begin() may
   * be skipped entirely: the first use of the link then lazily starts
   * HEARTH_DEFAULT_SERIAL at 115200 on its own (target builds only; there
   * is no default serial port on the host test build).
   */
  void begin(Stream &serial, unsigned long baud = 115200);

  /* True if a bare AT probe round-trips OK. */
  bool linkUp();

  /* Last +MTERR code any layer above HearthLink reported; 0 if none. */
  int lastError() const {
    return _lastError;
  }

  /* Register the link-event callback (co-processor reboot, protocol
   * trouble). At most one; a later call replaces the previous one. */
  void onLinkEvent(hearthEventCB cb);

  /* Drain any pending URCs without blocking. Call from loop(). */
  void poll();

  /* AT+MTVER?. Empty String on failure. */
  String firmwareVersion();

  /*
   * True once the Matter-named layer (Task 5) has warned the host's Serial
   * that applying the sketch's declared endpoint composition will
   * invalidate controller caches on a device that already belongs to a
   * fabric. Lives here, not on Matter, because the naming rule bars adding
   * members to Matter-named classes, including for test introspection.
   */
  bool warnedAboutRecommission() const {
    return _warnedAboutRecommission;
  }
  /* For the Matter-named layer to call once it has issued that warning. */
  void hearthSetWarnedAboutRecommission(bool warned = true) {
    _warnedAboutRecommission = warned;
  }

  /*
   * Arm the link for a reboot the host itself is about to trigger, e.g.
   * immediately before sending AT+MTEPAPPLY. While armed, the next
   * +MTREADY completes the arm silently instead of raising
   * HEARTH_COPROCESSOR_REBOOTED: see hearthExpectedRebootSeen().
   */
  void hearthArmExpectedReboot();

  /*
   * True once the +MTREADY armed by hearthArmExpectedReboot() has arrived.
   * Does not clear itself; call hearthArmExpectedReboot() again before
   * arming the next wait. The intended use (Task 5's AT+MTEPAPPLY
   * sequence) is a non-blocking poll loop the caller drives itself, the
   * same pattern HearthLink::poll() already uses for URCs in general:
   *   Hearth.hearthArmExpectedReboot();
   *   Hearth.hearthCommand("AT+MTEPAPPLY");
   *   while (!Hearth.hearthExpectedRebootSeen()) { Hearth.poll(); ... own timeout ... }
   */
  bool hearthExpectedRebootSeen() const {
    return _expectedRebootSeen;
  }

  /*
   * Send one AT+MT command through the link and remember its outcome as the
   * last error: a +MTERR code on failure, 0 otherwise. Returns the
   * HearthLink::command() result unchanged (0 OK, >0 +MTERR code, -1 plain
   * ERROR, -2 timeout / link not started).
   */
  int hearthCommand(const char *cmd, HearthLink::LineCb onLine = nullptr, void *arg = nullptr);

  /* Record an error code for a caller that fails before reaching the wire,
   * e.g. an attribute value type the AT protocol cannot carry. */
  void hearthSetError(int code);

  /* The underlying transport, for callers that need it directly. */
  HearthLink &link() {
    return _link;
  }

private:
  void hearthEnsureLink();
  void hearthRaiseEvent(hearthEvent_t e);
  static void hearthOnURCLine(const char *line, void *arg);
  static void hearthOnVerLine(const char *line, void *arg);

  HearthLink _link;
  int _lastError;
  bool _warnedAboutRecommission;
  bool _expectingReboot;
  bool _expectedRebootSeen;
  hearthEventCB _linkEventCB;
};

extern HearthClass Hearth;
