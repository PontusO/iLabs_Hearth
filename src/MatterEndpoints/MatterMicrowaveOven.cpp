/*
 * MatterMicrowaveOven.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the design rationale,
 * and the deferral-chain explanation for hearthOnForwardedCommandFields().
 */
#include "MatterEndpoints/MatterMicrowaveOven.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <string.h>

namespace {
/* microwave_oven (ESP_MATTER_MICROWAVE_OVEN_DEVICE_TYPE_ID),
 * MicrowaveOvenMode::Id, MicrowaveOvenControl::Id, and its two command ids.
 * See MatterMicrowaveOven.h's header comment for the quoted lines from the
 * pinned esp-matter checkout's generated headers. Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kMicrowaveOvenDeviceType = 0x0079;
const uint32_t kMicrowaveOvenModeClusterId = 0x005E;      // 94 decimal
const uint32_t kMicrowaveOvenControlClusterId = 0x005F;   // 95 decimal
const uint32_t kSetCookingParametersCommandId = 0x0000;
const uint32_t kAddMoreTimeCommandId = 0x0001;
}  // namespace

MatterMicrowaveOven::MatterMicrowaveOven() {}

MatterMicrowaveOven::~MatterMicrowaveOven() {
  end();
}

bool MatterMicrowaveOven::begin() {
  if (!hearthBeginOperationalState(kMicrowaveOvenDeviceType)) {
    return false;
  }
  modesCount = 0;
  return true;
}

/*
 * Builds and sends "AT+MTMODES=<ep>,94,<mode1>,<tag1>,"<label1>",..."
 * (AT_MT_SPEC.md S3.20.1) for exactly the triples given. Wire-only: the
 * caller decides whether/what to commit to the cache afterwards (house
 * discipline: a failed write must not update it).
 */
bool MatterMicrowaveOven::hearthSendModes(const uint8_t *modes_, const uint16_t *tags_, const char *const *labels_, uint8_t count) {
  char cmd[500];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u,%lu", (unsigned)getEndPointId(), (unsigned long)kMicrowaveOvenModeClusterId);
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,%u,\"%s\"", (unsigned)modes_[i], (unsigned)tags_[i], labels_[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Host-side grammar enforcement (S3.20.1) plus the wire send and cache
 * commit. See MatterRoboticVacuum::hearthSetModeList() for the identical
 * discipline this mirrors, narrowed to the one list this class actually
 * has: count bounds, mode uniqueness within THIS call's list, and
 * per-label length/printability/quote-exclusion. <tag>'s 0..0xFFFF range is
 * already guaranteed by its uint16_t parameter type, nothing extra to
 * check. Every violation reports Hearth.hearthSetError(1) without ever
 * reaching the wire.
 */
bool MatterMicrowaveOven::setSupportedModes(const uint8_t *modes_, const uint16_t *tags_, const char *const *labels_, uint8_t count) {
  if (!started) {
    return false;
  }
  if (modes_ == nullptr || tags_ == nullptr || labels_ == nullptr || count == 0 || count > kMaxModes) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
      if (modes_[i] == modes_[j]) {
        Hearth.hearthSetError(1);
        return false;
      }
    }
    if (labels_[i] == nullptr) {
      Hearth.hearthSetError(1);
      return false;
    }
    size_t len = strlen(labels_[i]);
    if (len == 0 || len > kMaxLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* AT_MT_SPEC.md S3.20.1's own grammar, checked host-side before the
     * wire would have to: every byte printable ASCII (0x20..0x7E), and
     * never a '"'. A comma is deliberately NOT rejected here: S3.20.1
     * states plainly that a comma inside a quoted label is legal and part
     * of its text. */
    for (const char *p = labels_[i]; *p != '\0'; p++) {
      unsigned char ch = (unsigned char)*p;
      if (ch < 0x20 || ch > 0x7E || ch == '"') {
        Hearth.hearthSetError(1);
        return false;
      }
    }
  }
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  if (!hearthSendModes(modes_, tags_, labels_, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(modes, modes_, count * sizeof(uint8_t));
  memcpy(tags, tags_, count * sizeof(uint16_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(labels[i], labels_[i], kMaxLabelLen);
    labels[i][kMaxLabelLen] = '\0';
  }
  modesCount = count;
  return true;
}

void MatterMicrowaveOven::onCookingParameters(std::function<bool(const HearthCookingParams &)> cb) {
  _onCookingParametersCB = cb;
}

void MatterMicrowaveOven::onAddMoreTime(std::function<bool(uint32_t)> cb) {
  _onAddMoreTimeCB = cb;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first: resends the cached SupportedModes list, which the
 * firmware does not persist across a reboot (S3.20.1), the same shape
 * MatterRoboticVacuum's own hearthOnReconciled() establishes for its two
 * lists. A no-op if nothing has been set yet.
 */
void MatterMicrowaveOven::hearthOnReconciled() {
  if (!started || modesCount == 0) {
    return;
  }
  const char *labelPtrs[kMaxModes];
  for (uint8_t i = 0; i < modesCount; i++) {
    labelPtrs[i] = labels[i];
  }
  hearthSendModes(modes, tags, labelPtrs, modesCount);
}

/*
 * AT_MT_SPEC.md S3.17: the firmware forwards a controller-invoked
 * SetCookingParameters/AddMoreTime (MicrowaveOvenControl) here for a
 * verdict. SetCookingParameters' four fields arrive in order
 * cookMode,cookTime,power,startAfter (fields.value[0..3]), each with its
 * own present[] flag honestly carried into HearthCookingParams -- this
 * firmware's own server resolves all four before the delegate callback
 * runs (S3.17: "on this firmware's actual wire traffic none of the four
 * forwarded fields is ever empty in practice"), but the wire grammar itself
 * permits a sparse line, so the has-flags are populated from fields.present[]
 * rather than assumed true. startAfterSetting defaults false when the field
 * is absent, matching the server's own default for an omitted invoke field.
 * AddMoreTime's single field is the server-computed ABSOLUTE
 * finalCookTimeSec (fields.value[0]), 0 if somehow absent.
 *
 * Everything else -- an unstarted endpoint, the wrong cluster, or an
 * unrecognised command id on this cluster -- defers to
 * MatterOperationalStateEndpoint::hearthOnForwardedCommandFields(): that
 * class does not itself override this virtual (only the legacy
 * four-argument hearthOnForwardedCommand()), so the qualified call below
 * statically binds to MatterEndPoint's own default body, which in turn
 * makes a virtual call to hearthOnForwardedCommand() -- a call this class
 * does not override, so it reaches MatterOperationalStateEndpoint's own
 * override and, through it, cluster 0x0060's Pause/Stop/Start/Resume
 * dispatch unchanged. See the header comment and the test file's regression
 * proof for this chain.
 */
bool MatterMicrowaveOven::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (started && cluster_id == kMicrowaveOvenControlClusterId) {
    if (command_id == kSetCookingParametersCommandId) {
      HearthCookingParams params;
      params.hasCookMode = fields.count > 0 && fields.present[0];
      params.cookMode = params.hasCookMode ? (uint8_t)fields.value[0] : 0;
      params.hasCookTime = fields.count > 1 && fields.present[1];
      params.cookTimeSec = params.hasCookTime ? fields.value[1] : 0;
      params.hasPower = fields.count > 2 && fields.present[2];
      params.powerPercent = params.hasPower ? (uint8_t)fields.value[2] : 0;
      params.startAfterSetting = (fields.count > 3 && fields.present[3]) ? (fields.value[3] != 0) : false;
      return _onCookingParametersCB ? _onCookingParametersCB(params) : false;
    }
    if (command_id == kAddMoreTimeCommandId) {
      uint32_t finalCookTimeSec = (fields.count > 0 && fields.present[0]) ? fields.value[0] : 0;
      return _onAddMoreTimeCB ? _onAddMoreTimeCB(finalCookTimeSec) : false;
    }
  }
  return MatterOperationalStateEndpoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}
