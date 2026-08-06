/*
 * Hearth.cpp - the Hearth global and ArduinoMatter/Matter, implementation.
 */
/* HearthGlobal.h, not Hearth.h: ArduinoMatter's implementation below calls
 * through the Hearth object, so it needs the declaration even in a build
 * that set NO_GLOBAL_INSTANCES or NO_GLOBAL_HEARTH. This file happens to
 * define the object above its first use today, which would have covered it
 * by accident; the include makes it not an accident. See that header. */
#include "HearthGlobal.h"
#include "MatterEndPoint.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

HearthClass::HearthClass()
  : _lastError(0),
    _warnedAboutRecommission(false),
    _reconcileFailed(false),
    _expectingReboot(false),
    _expectedRebootSeen(false),
    _expectedRebootArmedAt(0),
    _expectedRebootTimeoutMs(0),
    _cmdRespQueueCount(0) {}

void HearthClass::begin(Stream &serial, unsigned long baud) {
  (void)baud;  // see the header: a caller-supplied Stream has no begin() of its own to call with it;
               // only the HEARTH_SERIAL_PORT path in hearthEnsureLink() actually uses a baud.
  _link.begin(serial);
  _link.onURC(hearthOnURCLine, this);
  _reconcileFailed = false;  // a new link is a new start; see hearthReconcileFailed()
  _expectingReboot = false;
  _expectedRebootSeen = false;
  _expectedRebootArmedAt = 0;
  _expectedRebootTimeoutMs = 0;
}

/*
 * begin() was never called, which is the normal case: bring the link up on
 * the UART the board variant wires to the co-processor, then reset the
 * co-processor into a known state.
 *
 * This is the whole reason a sketch never names a serial port. The API this
 * library mirrors is arduino-esp32's, where Matter.begin() takes no port
 * because the radio is on-die; there is no parameter to add without
 * breaking the source compatibility the port exists for. So the wiring has
 * to come from the variant, and the bring-up has to happen on first use.
 *
 * Guarded on ARDUINO, which every Arduino core (including arduino-pico)
 * predefines and the host test build never does: no serial port object and
 * no GPIO exist there, and the host tests inject a MockStream through
 * begin(Stream&) instead.
 */
void HearthClass::hearthEnsureLink() {
  if (_link.started()) {
    return;
  }
#ifdef ARDUINO
#ifndef HEARTH_SERIAL_PORT
#error "iLabs Hearth requires a board variant that defines ESP_SERIAL_PORT (the UART wired to the ESP32-C6 co-processor), e.g. an iLabs Challenger WiFi6 board. Override with -DHEARTH_SERIAL_PORT=... for a board no variant describes."
#else
  HEARTH_SERIAL_PORT.begin(HEARTH_LINK_BAUD);
  begin(HEARTH_SERIAL_PORT, HEARTH_LINK_BAUD);
  hearthResetCoprocessor();
#endif
#endif
}

/*
 * The reset dance, lifted from the sibling iLabs_ESP-NOW library so the two
 * behave identically on the same board. MODE high selects run mode rather
 * than serial download; the input flush happens while reset is asserted, so
 * that everything discarded belongs to the firmware that is going away and
 * nothing from after the release is lost.
 *
 * Doing this at all (rather than trusting whatever state the co-processor
 * happens to be in) is what makes a host reset and a power-on look the
 * same to the sketch. Without it, a host-only reset leaves the C6 mid-
 * conversation with a host that has forgotten every endpoint id it cached.
 */
