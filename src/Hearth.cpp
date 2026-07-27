/*
 * Hearth.cpp - the Hearth global, implementation.
 */
#include "Hearth.h"
#include <string.h>

HearthClass::HearthClass()
  : _lastError(0), _warnedAboutRecommission(false), _expectingReboot(false), _expectedRebootSeen(false) {}

void HearthClass::begin(Stream &serial, unsigned long baud) {
  (void)baud;  // see the header: a caller-supplied Stream has no begin() of its own to call with it;
               // only the HEARTH_DEFAULT_SERIAL fallback in hearthEnsureLink() actually uses a baud.
  _link.begin(serial);
  _link.onURC(hearthOnURCLine, this);
  _expectingReboot = false;
  _expectedRebootSeen = false;
}

/*
 * begin() was never called: fall back to HEARTH_DEFAULT_SERIAL at 115200.
 * Guarded on ARDUINO, which every Arduino core (including arduino-pico)
 * predefines and the host test build never does, because Serial1 does not
 * exist on the host and HEARTH_DEFAULT_SERIAL is Serial1 unless a board
 * variant overrides it.
 */
void HearthClass::hearthEnsureLink() {
  if (_link.started()) {
    return;
  }
#ifdef ARDUINO
  HEARTH_DEFAULT_SERIAL.begin(115200);
  begin(HEARTH_DEFAULT_SERIAL, 115200);
#endif
}

bool HearthClass::linkUp() {
  return hearthCommand("AT") == 0;
}

void HearthClass::onLinkEvent(hearthEventCB cb) {
  _linkEventCB = cb;
}

void HearthClass::hearthRaiseEvent(hearthEvent_t e) {
  if (_linkEventCB) {
    _linkEventCB(e);
  }
}

void HearthClass::poll() {
  hearthEnsureLink();
  _link.poll();
}

void HearthClass::hearthOnVerLine(const char *line, void *arg) {
  String *out = (String *)arg;
  if (strncmp(line, "+MTVER:", 7) != 0) {
    return;
  }
  *out = String(line + 7);
}

String HearthClass::firmwareVersion() {
  String v;
  if (hearthCommand("AT+MTVER?", hearthOnVerLine, &v) != 0) {
    return String("");
  }
  return v;
}

int HearthClass::hearthCommand(const char *cmd, HearthLink::LineCb onLine, void *arg) {
  hearthEnsureLink();
  int rc = _link.command(cmd, onLine, arg);
  _lastError = (rc > 0) ? rc : 0;
  return rc;
}

void HearthClass::hearthSetError(int code) {
  _lastError = code;
}

void HearthClass::hearthArmExpectedReboot() {
  _expectingReboot = true;
  _expectedRebootSeen = false;
}

/*
 * The URC handler installed on HearthLink. Only +MTREADY is acted on here:
 * an unexpected one means the co-processor rebooted under us, so every
 * cached endpoint ID is now unconfirmed (HEARTH_COPROCESSOR_REBOOTED). An
 * expected one, i.e. hearthArmExpectedReboot() was called first (Task 5,
 * before AT+MTEPAPPLY), clears the arm silently instead: see
 * hearthExpectedRebootSeen().
 *
 * +MTEVT, +MTATTR and +MTIDENT are recognised but not routed anywhere yet:
 * Task 5 wires their dispatch to the Matter-named layer. They must not be
 * misclassified as protocol trouble here in the meantime.
 */
void HearthClass::hearthOnURCLine(const char *line, void *arg) {
  HearthClass *self = (HearthClass *)arg;
  if (strncmp(line, "+MTREADY", 8) == 0) {
    if (self->_expectingReboot) {
      self->_expectingReboot = false;
      self->_expectedRebootSeen = true;
      return;
    }
    self->hearthRaiseEvent(HEARTH_COPROCESSOR_REBOOTED);
    return;
  }
  // +MTEVT / +MTATTR / +MTIDENT: Task 5 wires these to the Matter-named layer.
}

HearthClass Hearth;
