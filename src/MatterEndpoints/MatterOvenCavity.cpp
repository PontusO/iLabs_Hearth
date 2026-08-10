/*
 * MatterOvenCavity.cpp - implementation. See the header for the design
 * rationale (subclassing the cabinet for the temperature machinery, the
 * owned-only begin gates, the OvenMode/OvenCavityOperationalState surface
 * and why the inherited cluster-82 path is skipped, the host-side {0,1,2}
 * state bound).
 */
#include "MatterEndpoints/MatterOvenCavity.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <string.h>

namespace {
/* OvenMode::Id (0x00000049, connectedhomeip's zap-generated
 * clusters/OvenMode/ClusterId.h, "cluster code: 73/0x49") and
 * OvenCavityOperationalState::Id (0x00000048,
 * clusters/OvenCavityOperationalState/ClusterId.h, "cluster code: 72/0x48"),
 * both quoted in AT_MT_SPEC.md S3.9's 0x0071 conditional-cluster table and
 * S3.17/S3.20.1/S3.21. ChangeToMode is ModeBase's command 0x0000, the same
 * id every ModeBase derivation shares; Stop is 0x01 and Start is 0x02
 * exactly as on the base OperationalState cluster, while Pause (0x00) and
 * Resume (0x03) are disallowConform on this cluster
 * (OperationalState_Oven.xml revision 2) and never forwarded. Given as
 * plain integers: there is no connectedhomeip header on a host build to
 * pull the named constants from. */
const uint32_t kOvenModeClusterId = 0x0049;             // 73 decimal
const uint32_t kOvenCavityOpStateClusterId = 0x0048;    // 72 decimal
const uint32_t kChangeToModeCommandId = 0x0000;
const uint32_t kStopCommandId = 0x0001;
const uint32_t kStartCommandId = 0x0002;

/* AT_MT_SPEC.md S3.21: the base OperationalStateEnum values this cluster
 * accepts, and the ONLY ones (the cluster defines no derived-number-space
 * states, so 0x40-0x42 are +MTERR:1 on a cavity endpoint). 3 (Error) is
 * reserved for the device-fault path on every cluster in the family. The
 * host-side bound below is this closed set's upper member. */
const uint8_t kMaxOperationalState = 2;  // 0 Stopped, 1 Running, 2 Paused
}  // namespace

MatterOvenCavity::MatterOvenCavity() {}

MatterOvenCavity::~MatterOvenCavity() {
  /* The base class chain (cabinet end(), MatterEndPoint's undeclare) does
   * the real teardown; only this class's own additions reset here. */
  ovenModesCount = 0;
  cavityOperationalState = 0;
}

/*
 * Both begin() overloads: OWNED ONLY. A cavity no oven owns refuses before
 * the base class could ever reach its standalone hearthDeclare() path (the
 * header comment's "no public standalone begin path"); an owned one takes
 * the base's owned branch, which validates the flavour against the declared
 * variant and caches without declaring (the parent's declaration is
 * authoritative, parent index and all). On success the cavity's own state
 * resets alongside the base's, matching the base begin()'s own
 * fridge-mode/current-mode reset.
 */
bool MatterOvenCavity::begin(double tempSetpoint, double minTemperature, double maxTemperature, double step) {
  if (!hearthOwnedByFridge) {
    return false;
  }
  if (!MatterTemperatureControlledCabinet::begin(tempSetpoint, minTemperature, maxTemperature, step)) {
    return false;
  }
  ovenModesCount = 0;
  cavityOperationalState = 0;
  return true;
}

bool MatterOvenCavity::begin(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel) {
  if (!hearthOwnedByFridge) {
    return false;
  }
  if (!MatterTemperatureControlledCabinet::begin(supportedLevels, levelCount, selectedLevel)) {
    return false;
  }
  ovenModesCount = 0;
  cavityOperationalState = 0;
  return true;
}

/*
 * Builds and sends "AT+MTMODES=<ep>,73,<mode1>,<tag1>,"<label1>",..."
 * (AT_MT_SPEC.md S3.20.1's cluster-aware form) for exactly the triples
 * given. Wire-only: the caller decides whether/what to commit to the cache
 * afterwards (house discipline: a failed write must not update it). Same
 * shape as the base class's hearthSendFridgeModes() with the cluster fixed
 * to OvenMode.
 */
bool MatterOvenCavity::hearthSendOvenModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  char cmd[500];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u,%lu", (unsigned)getEndPointId(), (unsigned long)kOvenModeClusterId);
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,%u,\"%s\"", (unsigned)modes[i], (unsigned)tags[i], labels[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * S3.20.1's grammar enforced host-side before the wire would have to, the
 * identical discipline the base class's cluster-82 version (and
 * MatterRoboticVacuum before it) established: count bounds, mode uniqueness
 * within this call, per-label length/printability/quote-exclusion, every
 * violation Hearth.hearthSetError(1) with no wire traffic; an unaddressable
 * endpoint (pre-reconcile) is hearthSetError(2). Cache commit only after a
 * successful wire write. Hides the base version: this one targets OvenMode
 * (73) and its own storage, never the fridge-cabinet arrays the base
 * reconcile hook would resend on cluster 82.
 */