void HearthClass::hearthResetCoprocessor() {
#if defined(ARDUINO) && defined(PIN_ESP_MODE) && defined(PIN_ESP_RST)
  if (!_link.started()) {
    return;
  }
  pinMode(PIN_ESP_MODE, OUTPUT);
  digitalWrite(PIN_ESP_MODE, HIGH);  /* run mode, not serial download */
  pinMode(PIN_ESP_RST, OUTPUT);
  digitalWrite(PIN_ESP_RST, LOW);    /* assert reset */
  delay(5);
  _link.flushInput();                /* drop pre-reset noise */
  digitalWrite(PIN_ESP_RST, HIGH);   /* release: the C6 boots */

  /* Arm first, then wait: waitReady() dispatches the marker through the
   * ordinary URC path, and the arm is what stops that path from reporting
   * this entirely expected boot as HEARTH_COPROCESSOR_REBOOTED. */
  hearthArmExpectedReboot(HEARTH_READY_TIMEOUT_MS);
  _link.waitReady(HEARTH_READY_TIMEOUT_MS);

  /* Whether or not the marker arrived, this boot is over. Clearing the arm
   * stops it swallowing a later, genuinely spontaneous reboot; clearing the
   * seen flag stops the next AT+MTEPAPPLY from mistaking this boot's marker
   * for the one its own reboot owes it. */
  hearthDisarmExpectedReboot();
  _expectedRebootSeen = false;
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

void HearthClass::hearthReportProtocolError() {
  hearthRaiseEvent(HEARTH_PROTOCOL_ERROR);
}

void HearthClass::poll() {
  hearthEnsureLink();
  /*
   * Dispatch first, check expiry second. A +MTREADY the co-processor sent
   * well within the deadline may already be sitting unread in the stream if
   * the host was simply slow to call poll(); if the expiry check ran first
   * it would clear the arm out from under that already-arrived reply, and
   * the reply would then be misdispatched as a spontaneous
   * HEARTH_COPROCESSOR_REBOOTED even though nothing actually went wrong.
   * Once _link.poll() has had a chance to consume a buffered +MTREADY (via
   * hearthOnURCLine(), which clears _expectingReboot itself on the expected
   * path), the expiry check below has nothing left to clear and the
   * ordering stops mattering.
   */
  _link.poll();
  hearthCheckExpectedRebootExpiry();
  /* Fix round 1 (C3 review): drain any +MTCMD verdicts _link.poll() just
   * queued, now that its busy gate has been released. See
   * hearthDrainCmdRespQueue()'s own comment for why this cannot happen
   * inside dispatchURC() itself. */
  hearthDrainCmdRespQueue();
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
  /*
   * Pump the URC queue before every command. Nothing else can: upstream
   * runs the Matter stack in a background task, so an unmodified sketch
   * contains no poll() call and there is no parity surface on which to add
   * one. What every upstream loop() *does* contain is at least one call
   * into the library (isDeviceCommissioned(), a sensor update, a toggle),
   * and every one of those reaches the wire through here. Draining here is
   * therefore what makes a controller-driven change reach the sketch's
   * onChange() on an unmodified sketch. A sketch whose loop() calls nothing
   * into the library at all still has to call Hearth.poll() itself; the
   * README says so.
   *
   * _link.poll() is a no-op when called re-entrantly, so a callback this
   * drain dispatches that calls back into the library cannot recurse here.
   *
   * One consequence, deliberate: this also drains during
   * ArduinoMatter::begin()'s own AT+MTEP?, when no endpoint has adopted an
   * id yet, so a +MTATTR arriving in that window is consumed and dropped.
   * hearthDispatchAttr() below carries the reasoning for why that is the
   * right answer rather than a gap.
   */
  poll();
  /* Same ordering reason as poll(): a buffered +MTREADY that arrives on the
   * wire ahead of this command's own response lines is read and dispatched
   * by _link.command()'s read loop before it returns, so the expiry check
   * must run after, not before, or a reply that was already sitting there
   * could be misclassified the same way described in poll() above. */
  int rc = _link.command(cmd, onLine, arg);
  hearthCheckExpectedRebootExpiry();
  _lastError = (rc > 0) ? rc : 0;
  /* Fix round 1 (C3 review): drain any +MTCMD verdicts queued during either
   * the poll() drain above or this command's own read loop, now that
   * _link.command()'s busy gate has been released. Runs after _lastError is
   * set from `rc` above, and uses _link.command() directly (not this
   * hearthCommand() wrapper) so a queued AT+MTCMDRESP's own reply -- even a
   * harmless +MTERR:1 for a stale seq -- can never clobber the caller's own
   * lastError(). See hearthDrainCmdRespQueue()'s comment for the full
   * reasoning. */
  hearthDrainCmdRespQueue();
  return rc;
}

void HearthClass::hearthSetError(int code) {
  _lastError = code;
}

void HearthClass::hearthArmExpectedReboot(uint32_t timeout_ms) {
  _expectingReboot = true;
  _expectedRebootSeen = false;
  _expectedRebootArmedAt = millis();
  _expectedRebootTimeoutMs = timeout_ms;
}

void HearthClass::hearthDisarmExpectedReboot() {
  _expectingReboot = false;
}

/*
 * Backstop for a caller that arms and then never disarms on its own
 * timeout path: HEARTH_REBOOT_ARM_TIMEOUT_MS (or whatever was passed to
 * hearthArmExpectedReboot()) after arming, the arm clears itself even
 * though no +MTREADY arrived. Deliberately does not mark
 * _expectedRebootSeen or raise any event: nothing is known to have
 * happened yet, this only stops a stale arm from swallowing the *next*,
 * unrelated spontaneous reboot as if it were the one that was expected.
 */
void HearthClass::hearthCheckExpectedRebootExpiry() {
  if (!_expectingReboot) {
    return;
  }
  uint32_t elapsed = millis() - _expectedRebootArmedAt;
  if (elapsed >= _expectedRebootTimeoutMs) {
    _expectingReboot = false;
  }
}

/*
 * +MTEVT:<bit>[,<detail>] -> matterEvent_t, from AT_MT_SPEC.md S3.11. Bits
 * 10, 11 and 24 carry a <detail> of 1 or 0 (up/down); every other bit's
 * detail, when even present, is left at 0 in the stub ChipDeviceEvent since
 * nothing in this port reads it. Bit 23 is reserved by the spec and has no
 * matterEvent_t of its own; it is mapped here to MATTER_ESP32_SPECIFIC_EVENT
 * only so the table has an entry for every index 0-26, not because that is
 * a meaningful pairing on the wire.
 */
static const matterEvent_t kEventForBit[27] = {
  /*  0 */ MATTER_COMMISSIONING_WINDOW_OPEN,
  /*  1 */ MATTER_COMMISSIONING_SESSION_STARTED,
  /*  2 */ MATTER_COMMISSIONING_SESSION_STOPPED,
  /*  3 */ MATTER_COMMISSIONING_COMPLETE,
  /*  4 */ MATTER_COMMISSIONING_WINDOW_CLOSED,
  /*  5 */ MATTER_FAIL_SAFE_TIMER_EXPIRED,
  /*  6 */ MATTER_FABRIC_WILL_BE_REMOVED,
  /*  7 */ MATTER_FABRIC_REMOVED,
  /*  8 */ MATTER_FABRIC_COMMITTED,
  /*  9 */ MATTER_FABRIC_UPDATED,
  /* 10 */ MATTER_WIFI_CONNECTIVITY_CHANGE,
  /* 11 */ MATTER_INTERNET_CONNECTIVITY_CHANGE,
  /* 12 */ MATTER_INTERFACE_IP_ADDRESS_CHANGED,
  /* 13 */ MATTER_OPERATIONAL_NETWORK_STARTED,
  /* 14 */ MATTER_DNSSD_INITIALIZED,
  /* 15 */ MATTER_SERVER_READY,
  /* 16 */ MATTER_CHIPOBLE_CONNECTION_ESTABLISHED,
  /* 17 */ MATTER_CHIPOBLE_CONNECTION_CLOSED,
  /* 18 */ MATTER_CHIPOBLE_ADVERTISING_CHANGE,
  /* 19 */ MATTER_BLE_DEINITIALIZED,
  /* 20 */ MATTER_OTA_STATE_CHANGED,
  /* 21 */ MATTER_BINDINGS_CHANGED_VIA_CLUSTER,
  /* 22 */ MATTER_TIME_SYNC_CHANGE,
  /* 23 */ MATTER_ESP32_SPECIFIC_EVENT, /* bit 23 is reserved */
  /* 24 */ MATTER_THREAD_CONNECTIVITY_CHANGE,
  /* 25 */ MATTER_THREAD_STATE_CHANGE,
  /* 26 */ MATTER_THREAD_INTERFACE_STATE_CHANGE,
};

namespace {

/*
 * Parses "<ep>,<cl>,<attr>,<val>" (the text after "+MTATTR:") and routes it
 * to that endpoint's attributeChangeCB, if the endpoint is one the sketch
 * declared. The wire carries a bare integer with no type tag
 * (MatterEndPoint.h), and at this generic dispatch point there is no
 * built-in per-attribute type knowledge, unlike the synchronous
 * getAttributeVal() path where the *caller* already knows what type it
 * asked for. The value is therefore rebuilt via the target endpoint's own
 * hearthAttrTypeFor(cluster, attribute) -- each concrete endpoint type
 * (Tasks 6-8) overrides this for the clusters/attributes it owns, so the
 * esp_matter_attr_val_t handed to attributeChangeCB lands in the same union
 * member upstream's own implementations read (val.b for a bool attribute,
 * val.u for an unsigned one, and so on), matching upstream exactly rather
 * than forcing every override to know about this port's wire format. An
 * endpoint type that does not override hearthAttrTypeFor() gets
 * ESP_MATTER_VAL_TYPE_INTEGER, .val.i holding the raw signed wire value,
 * same as before this existed.
 *
 * An ep with no registered endpoint (the root endpoint, or one the sketch
 * never declared) is dropped silently: both are legitimately not ours, per
 * AT_MT_SPEC.md S4's own note that the root endpoint is intentionally never
 * reported.
 *
 * A third case reaches the same drop and is worth naming, because it is not
 * obviously the same thing (re-review, MINOR 4): a +MTATTR that arrives
 * *before reconcile has finished*. hearthCommand() drains URCs at the top of
 * every call, including the AT+MTEP? that ArduinoMatter::begin() issues, and
 * at that moment every declared endpoint still has endpoint_id 0, so nothing
 * here matches and the line is consumed and discarded.
 *
 * That is deliberate rather than a gap to be closed later:
 *
 *   - there is no correct alternative delivery. Matching a real endpoint id
 *     against endpoints that all still carry 0 is exactly the Root Node
 *     confusion hearthFindByEndPointId()'s own guard exists to prevent;
 *   - a composition change reboots the C6 (AT+MTEPAPPLY), so anything
 *     buffered from before that point is stale by construction;
 *   - the C6, not the host, holds the authoritative value, and a sketch that
 *     wants it can read it back with getAttributeVal() once ids are adopted.
 *     Upstream's own examples resynchronise here anyway, calling
 *     updateAccessory() right after Matter.begin();
 *   - the window is one AT round trip on the ordinary no-change boot.
 *     Buffering across it would mean a queue whose depth and overflow policy
 *     are a new failure mode in exchange for values that are already stale.
 *
 * Pinned by test_urcs_arriving_before_reconcile_are_dropped()
 * (test_onofflight.cpp), which also asserts that the very next URC, after
 * ids are adopted, is delivered normally.
 */
void hearthDispatchAttr(const char *rest) {
  char *end;
  unsigned long ep = strtoul(rest, &end, 10);
  if (end == rest || *end != ',') {
    return;
  }
  unsigned long cluster = strtoul(end + 1, &end, 10);
  if (*end != ',') {
    return;
  }
  unsigned long attribute = strtoul(end + 1, &end, 10);
  if (*end != ',') {
    return;
  }
  long value = strtol(end + 1, &end, 10);

  MatterEndPoint *target = MatterEndPoint::hearthFindByEndPointId((uint16_t)ep);
  if (!target) {
    return;
  }
  esp_matter_val_type_t type = target->hearthAttrTypeFor((uint32_t)cluster, (uint32_t)attribute);
  esp_matter_attr_val_t val = hearthAttrValFromLong(type, value);
  target->attributeChangeCB((uint16_t)ep, (uint32_t)cluster, (uint32_t)attribute, &val);
}

/* Parses "<ep>,<enabled>" (the text after "+MTIDENT:") and routes it to that
 * endpoint's endpointIdentifyCB. Same silent-drop policy as
 * hearthDispatchAttr() for an ep the sketch did not declare. */
void hearthDispatchIdent(const char *rest) {
  char *end;
  unsigned long ep = strtoul(rest, &end, 10);
  if (end == rest || *end != ',') {
    return;
  }
  int enabled = atoi(end + 1);

  MatterEndPoint *target = MatterEndPoint::hearthFindByEndPointId((uint16_t)ep);
  if (!target) {
    return;
  }
  target->endpointIdentifyCB((uint16_t)ep, enabled != 0);
}

}  // namespace

/* Parses "<bit>[,<detail>]" (the text after "+MTEVT:") and, if a sketch has
 * registered one, calls ArduinoMatter's event callback. Bit 27 is a
 * Hearth-specific transport-mismatch event that goes to the link-event
 * callback instead. Bits 28-31 (reserved per S3.11) and malformed input
 * are dropped silently, the same policy given for an unrecognised endpoint
 * in hearthDispatchAttr()/hearthDispatchIdent() in the anonymous namespace. */
void HearthClass::hearthDispatchEvt(const char *rest, HearthClass *self) {
  char *end;
  long bit = strtol(rest, &end, 10);
  if (end == rest || bit < 0) {
    return;
  }
  if (bit == 27) {
    /* Transport mismatch is a Hearth extension with no upstream
     * matterEvent_t; it goes to the link-event callback. */
    self->hearthRaiseEvent(HEARTH_TRANSPORT_MISMATCH);
    return;
  }
  if (bit >= 27) {
    return;
  }
  int detail = 0;
  if (*end == ',') {
    detail = atoi(end + 1);
  }
  if (!ArduinoMatter::_matterEventCB) {
    return;
  }
  chip::DeviceLayer::ChipDeviceEvent ev;
  ev.bit = (uint8_t)bit;
  ev.detail = detail;
  ArduinoMatter::_matterEventCB(kEventForBit[bit], &ev);
}

/*
 * "<seq>,<ep>,<cluster>,<command>" (text after "+MTCMD:"), AT_MT_SPEC.md
 * S3.17: a controller invoked a command that needs an app-level verdict.
 * Routes to the named endpoint's hearthOnForwardedCommand() (MatterEndPoint.h)
 * at dispatch time -- the timing the user's callback sees is unchanged --
 * but does NOT write AT+MTCMDRESP here. An endpoint the sketch never
 * declared, or one whose override still says no (the base class default),
 * both deny -- fail closed, per the wire contract.
 *
 * Fix round 1 (C3 review, CRITICAL): this function used to send the reply
 * immediately via a fire-and-forget HearthLink::sendLine(), reasoning that
 * dispatchURC() runs with the link's busy gate already held (by the outer
 * command()/poll() that delivered this URC), so the ordinary
 * hearthCommand() path would be refused with HEARTH_CMD_REENTRANT. That
 * refusal reasoning was right, but the fix was wrong: a write with no
 * reader waiting for it leaves its own OK/+MTERR:1 terminal ownerless on
 * this single-reader link. Confirmed against the real library, not just
 * argued: a later command's read loop -- worst case the very next
 * hearthCommand() call, sent immediately after this dispatch returns --
 * can read that ownerless OK as ITS OWN terminal instead of the reply it
 * actually asked for. A rejected AT+MTLOCK write (+MTERR:2) then read back
 * as success, with the cache updated to match.
 *
 * The real fix: enqueue (seq, verdict) here and let
 * hearthDrainCmdRespQueue() -- called from hearthCommand() and poll(),
 * both AFTER their own _link.command()/_link.poll() call returns, i.e.
 * with the busy gate released -- send it through the ordinary
 * _link.command() path, which blocks for and consumes its own terminal
 * before anything else on this link runs. See that method's own comment.
 *
 * A malformed line (fields do not parse) is dropped without a reply, the
 * same silent-drop policy hearthDispatchAttr()/hearthDispatchIdent() use
 * above: the firmware's own +MTCMDTO:<seq> already covers "no answer
 * arrived in time", so there is no case here that needs a reply this
 * function cannot construct.
 */
void HearthClass::hearthDispatchCmd(const char *rest, HearthClass *self) {
  char *end;
  unsigned long seq = strtoul(rest, &end, 10);
  if (end == rest || *end != ',') {
    return;
  }
  unsigned long ep = strtoul(end + 1, &end, 10);
  if (*end != ',') {
    return;
  }
  unsigned long cluster = strtoul(end + 1, &end, 10);
  if (*end != ',') {
    return;
  }
  unsigned long command = strtoul(end + 1, &end, 10);

  MatterEndPoint *target = MatterEndPoint::hearthFindByEndPointId((uint16_t)ep);
  bool verdict = target && target->hearthOnForwardedCommand((uint32_t)cluster, (uint32_t)command);

  self->hearthEnqueueCmdResp((uint32_t)seq, verdict);
}

/*
 * Enqueue a verdict for hearthDrainCmdRespQueue() to send once the busy
 * gate is released. Called only from inside dispatchURC() (hearthDispatchCmd()
 * above), so this must never touch the wire itself -- see that function's
 * comment for why.
 *
 * A full queue drops the newest entry silently rather than growing
 * unbounded or overwriting an older one: see the queue's own depth comment
 * in Hearth.h for why a drop here is not a hang (the firmware's 1000 ms
 * deadline already covers it, degrading to the ordinary HEARTH_CMD_TIMEOUT
 * path).
 */
void HearthClass::hearthEnqueueCmdResp(uint32_t seq, bool verdict) {
  if (_cmdRespQueueCount >= kHearthCmdRespQueueDepth) {
    return;
  }
  _cmdRespQueue[_cmdRespQueueCount].seq = seq;
  _cmdRespQueue[_cmdRespQueueCount].verdict = verdict;
  _cmdRespQueueCount++;
}

/*
 * Send every queued AT+MTCMDRESP reply, oldest first, through the ordinary
 * _link.command() path -- never self->hearthCommand(), which would run
 * poll() and this same drain again and would let a queued reply's own
 * +MTERR:1 (a harmless stale/already-answered seq, AT_MT_SPEC.md S3.17)
 * clobber the caller's lastError(). Called only from hearthCommand() and
 * poll(), both after their own _link call has returned, i.e. with
 * HearthLink::_busy already false: _link.command() here is therefore a
 * genuine, non-reentrant call that blocks for and consumes its own
 * terminal before the next entry (or the original caller) runs.
 *
 * Re-reads _cmdRespQueueCount on every iteration rather than snapshotting
 * it once: the _link.command() call below can itself dispatch a further
 * +MTCMD if one interleaves with THIS reply's own OK/+MTERR wait, which
 * enqueues another entry via hearthEnqueueCmdResp() above. Looping until
 * the queue is actually empty is what drains that one too, in the same
 * call, rather than leaving it for the next unrelated poll()/hearthCommand().
 */
void HearthClass::hearthDrainCmdRespQueue() {
  while (_cmdRespQueueCount > 0) {
    HearthPendingCmdResp entry = _cmdRespQueue[0];
    for (uint8_t i = 1; i < _cmdRespQueueCount; i++) {
      _cmdRespQueue[i - 1] = _cmdRespQueue[i];
    }
    _cmdRespQueueCount--;

    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+MTCMDRESP=%lu,%d", (unsigned long)entry.seq, entry.verdict ? 1 : 0);
    _link.command(cmd);
  }
}

/*
 * "+MTCMDTO:<seq>" (AT_MT_SPEC.md S3.17): the verdict window closed with no
 * AT+MTCMDRESP from this host, so the firmware default-denied on its own.
 * Nothing on this side was waiting on the seq (hearthDispatchCmd() above
 * never holds one open past its own return), so there is nothing to look
 * up or clean up here -- just tell the sketch, via the same link-event
 * channel HEARTH_TRANSPORT_MISMATCH already uses for a Hearth-specific
 * condition with no upstream matterEvent_t of its own.
 */
void HearthClass::hearthDispatchCmdTimeout(const char *rest, HearthClass *self) {
  (void)rest;
  self->hearthRaiseEvent(HEARTH_CMD_TIMEOUT);
}

/*
 * The URC handler installed on HearthLink. +MTREADY is the one link-level
 * concern: an unexpected one means the co-processor rebooted under us, so
 * every cached endpoint ID is now unconfirmed (HEARTH_COPROCESSOR_REBOOTED).
 * An expected one, i.e. hearthArmExpectedReboot() was called first
 * (ArduinoMatter::begin(), before AT+MTEPAPPLY), clears the arm silently
 * instead: see hearthExpectedRebootSeen().
 *
 * +MTEVT, +MTATTR and +MTIDENT are the Matter-named layer's concern, each
 * routed to its dispatcher above.
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
  if (strncmp(line, "+MTEVT:", 7) == 0) {
    hearthDispatchEvt(line + 7, self);
    return;
  }
  if (strncmp(line, "+MTATTR:", 8) == 0) {
    hearthDispatchAttr(line + 8);
    return;
  }
  if (strncmp(line, "+MTIDENT:", 9) == 0) {
    hearthDispatchIdent(line + 9);
    return;
  }
  /* +MTCMDTO: checked before +MTCMD: even though the two prefixes cannot
   * collide (their 7th characters are ':' and 'T'): keeps the more specific
   * timeout URC visually paired with its own dispatcher first. */
  if (strncmp(line, "+MTCMDTO:", 9) == 0) {
    hearthDispatchCmdTimeout(line + 9, self);
    return;
  }
  if (strncmp(line, "+MTCMD:", 7) == 0) {
    hearthDispatchCmd(line + 7, self);
    return;
  }
}

