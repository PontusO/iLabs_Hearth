/*
 * Hearth.cpp - the Hearth global and ArduinoMatter/Matter, implementation.
 */
#include "Hearth.h"
#include "MatterEndPoint.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

HearthClass::HearthClass()
  : _lastError(0),
    _warnedAboutRecommission(false),
    _expectingReboot(false),
    _expectedRebootSeen(false),
    _expectedRebootArmedAt(0),
    _expectedRebootTimeoutMs(0) {}

void HearthClass::begin(Stream &serial, unsigned long baud) {
  (void)baud;  // see the header: a caller-supplied Stream has no begin() of its own to call with it;
               // only the HEARTH_DEFAULT_SERIAL fallback in hearthEnsureLink() actually uses a baud.
  _link.begin(serial);
  _link.onURC(hearthOnURCLine, this);
  _expectingReboot = false;
  _expectedRebootSeen = false;
  _expectedRebootArmedAt = 0;
  _expectedRebootTimeoutMs = 0;
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
  /* Same ordering reason as poll(): a buffered +MTREADY that arrives on the
   * wire ahead of this command's own response lines is read and dispatched
   * by _link.command()'s read loop before it returns, so the expiry check
   * must run after, not before, or a reply that was already sitting there
   * could be misclassified the same way described in poll() above. */
  int rc = _link.command(cmd, onLine, arg);
  hearthCheckExpectedRebootExpiry();
  _lastError = (rc > 0) ? rc : 0;
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

/* Parses "<bit>[,<detail>]" (the text after "+MTEVT:") and, if a sketch has
 * registered one, calls ArduinoMatter's event callback. Out-of-table bits
 * (27-31, reserved per S3.11, or outright malformed input) are dropped
 * silently, the same policy the brief gives for an unrecognised endpoint in
 * hearthDispatchAttr()/hearthDispatchIdent() below. */
void hearthDispatchEvt(const char *rest) {
  char *end;
  long bit = strtol(rest, &end, 10);
  if (end == rest || bit < 0 || bit >= 27) {
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
 * Parses "<ep>,<cl>,<attr>,<val>" (the text after "+MTATTR:") and routes it
 * to that endpoint's attributeChangeCB, if the endpoint is one the sketch
 * declared. The wire carries a bare integer with no type tag (MatterEndPoint.h),
 * and at this generic dispatch point there is no per-attribute type
 * knowledge to reconstruct one from, unlike the synchronous
 * getAttributeVal() path where the *caller* already knows what type it
 * asked for. The value is therefore rebuilt as ESP_MATTER_VAL_TYPE_INTEGER,
 * .val.i holding the raw signed wire value: Tasks 6-8's concrete endpoint
 * types must read val.i directly (ignoring .type, which is not the
 * attribute's real type) and reinterpret it according to their own known
 * attribute definition, exactly as their setAttributeVal/updateAttributeVal
 * callers already must for the type they pass in.
 *
 * An ep with no registered endpoint (the root endpoint, or one the sketch
 * never declared) is dropped silently: both are legitimately not ours, per
 * AT_MT_SPEC.md S4's own note that the root endpoint is intentionally never
 * reported.
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
  esp_matter_attr_val_t val = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_INTEGER, value);
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
    hearthDispatchEvt(line + 7);
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
}

HearthClass Hearth;

/*
 * ArduinoMatter::_matterEventCB - upstream's own public static member; see
 * Hearth.h's comment on the class for why it stays public rather than
 * gaining a Hearth-side wrapper.
 */
ArduinoMatter::matterEventCB ArduinoMatter::_matterEventCB;

namespace {

/* One declared-or-live endpoint entry, as reported by AT+MTEP?. */
struct HearthEpEntry {
  uint16_t endpoint_id;
  uint32_t device_type;
};

struct HearthEpQueryCtx {
  HearthEpEntry entries[HEARTH_MAX_ENDPOINTS];
  uint8_t count;
};

/* "+MTEP:<index>,<endpoint_id>,<device_type>" per line (AT_MT_SPEC.md S3.9).
 * <index> is the line's own position, already implied by arrival order, so
 * it is parsed only to skip past it. <device_type> is always hex on the
 * wire ("0x%04lX", per the firmware's cmd_mtep()); strtoul's base-16 mode
 * accepts the "0x" prefix directly. */
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

  ctx->entries[ctx->count].endpoint_id = (uint16_t)ep;
  ctx->entries[ctx->count].device_type = (uint32_t)devtype;
  ctx->count++;
}

/* "+MTFABRICS:<count>" (AT_MT_SPEC.md S3.4). */
void hearthOnFabricsLine(const char *line, void *arg) {
  long *out = (long *)arg;
  if (strncmp(line, "+MTFABRICS:", 11) == 0) {
    *out = atol(line + 11);
  }
}

bool hearthQueryFabricCount(long *out) {
  *out = 0;
  return Hearth.hearthCommand("AT+MTFABRICS?", hearthOnFabricsLine, out) == 0;
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

/* "+MTNET:<transport>,<enabled>,<connected>" (AT_MT_SPEC.md S3.12). One
 * line: transport is fixed at build time, so there is never a WIFI line and
 * a THREAD line to choose between. */
struct HearthNetCtx {
  char transport[8];
  int enabled;
  int connected;
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
  ctx->connected = (int)strtol(end + 1, nullptr, 10);
  ctx->got = true;
}

bool hearthQueryNet(HearthNetCtx *ctx) {
  ctx->got = false;
  return Hearth.hearthCommand("AT+MTNET?", hearthOnNetLine, ctx) == 0 && ctx->got;
}

}  // namespace

/*
 * ArduinoMatter::begin() - design spec S5.4, implemented exactly:
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
 *   4. Different: AT+MTFABRICS?; a non-zero count means applying will
 *      invalidate a live fabric's caches, so warn on Serial and record it
 *      (Hearth.warnedAboutRecommission(), since Matter-named classes may
 *      not gain new members even for this). Then AT+MTEPCLEAR, one
 *      AT+MTEP=0x%04lX per declared endpoint in order, AT+MTEPAPPLY, wait
 *      up to 15000 ms for +MTREADY, and re-query (continue at step 3).
 *
 * The expected-reboot arm (Hearth.hearthArmExpectedReboot()) is taken
 * before *every* AT+MTEP? in the loop, including the very first one, not
 * only around AT+MTEPAPPLY. begin() is itself the host's resynchronization
 * point: a +MTREADY arriving anywhere during it, including one already
 * sitting unread because the co-processor rebooted moments before begin()
 * was even called, reflects normal resync rather than trouble, so it must
 * not raise HEARTH_COPROCESSOR_REBOOTED. Only a +MTREADY arriving after a
 * successful, disarmed reconcile is genuinely unexpected. Disarming
 * happens the moment step 3 (identical) is reached, closing that window as
 * soon as reconcile is done rather than leaving it open for the arm's own
 * timeout.
 */
void ArduinoMatter::begin() {
  for (;;) {
    Hearth.hearthArmExpectedReboot();

    HearthEpQueryCtx ctx;
    ctx.count = 0;
    Hearth.hearthCommand("AT+MTEP?", hearthOnEpLine, &ctx);

    uint8_t declaredCount = MatterEndPoint::hearthDeclaredCount();
    bool identical = (ctx.count == declaredCount);
    for (uint8_t i = 0; identical && i < declaredCount; i++) {
      if (ctx.entries[i].device_type != MatterEndPoint::hearthDeclaredTypeAt(i)) {
        identical = false;
      }
    }

    if (identical) {
      Hearth.hearthDisarmExpectedReboot();
      for (uint8_t i = 0; i < declaredCount; i++) {
        MatterEndPoint::hearthDeclaredAt(i)->setEndPointId(ctx.entries[i].endpoint_id);
      }
      MatterEndPoint::hearthMarkReconciled();
      return;
    }

    long fabrics = 0;
    hearthQueryFabricCount(&fabrics);
    if (fabrics != 0) {
#ifdef ARDUINO
      Serial.println(
        "Hearth: endpoint composition is changing on a device with an active fabric; "
        "the commissioned controller's cached data model may need re-pairing to see it."
      );
#endif
      Hearth.hearthSetWarnedAboutRecommission();
    }

    Hearth.hearthCommand("AT+MTEPCLEAR");
    for (uint8_t i = 0; i < declaredCount; i++) {
      char cmd[24];
      snprintf(cmd, sizeof(cmd), "AT+MTEP=0x%04lX", (unsigned long)MatterEndPoint::hearthDeclaredTypeAt(i));
      Hearth.hearthCommand(cmd);
    }
    Hearth.hearthCommand("AT+MTEPAPPLY");

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
         * useful. */
        Hearth.hearthDisarmExpectedReboot();
        Hearth.hearthSetError(-2); /* HearthLink's own "timeout" sentinel */
        Hearth.hearthRaiseEvent(HEARTH_PROTOCOL_ERROR);
        return;
      }
      yield();
    }
    /* Loop back to step 1: re-query and confirm the apply took. */
  }
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

void ArduinoMatter::decommission() {
  Hearth.hearthCommand("AT+MTRESET");
}

ArduinoMatter Matter;
