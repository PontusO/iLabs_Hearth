/*
 * MatterRoboticVacuum.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the design rationale,
 * and the "CurrentMode caching" section explaining why attributeChangeCB()
 * is a no-op and the mode caches update only from an allowed
 * hearthOnForwardedCommandFields() verdict.
 */
#include "MatterEndpoints/MatterRoboticVacuum.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <string.h>

namespace {
/* robotic_vacuum_cleaner (ESP_MATTER_ROBOTIC_VACUUM_CLEANER_DEVICE_TYPE_ID),
 * RvcRunMode::Id, RvcCleanMode::Id, RvcOperationalState::Id, and the six
 * command ids (ChangeToMode shared by the two ModeBase clusters; Pause,
 * Resume, GoHome on RvcOperationalState). See MatterRoboticVacuum.h's
 * header comment for the quoted lines from the pinned esp-matter checkout's
 * generated headers. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kRoboticVacuumDeviceType = 0x0074;
const uint32_t kRvcRunModeClusterId = 0x0054;         // 84 decimal
const uint32_t kRvcCleanModeClusterId = 0x0055;       // 85 decimal
const uint32_t kRvcOperationalStateClusterId = 0x0061;  // 97 decimal
const uint32_t kChangeToModeCommandId = 0x0000;       // shared by both ModeBase clusters
const uint32_t kPauseCommandId = 0x0000;
const uint32_t kResumeCommandId = 0x0003;
const uint32_t kGoHomeCommandId = 0x0080;
}  // namespace

MatterRoboticVacuum::MatterRoboticVacuum() {}

MatterRoboticVacuum::~MatterRoboticVacuum() {
  end();
}

bool MatterRoboticVacuum::begin() {
  if (!hearthDeclare(this, kRoboticVacuumDeviceType)) {
    return false;
  }
  operationalState = kStateStopped;
  currentRunMode = 0;
  currentCleanMode = 0;
  runModesCount = 0;
  cleanModesCount = 0;
  started = true;
  return true;
}

void MatterRoboticVacuum::end() {
  started = false;
  runModesCount = 0;
  cleanModesCount = 0;
}

/*
 * Builds and sends "AT+MTMODES=<ep>,<cluster>,<mode1>,<tag1>,"<label1>",..."
 * (AT_MT_SPEC.md S3.20.1) for exactly the triples given. Wire-only: the
 * caller decides whether/what to commit to the cache afterwards (house
 * discipline: a failed write must not update it).
 */
bool MatterRoboticVacuum::hearthSendModes(uint32_t cluster, const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  char cmd[500];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u,%lu", (unsigned)getEndPointId(), (unsigned long)cluster);
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,%u,\"%s\"", (unsigned)modes[i], (unsigned)tags[i], labels[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Shared host-side grammar enforcement (S3.20.1) plus the wire send and
 * cache commit, common to setSupportedRunModes()/setSupportedCleanModes().
 * See MatterModeSelect::setSupportedModes() for the identical discipline
 * this mirrors: count bounds, mode uniqueness within THIS call's list, and
 * per-label length/printability/quote-exclusion. <tag>'s 0..0xFFFF range is
 * already guaranteed by its uint16_t parameter type, nothing extra to check.
 * Every violation reports Hearth.hearthSetError(1) without ever reaching
 * the wire.
 */
bool MatterRoboticVacuum::hearthSetModeList(
  uint32_t cluster, const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count, uint8_t *cacheModes,
  uint16_t *cacheTags, char cacheLabels[][kMaxLabelLen + 1], uint8_t *cacheCount
) {
  if (!started) {
    return false;
  }
  if (modes == nullptr || tags == nullptr || labels == nullptr || count == 0 || count > kMaxModes) {
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
    if (len == 0 || len > kMaxLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* AT_MT_SPEC.md S3.20.1's own grammar, checked host-side before the
     * wire would have to: every byte printable ASCII (0x20..0x7E), and
     * never a '"'. A comma is deliberately NOT rejected here: S3.20.1
     * states plainly that a comma inside a quoted label is legal and part
     * of its text (the same S3.20 rule the ModeSelect form follows). */
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
  if (!hearthSendModes(cluster, modes, tags, labels, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(cacheModes, modes, count * sizeof(uint8_t));
  memcpy(cacheTags, tags, count * sizeof(uint16_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(cacheLabels[i], labels[i], kMaxLabelLen);
    cacheLabels[i][kMaxLabelLen] = '\0';
  }
  *cacheCount = count;
  return true;
}

bool MatterRoboticVacuum::setSupportedRunModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  return hearthSetModeList(kRvcRunModeClusterId, modes, tags, labels, count, runModes, runTags, runLabels, &runModesCount);
}

bool MatterRoboticVacuum::setSupportedCleanModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  return hearthSetModeList(kRvcCleanModeClusterId, modes, tags, labels, count, cleanModes, cleanTags, cleanLabels, &cleanModesCount);
}

void MatterRoboticVacuum::onChangeRunMode(std::function<bool(uint8_t)> cb) {
  _onChangeRunModeCB = cb;
}

void MatterRoboticVacuum::onChangeCleanMode(std::function<bool(uint8_t)> cb) {
  _onChangeCleanModeCB = cb;
}

void MatterRoboticVacuum::onPause(std::function<bool()> cb) {
  _onPauseCB = cb;
}

void MatterRoboticVacuum::onResume(std::function<bool()> cb) {
  _onResumeCB = cb;
}

void MatterRoboticVacuum::onGoHome(std::function<bool()> cb) {
  _onGoHomeCB = cb;
}

/*
 * AT+MTOPSTATE=<ep>,<state> (AT_MT_SPEC.md S3.21). getEndPointId() == 0 is
 * checked directly here, not through the private endpoint-addressable guard:
 * AT+MTOPSTATE is its own command, not an attribute write, the same pattern
 * MatterOperationalStateEndpoint::hearthSendOperationalState() establishes
 * for the identical reason.
 */
bool MatterRoboticVacuum::hearthSendOperationalState(uint8_t state) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTOPSTATE=%u,%u", (unsigned)getEndPointId(), (unsigned)state);
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterRoboticVacuum::setOperationalState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (operationalState == state) {
    return true;
  }
  if (!hearthSendOperationalState(state)) {
    return false;  // cache untouched on a failed write
  }
  operationalState = state;
  return true;
}

uint8_t MatterRoboticVacuum::getOperationalState() {
  return operationalState;
}

uint8_t MatterRoboticVacuum::getCurrentRunMode() {
  return currentRunMode;
}

uint8_t MatterRoboticVacuum::getCurrentCleanMode() {
  return currentCleanMode;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first: resends both cached SupportedModes lists, which the
 * firmware does not persist across a reboot (S3.20.1), the same B120 shape
 * as MatterModeSelect's own hearthOnReconciled(), doubled since this class
 * carries two independent lists on one endpoint. A no-op for whichever
 * list (or both) nothing has been set on yet.
 */
void MatterRoboticVacuum::hearthOnReconciled() {
  if (!started) {
    return;
  }
  if (runModesCount > 0) {
    const char *labelPtrs[kMaxModes];
    for (uint8_t i = 0; i < runModesCount; i++) {
      labelPtrs[i] = runLabels[i];
    }
    hearthSendModes(kRvcRunModeClusterId, runModes, runTags, labelPtrs, runModesCount);
  }
  if (cleanModesCount > 0) {
    const char *labelPtrs[kMaxModes];
    for (uint8_t i = 0; i < cleanModesCount; i++) {
      labelPtrs[i] = cleanLabels[i];
    }
    hearthSendModes(kRvcCleanModeClusterId, cleanModes, cleanTags, labelPtrs, cleanModesCount);
  }
}

/*
 * A documented no-op returning `started`: see the header comment's
 * "CurrentMode caching" section. No attribute on RvcRunMode, RvcCleanMode
 * or RvcOperationalState has an AT+MTATTR path (S3.20.1/S3.21's
 * AttributeAccessInterface finding), so hearthDispatchAttr() (Hearth.cpp)
 * never has a (cluster, attribute) pair from any of the three to route
 * here in the first place. Present only because MatterEndPoint declares
 * this pure virtual -- the same shape MatterOperationalStateEndpoint's own
 * header comment establishes.
 */
bool MatterRoboticVacuum::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

esp_matter_val_type_t MatterRoboticVacuum::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * AT_MT_SPEC.md S3.17/S3.20.1/S3.21: the firmware forwards a
 * controller-invoked ChangeToMode (RvcRunMode/RvcCleanMode) or
 * Pause/Resume/GoHome (RvcOperationalState) here for a verdict.
 * ChangeToMode's requested mode arrives as fields.value[0] (S3.20.1: "the
 * requested mode as the payload"); Pause/Resume/GoHome carry no payload.
 * Everything else -- an unstarted endpoint, the wrong cluster, or an
 * unrecognised command id -- defers to the base class default
 * (MatterEndPoint::hearthOnForwardedCommandFields(), itself fail-closed:
 * false for a subclass that has not overridden the legacy four-argument
 * virtual either, which this class has not), rather than returning false
 * directly.
 *
 * The mode caches update ONLY on an allow (see the header comment's
 * "CurrentMode caching" section): there is no ember-level signal of any
 * kind for a CurrentMode change on these clusters, so the verdict this
 * host itself gives is the only trustworthy record of what the device's
 * CurrentMode actually became.
 */
bool MatterRoboticVacuum::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (started && cluster_id == kRvcRunModeClusterId && command_id == kChangeToModeCommandId) {
    uint8_t requested = (fields.count > 0 && fields.present[0]) ? (uint8_t)fields.value[0] : 0;
    bool allow = _onChangeRunModeCB ? _onChangeRunModeCB(requested) : false;
    if (allow) {
      currentRunMode = requested;
    }
    return allow;
  }
  if (started && cluster_id == kRvcCleanModeClusterId && command_id == kChangeToModeCommandId) {
    uint8_t requested = (fields.count > 0 && fields.present[0]) ? (uint8_t)fields.value[0] : 0;
    bool allow = _onChangeCleanModeCB ? _onChangeCleanModeCB(requested) : false;
    if (allow) {
      currentCleanMode = requested;
    }
    return allow;
  }
  if (started && cluster_id == kRvcOperationalStateClusterId) {
    if (command_id == kPauseCommandId) {
      return _onPauseCB ? _onPauseCB() : false;
    }
    if (command_id == kResumeCommandId) {
      return _onResumeCB ? _onResumeCB() : false;
    }
    if (command_id == kGoHomeCommandId) {
      return _onGoHomeCB ? _onGoHomeCB() : false;
    }
  }
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}