HearthClass Hearth;

/*
 * ArduinoMatter::_matterEventCB - upstream's own public static member; see
 * Hearth.h's comment on the class for why it stays public rather than
 * gaining a Hearth-side wrapper.
 */
ArduinoMatter::matterEventCB ArduinoMatter::_matterEventCB;

namespace {

/* One declared-or-live endpoint entry, as reported by AT+MTEP?. variant is
 * the optional fourth field (AT_MT_SPEC.md S3.9); it defaults to 0 when the
 * line carries only three fields, exactly matching what a variant-0
 * declaration (the two-arg hearthDeclare()) would report on the wire. */
struct HearthEpEntry {
  uint16_t endpoint_id;
  uint32_t device_type;
  uint8_t variant;
};

struct HearthEpQueryCtx {
  HearthEpEntry entries[HEARTH_MAX_ENDPOINTS];
  uint8_t count;
};

/* "+MTEP:<index>,<endpoint_id>,<device_type>[,<variant>]" per line
 * (AT_MT_SPEC.md S3.9). <index> is the line's own position, already implied
 * by arrival order, so it is parsed only to skip past it. <device_type> is
 * always hex on the wire ("0x%04lX", per the firmware's cmd_mtep()); strtoul's
 * base-16 mode accepts the "0x" prefix directly. The fourth field is present
 * only when the variant is nonzero (the firmware's own byte-identical-output
 * guarantee for existing hosts); its absence means variant 0, not "unknown". */
void hearthOnEpLine(const char *line, void *arg) {
  HearthEpQueryCtx *ctx = (HearthEpQueryCtx *)arg;
  if (strncmp(line, "+MTEP:", 6) != 0 || ctx->count >= HEARTH_MAX_ENDPOINTS) {
    return;
  }
  char *end;
  strtoul(line + 6, &end, 10); /* index field, unused */
  if (*end != ',') {
    return;
  }
  unsigned long ep = strtoul(end + 1, &end, 10);
  if (*end != ',') {
    return;
  }
  unsigned long devtype = strtoul(end + 1, &end, 16);
  unsigned long variant = 0;
  if (*end == ',') {
    variant = strtoul(end + 1, &end, 10);
  }

  ctx->entries[ctx->count].endpoint_id = (uint16_t)ep;
  ctx->entries[ctx->count].device_type = (uint32_t)devtype;
  ctx->entries[ctx->count].variant = (uint8_t)variant;
  ctx->count++;
}

/* "+MTFABRICS:<count>" (AT_MT_SPEC.md S3.4). Gated on ctx.got, the same way
 * hearthQueryCodes()/hearthQueryNet() confirm they actually read a matching
 * line: an OK with no +MTFABRICS: line (a truncated or malformed reply)
 * must not be read as "definitely zero fabrics", or the fail-closed warning
 * in ArduinoMatter::begin() below never fires for it. */
struct HearthFabricsCtx {
  long count;
  bool got;
};

void hearthOnFabricsLine(const char *line, void *arg) {
  HearthFabricsCtx *ctx = (HearthFabricsCtx *)arg;
  if (strncmp(line, "+MTFABRICS:", 11) == 0) {
    ctx->count = atol(line + 11);
    ctx->got = true;
  }
}

bool hearthQueryFabricCount(long *out) {
  HearthFabricsCtx ctx;
  ctx.count = 0;
  ctx.got = false;
  *out = 0;
  if (Hearth.hearthCommand("AT+MTFABRICS?", hearthOnFabricsLine, &ctx) != 0 || !ctx.got) {
    return false;
  }
  *out = ctx.count;
  return true;
}

/* "+MTCODES:<qr_payload>,<manual_pairing_code>" (AT_MT_SPEC.md S3.6). Fixed
 * buffers, not String concatenation: real Arduino's String has no operator+
 * guaranteed compatible with this port's minimal host stub (see
 * HearthClass::firmwareVersion() for the same choice). HEARTH_LINE_MAX
 * (HearthLink.h) already documents the QR payload as the longest field on
 * the wire, so it is reused here rather than inventing a second constant. */
struct HearthCodesCtx {
  char qr[HEARTH_LINE_MAX];
  char code[16];
  bool got;
};

void hearthOnCodesLine(const char *line, void *arg) {
  HearthCodesCtx *ctx = (HearthCodesCtx *)arg;
  if (strncmp(line, "+MTCODES:", 9) != 0) {
    return;
  }
  const char *p = line + 9;
  const char *comma = strchr(p, ',');
  if (!comma) {
    return;
  }
  size_t qrlen = (size_t)(comma - p);
  if (qrlen >= sizeof(ctx->qr)) {
    qrlen = sizeof(ctx->qr) - 1;
  }
  memcpy(ctx->qr, p, qrlen);
  ctx->qr[qrlen] = '\0';

  strncpy(ctx->code, comma + 1, sizeof(ctx->code) - 1);
  ctx->code[sizeof(ctx->code) - 1] = '\0';
  ctx->got = true;
}

bool hearthQueryCodes(HearthCodesCtx *ctx) {
  ctx->got = false;
  return Hearth.hearthCommand("AT+MTCODES?", hearthOnCodesLine, ctx) == 0 && ctx->got;
}

/* "+MTNET:<transport>,<enabled>,<connected>[,<mismatch>]" (AT_MT_SPEC.md
 * S3.12). One line per query: one transport is active per BOOT. On the
 * single-stack images that choice is fixed at build time; on the combined
 * image it follows the persisted AT+MTTRANSPORT setting. The fourth field
 * (0.2.0 firmware) is the transport-mismatch flag of S3.12.1; older
 * firmware sends three fields and the flag defaults to 0. */
struct HearthNetCtx {
  char transport[8];
  int enabled;
  int connected;
  int mismatch;
  bool got;
};

void hearthOnNetLine(const char *line, void *arg) {
  HearthNetCtx *ctx = (HearthNetCtx *)arg;
  if (strncmp(line, "+MTNET:", 7) != 0) {
    return;
  }
  const char *p = line + 7;
  const char *c1 = strchr(p, ',');
  if (!c1) {
    return;
  }
  size_t len = (size_t)(c1 - p);
  if (len >= sizeof(ctx->transport)) {
    len = sizeof(ctx->transport) - 1;
  }
  memcpy(ctx->transport, p, len);
  ctx->transport[len] = '\0';

  char *end;
  ctx->enabled = (int)strtol(c1 + 1, &end, 10);
  if (*end != ',') {
    return;
  }
  ctx->connected = (int)strtol(end + 1, &end, 10);
  ctx->mismatch = (*end == ',') ? (int)strtol(end + 1, nullptr, 10) : 0;
  ctx->got = true;
}

bool hearthQueryNet(HearthNetCtx *ctx) {
  ctx->got = false;
  return Hearth.hearthCommand("AT+MTNET?", hearthOnNetLine, ctx) == 0 && ctx->got;
}

/*
 * Shared bail-out for every failure path in ArduinoMatter::begin() below:
 * HEARTH_PROTOCOL_ERROR, and the caller returns immediately afterwards with
 * every endpoint ID still at 0.
 *
 * `rc` is the hearthCommand() return that triggered the abort, or -2 for
 * the two paths with no single command to blame (the AT+MTEPAPPLY wait
 * timing out; the retry cap exhausted on a composition that never
 * converged). hearthCommand() already recorded the real code in
 * lastError() when rc is a positive +MTERR value: overwriting it here with
 * a generic -2 would throw away exactly the information the +MTERR
 * grammar exists to carry (S5, "you asked the wrong way" vs "that device
 * type does not exist"). -2 is set only when rc itself is -2 (a genuine
 * host-side timeout, which hearthCommand() leaves lastError() at 0 for);
 * a bare ERROR (-1) is left at whatever hearthCommand() already set
 * (also 0, since a bare-ERROR "wrong command form" carries no code to
 * preserve).
 */
void hearthAbortReconcile(int rc) {
  if (rc == -2) {
    Hearth.hearthSetError(-2);
  }
  /* Latch the failure for the rest of the boot. See
   * HearthClass::hearthReconcileFailed(): the retry cap below bounds one
   * call, this bounds the loop() the sketch wraps that call in. */
  Hearth.hearthSetReconcileFailed();
  Hearth.hearthReportProtocolError();
}

/*
 * Ceiling on ArduinoMatter::begin()'s query/apply loop: one query that may
 * discover a mismatch and apply, one re-query to confirm it took. A
 * composition that still does not match after that is not going to start
 * matching by looping again with the same inputs; something on the wire is
 * rejecting a write (unknown device type, the firmware's 16-endpoint cap,
 * ...), and every command in the path below is checked for exactly that
 * reason. Retrying anyway would mean unbounded AT+MTEPCLEAR / AT+MTEPAPPLY
 * cycles, i.e. unbounded NVS writes, on a state that cannot resolve itself.
 */
static const int kHearthMaxReconcileAttempts = 2;

/* "WIFI" / "THREAD" to the enum; wire names are upper-case exact. */
bool hearthTransportFromName(const char *s, size_t len, HearthTransport *out) {
  if (len == 4 && strncmp(s, "WIFI", 4) == 0) {
    *out = HEARTH_TRANSPORT_WIFI;
    return true;
  }
  if (len == 6 && strncmp(s, "THREAD", 6) == 0) {
    *out = HEARTH_TRANSPORT_THREAD;
    return true;
  }
  return false;
}

struct HearthTransportCtx {
  HearthTransport active;
  HearthTransport stored;
  bool got;
};

void hearthOnTransportLine(const char *line, void *arg) {
  HearthTransportCtx *ctx = (HearthTransportCtx *)arg;
  if (strncmp(line, "+MTTRANSPORT:", 13) != 0) {
    return;
  }
  const char *p = line + 13;
  const char *c1 = strchr(p, ',');
  if (!c1) {
    return;
  }
  HearthTransport a, s;
  if (!hearthTransportFromName(p, (size_t)(c1 - p), &a)) {
    return;
  }
  if (!hearthTransportFromName(c1 + 1, strlen(c1 + 1), &s)) {
    return;
  }
  ctx->active = a;
  ctx->stored = s;
  ctx->got = true;
}

}  // namespace