bool MatterOvenCavity::setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  if (!hearthOwnedByFridge || hearthOwnedInert || !started) {
    return false;
  }
  if (modes == nullptr || tags == nullptr || labels == nullptr || count == 0 || count > kMaxOvenModes) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
      if (modes[i] == modes[j]) {
        Hearth.hearthSetError(1);
        return false;
      }
    }
    if (labels[i] == nullptr) {
      Hearth.hearthSetError(1);
      return false;
    }
    size_t len = strlen(labels[i]);
    if (len == 0 || len > kMaxOvenModeLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* S3.20.1's own grammar, checked host-side before the wire would have
     * to: every byte printable ASCII (0x20..0x7E), and never a '"'. A comma
     * is deliberately NOT rejected: legal inside a quoted label. */
    for (const char *p = labels[i]; *p != '\0'; p++) {
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
  if (!hearthSendOvenModes(modes, tags, labels, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(ovenModes, modes, count * sizeof(uint8_t));
  memcpy(ovenTags, tags, count * sizeof(uint16_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(ovenModeLabels[i], labels[i], kMaxOvenModeLabelLen);
    ovenModeLabels[i][kMaxOvenModeLabelLen] = '\0';
  }
  ovenModesCount = count;
  return true;
}

void MatterOvenCavity::onStop(std::function<bool()> cb) {
  _onStopCB = cb;
}

void MatterOvenCavity::onStart(std::function<bool()> cb) {
  _onStartCB = cb;
}

/*
 * AT+MTOPSTATE=<ep>,<state> (AT_MT_SPEC.md S3.21). getEndPointId() == 0 is
 * checked directly here, not through hearthEndPointAddressable(): a custom
 * wire verb repeats the check locally, the MatterOperationalStateEndpoint /
 * MatterDoorLock pattern.
 */
bool MatterOvenCavity::hearthSendOperationalState(uint8_t state) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTOPSTATE=%u,%u", (unsigned)getEndPointId(), (unsigned)state);
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Plain {0,1,2} membership enforced HOST-side (the task brief's explicit
 * requirement for this typed child; see the header comment for why this
 * diverges from the trio's firmware-validates precedent), then the trio's
 * own shape: skip-if-equal (sound here because both cache and device boot
 * at Stopped), wire write, cache commit only on success.
 */
bool MatterOvenCavity::setOperationalState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (state > kMaxOperationalState) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (cavityOperationalState == state) {
    return true;
  }
  if (!hearthSendOperationalState(state)) {
    return false;  // cache untouched on a failed write
  }
  cavityOperationalState = state;
  return true;
}

uint8_t MatterOvenCavity::getOperationalState() {
  return cavityOperationalState;
}

/*
 * The cavity's whole adjudication surface (AT_MT_SPEC.md S3.17):
 * ChangeToMode on OvenMode (73,0), the requested mode as fields.value[0],
 * with the cache updating ONLY on an allow (the 0.6.0 rule); and Stop
 * (72,1) / Start (72,2), no payload, whose verdict IS the wire response
 * the controller observes. Any other command id on cluster 72, including
 * the disallowed Pause (0) and Resume (3) the firmware never forwards, is
 * denied without reaching any callback. Everything else defers to
 * MatterEndPoint's fail-closed default DIRECTLY, deliberately skipping the
 * cabinet base class's owned-cabinet cluster-82 adjudication: the cavity
 * carries OvenMode, not the fridge-cabinet cluster, so a spurious
 * cluster-82 forward must be denied without consulting the callback the
 * two paths share (see the header comment).
 */
bool MatterOvenCavity::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (hearthOwnedByFridge && !hearthOwnedInert && started) {
    if (cluster_id == kOvenModeClusterId && command_id == kChangeToModeCommandId) {
      uint8_t requested = (fields.count > 0 && fields.present[0]) ? (uint8_t)fields.value[0] : 0;
      bool allow = _onChangeModeCB ? _onChangeModeCB(requested) : false;
      if (allow) {
        currentFridgeMode = requested;  // the inherited mode cache getCurrentMode() reads
      }
      return allow;
    }
    if (cluster_id == kOvenCavityOpStateClusterId) {
      if (command_id == kStopCommandId) {
        return _onStopCB ? _onStopCB() : false;
      }
      if (command_id == kStartCommandId) {
        return _onStartCB ? _onStartCB() : false;
      }
      return false;  /* Pause/Resume and anything else: denied, no callback */
    }
  }
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}

/*
 * On every reconcile: the base class pushes the cached temperature
 * configuration (its own cluster-82 mode resend is structurally dead here,
 * this class never populates those arrays), then the cached OvenMode list
 * is resent, which the firmware does not persist across a reboot (S3.20.1),
 * the same B120 shape as every mode-carrying class. A no-op when nothing
 * has been set yet. Best-effort, the base hook's own convention: this runs
 * deep inside ArduinoMatter::begin(), which has already committed to its
 * composition verdict.
 */
void MatterOvenCavity::hearthOnReconciled() {
  MatterTemperatureControlledCabinet::hearthOnReconciled();
  if (!started || ovenModesCount == 0) {
    return;
  }
  const char *labelPtrs[kMaxOvenModes];
  for (uint8_t i = 0; i < ovenModesCount; i++) {
    labelPtrs[i] = ovenModeLabels[i];
  }
  hearthSendOvenModes(ovenModes, ovenTags, labelPtrs, ovenModesCount);
}