/* The S3.12.1 transport-mismatch flag from a live AT+MTNET? round-trip:
 * true when the device holds a fabric but its active transport is not
 * provisioned. Always a fresh query, like every other network predicate
 * in this library. Older firmware never reports it, so this is false
 * there. */
bool HearthClass::transportMismatch() {
  HearthNetCtx ctx;
  return hearthQueryNet(&ctx) && ctx.mismatch == 1;
}

bool HearthClass::setTransport(HearthTransport t) {
  const char *name = (t == HEARTH_TRANSPORT_THREAD) ? "THREAD" : "WIFI";
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+MTTRANSPORT=%s", name);
  return hearthCommand(cmd) == 0;
}

bool HearthClass::transport(HearthTransport *active, HearthTransport *stored) {
  HearthTransportCtx ctx;
  ctx.got = false;
  if (hearthCommand("AT+MTTRANSPORT?", hearthOnTransportLine, &ctx) != 0 || !ctx.got) {
    return false;
  }
  *active = ctx.active;
  *stored = ctx.stored;
  return true;
}

/*
 * ArduinoMatter::begin() - design spec S5.4:
 *
 *   1. AT+MTEP?, collecting (endpoint_id, devtype) pairs in order.
 *   2. Compare that devtype sequence, in order, against the sketch's
 *      hearthDeclaredTypeAt(0..n). Order is part of the composition: two
 *      compositions with the same device types in a different sequence are
 *      different, because endpoint IDs are assigned from declaration order
 *      and their stability across boots is what persisted attribute values
 *      and the controller's cached data model are keyed on (spec S5.3).
 *   3. Identical: adopt the reported endpoint_id onto each declared
 *      endpoint and return. One query, zero writes: this is every boot
 *      after the first, where the sketch's declaration already matches
 *      what the C6 persisted.
 *   4. Different: AT+MTFABRICS?; a non-zero (or unknown) count means
 *      applying might invalidate a live fabric's caches, so warn on Serial
 *      and record it (Hearth.warnedAboutRecommission(), since Matter-named
 *      classes may not gain new members even for this). Then AT+MTEPCLEAR,
 *      one AT+MTEP=0x%04lX per declared endpoint in order, AT+MTEPAPPLY,
 *      wait up to 15000 ms for +MTREADY, and re-query (continue at step 3).
 *
 * Bounded to kHearthMaxReconcileAttempts query rounds (see its comment) and
 * every command's return code is checked; a failure anywhere in the apply
 * sequence aborts immediately rather than continuing with a composition
 * that is now known to be wrong on the wire.
 *
 * The expected-reboot arm (Hearth.hearthArmExpectedReboot()) is taken
 * immediately before AT+MTEPAPPLY only, and nowhere else. Arming any
 * earlier, e.g. before the query that decides whether an apply is even
 * needed, would let a +MTREADY consumed during that query (or during
 * AT+MTFABRICS?/AT+MTEPCLEAR/an AT+MTEP= write) satisfy
 * hearthExpectedRebootSeen() before the apply's own reboot has actually
 * happened, so the wait below would return immediately and re-query a
 * co-processor still mid-reboot. Narrowing the arm to bracket only the
 * apply is what makes hearthExpectedRebootSeen() actually mean "the reboot
 * this AT+MTEPAPPLY triggered has completed."
 */
void ArduinoMatter::begin() {
  /*
   * A reconcile that already failed this boot is not attempted again. The
   * retry cap below is per call; this is per boot, and the two are not the
   * same bound because a sketch may call begin() from loop(). Without it, a
   * composition the C6 rejects (unknown device type, past its 16-endpoint
   * cap) runs AT+MTEPCLEAR, the writes, AT+MTEPAPPLY and a co-processor
   * reboot on every iteration, forever. Nothing about the inputs changes
   * between those iterations, so a retry can only repeat the NVS wear and
   * keep the C6 permanently rebooting. Silent rather than re-raising
   * HEARTH_PROTOCOL_ERROR: the event fired once, on the attempt that
   * actually failed, and repeating it every iteration would bury it.
   */
  if (Hearth.hearthReconcileFailed()) {
    return;
  }
  for (int attempt = 0; attempt < kHearthMaxReconcileAttempts; attempt++) {
    HearthEpQueryCtx ctx;
    ctx.count = 0;
    int queryRc = Hearth.hearthCommand("AT+MTEP?", hearthOnEpLine, &ctx);
    if (queryRc != 0) {
      hearthAbortReconcile(queryRc);
      return;
    }

    uint8_t declaredCount = MatterEndPoint::hearthDeclaredCount();
    bool identical = (ctx.count == declaredCount);
    for (uint8_t i = 0; identical && i < declaredCount; i++) {
      if (ctx.entries[i].device_type != MatterEndPoint::hearthDeclaredTypeAt(i)
          || ctx.entries[i].variant != MatterEndPoint::hearthDeclaredVariantAt(i)) {
        identical = false;
      }
    }

    if (identical) {
      for (uint8_t i = 0; i < declaredCount; i++) {
        MatterEndPoint *ep = MatterEndPoint::hearthDeclaredAt(i);
        ep->setEndPointId(ctx.entries[i].endpoint_id);
        /* Resend any state the C6 does not persist across a reboot (e.g.
         * TemperatureLevel labels); see hearthOnReconciled()'s own comment.
         * Runs on every reconcile, not only the first, since this branch is
         * also where a sketch's repeated Matter.begin() call (the README
         * documents calling it from loop()) lands once the composition is
         * already steady. */
        ep->hearthOnReconciled();
      }
      MatterEndPoint::hearthMarkReconciled();
      return;
    }

    /* Still mismatched on the last permitted round: do not apply again,
     * fall through to the shared abort below rather than looping forever. */
    if (attempt == kHearthMaxReconcileAttempts - 1) {
      break;
    }

    long fabrics = 0;
    bool fabricsKnown = hearthQueryFabricCount(&fabrics);
    if (!fabricsKnown || fabrics != 0) {
      /* Fail closed: a link hiccup here must not read as "definitely zero
       * fabrics, no warning needed" on what may be a live commissioned
       * device. Warn in both cases; only the message differs. */
#ifdef ARDUINO
      if (!fabricsKnown) {
        Serial.println(
          "Hearth: could not confirm the fabric count before changing the endpoint "
          "composition; warning as a precaution in case the device is commissioned."
        );
      } else {
        Serial.println(
          "Hearth: endpoint composition is changing on a device with an active fabric; "
          "the commissioned controller's cached data model may need re-pairing to see it."
        );
      }
#endif
      Hearth.hearthSetWarnedAboutRecommission();
    }

    int clearRc = Hearth.hearthCommand("AT+MTEPCLEAR");
    if (clearRc != 0) {
      hearthAbortReconcile(clearRc);
      return;
    }
    int writeRc = 0;
    for (uint8_t i = 0; i < declaredCount; i++) {
      char cmd[24];
      uint8_t variant = MatterEndPoint::hearthDeclaredVariantAt(i);
      if (variant != 0) {
        /* AT_MT_SPEC.md S3.9: the variant field is present only when
         * nonzero, so a variant-0 declaration keeps sending the exact
         * command every existing host and firmware revision already
         * understands. */
        snprintf(cmd, sizeof(cmd), "AT+MTEP=0x%04lX,%u", (unsigned long)MatterEndPoint::hearthDeclaredTypeAt(i), (unsigned)variant);
      } else {
        snprintf(cmd, sizeof(cmd), "AT+MTEP=0x%04lX", (unsigned long)MatterEndPoint::hearthDeclaredTypeAt(i));
      }
      writeRc = Hearth.hearthCommand(cmd);
      if (writeRc != 0) {
        break;
      }
    }
    if (writeRc != 0) {
      hearthAbortReconcile(writeRc);
      return;
    }

    Hearth.hearthArmExpectedReboot();
    int applyRc = Hearth.hearthCommand("AT+MTEPAPPLY");
    if (applyRc != 0) {
      Hearth.hearthDisarmExpectedReboot();
      hearthAbortReconcile(applyRc);
      return;
    }

    uint32_t start = millis();
    while (!Hearth.hearthExpectedRebootSeen()) {
      Hearth.poll();
      if (millis() - start > 15000) {
        /* No +MTREADY arrived: the apply may have wedged the co-processor,
         * or it rebooted without the AT link coming back up in time. Do not
         * leave the arm live (Task 4's report: a stale arm swallows the
         * next, unrelated spontaneous reboot), and leave every endpoint ID
         * at 0 rather than guessing: 0 is the Root Node, which the firmware
         * deliberately never reports over +MTATTR (S4), so an attribute
         * write against it fails loudly instead of silently doing nothing
         * useful. No wire code applies here (AT+MTEPAPPLY itself returned
         * OK; it is the promised +MTREADY that never showed), so -2 is the
         * genuine "nothing usable came back" case, not a discarded real
         * code. */
        Hearth.hearthDisarmExpectedReboot();
        hearthAbortReconcile(-2);
        return;
      }
      yield();
    }
    /* Loop back to step 1: re-query and confirm the apply took. */
  }
  /* Exhausted kHearthMaxReconcileAttempts rounds without ever reaching the
   * identical case: something on the wire keeps rejecting the composition
   * (unknown device type, the endpoint cap, ...) rather than time or luck
   * fixing it. No single command failed here either (the last AT+MTEP?
   * returned OK, just with content that still did not match), so -2 again. */
  hearthAbortReconcile(-2);
}

String ArduinoMatter::getManualPairingCode() {
  HearthCodesCtx ctx;
  if (!hearthQueryCodes(&ctx)) {
    return String("");
  }
  return String(ctx.code);
}

String ArduinoMatter::getOnboardingQRCodeUrl() {
  HearthCodesCtx ctx;
  if (!hearthQueryCodes(&ctx)) {
    return String("");
  }
  char url[HEARTH_LINE_MAX + 64];
  snprintf(url, sizeof(url), "https://project-chip.github.io/connectedhomeip/qrcode.html?data=%s", ctx.qr);
  return String(url);
}

bool ArduinoMatter::isWiFiStationEnabled() {
  HearthNetCtx ctx;
  return hearthQueryNet(&ctx) && strcmp(ctx.transport, "WIFI") == 0 && ctx.enabled == 1;
}

/* The C6 image runs no SoftAP: WiFi commissioning is station-mode plus BLE
 * pairing, never an access point the phone joins directly. */
bool ArduinoMatter::isWiFiAccessPointEnabled() {
  return false;
}

bool ArduinoMatter::isThreadEnabled() {
  HearthNetCtx ctx;
  return hearthQueryNet(&ctx) && strcmp(ctx.transport, "THREAD") == 0 && ctx.enabled == 1;
}

/* BLE commissioning is how every image commissions; there is no AT+MT query
 * for this because it is never anything but true. */
bool ArduinoMatter::isBLECommissioningEnabled() {
  return true;
}

bool ArduinoMatter::isDeviceCommissioned() {
  long fabrics = 0;
  return hearthQueryFabricCount(&fabrics) && fabrics > 0;
}

bool ArduinoMatter::isWiFiConnected() {
  HearthNetCtx ctx;
  return hearthQueryNet(&ctx) && strcmp(ctx.transport, "WIFI") == 0 && ctx.connected == 1;
}

bool ArduinoMatter::isThreadConnected() {
  HearthNetCtx ctx;
  return hearthQueryNet(&ctx) && strcmp(ctx.transport, "THREAD") == 0 && ctx.connected == 1;
}

bool ArduinoMatter::isDeviceConnected() {
  HearthNetCtx ctx;
  return hearthQueryNet(&ctx) && ctx.connected == 1;
}

/*
 * Remove the device from its fabric. AT+MTRESET is the firmware's Matter
 * reset (AT_MT_SPEC.md S3.10): it erases the fabrics, credentials and
 * attribute persistence, then reboots. That erasure IS the mechanism
 * here, not a side effect of rebooting. The endpoint composition and,
 * on the combined image, the stored transport selection survive. Note
 * that on the combined image network credentials are erased only for
 * the ACTIVE transport; a dormant transport's credentials survive.
 */
void ArduinoMatter::decommission() {
  Hearth.hearthCommand("AT+MTRESET");
}

ArduinoMatter Matter;
