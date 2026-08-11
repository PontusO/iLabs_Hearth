/*
 * MatterRefrigerator.cpp - implementation. See the header for the design
 * rationale (owned cabinets over Task 7's parent-aware declaration, the
 * 0.6.0 CurrentMode caching rule, the AT+MTALARM fridge form), and
 * MatterTemperatureControlledCabinet.h's Task 8 comment for the owned
 * cabinet's side of the contract.
 */
#include "MatterEndpoints/MatterRefrigerator.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <string.h>

namespace {
/* refrigerator (ESP_MATTER_REFRIGERATOR_DEVICE_TYPE_ID) and
 * temperature_controlled_cabinet
 * (ESP_MATTER_TEMPERATURE_CONTROLLED_CABINET_DEVICE_TYPE_ID), AT_MT_SPEC.md
 * S3.9's device type table;
 * RefrigeratorAndTemperatureControlledCabinetMode::Id (0x00000052, "cluster
 * code: 82/0x52", see the header comment's quoted source); ChangeToMode is
 * ModeBase's command 0x0000. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kRefrigeratorDeviceType = 0x0070;
const uint32_t kCabinetDeviceType = 0x0071;
const uint32_t kRefrigeratorTccModeClusterId = 0x0052;  // 82 decimal
const uint32_t kChangeToModeCommandId = 0x0000;
/* AT+MTALARM's fridge form (S3.22): bit 0 is DoorOpen, the one bit
 * RefrigeratorAlarmServer's Supported bitmap carries today. */
const uint8_t kDoorOpenAlarmBit = 0;
}  // namespace

MatterRefrigerator::MatterRefrigerator() {
  /* The reject cabinet: owned AND inert, so its begin() overloads fail on
   * the inert check before anything else, and the modes/temperature APIs
   * refuse too. See addCabinet(). */
  _inertCabinet.hearthOwnedByFridge = true;
  _inertCabinet.hearthOwnedInert = true;
}

MatterRefrigerator::~MatterRefrigerator() {
  started = false;
}

/*
 * Pre-begin only: after begin() has declared the composition, an extra
 * cabinet could never be declared under the parent (and after reconcile,
 * not at all), so the reject reference is returned instead and the sketch
 * finds out at that cabinet's begin(). Capacity overflow gets the same
 * treatment for the same reason: silently aliasing a real cabinet would
 * make the over-capacity begin() succeed against the wrong endpoint.
 */
MatterTemperatureControlledCabinet &MatterRefrigerator::addCabinet(CabinetFlavour_t flavour) {
  if (started || _cabinetCount >= kMaxCabinets) {
    return _inertCabinet;
  }
  MatterTemperatureControlledCabinet &cab = _cabinets[_cabinetCount];
  _cabinetCount++;
  cab.hearthOwnedByFridge = true;
  cab.hearthOwnedLevels = (flavour == LEVELS);
  return cab;
}

/*
 * Declares the fridge first, then every added cabinet with parentIndex =
 * the fridge's own registry index (declaration order, exactly what the wire
 * grammar's third field carries, S3.9). The cabinets' own begin() calls
 * declare nothing (see the cabinet header's Task 8 comment); this is the
 * single place the composed shape enters the registry. hearthDeclare()
 * itself is what refuses a re-begin after reconcile (+MTERR:10), before any
 * member state changes.
 */
bool MatterRefrigerator::begin() {
  if (!hearthDeclare(this, kRefrigeratorDeviceType)) {
    return false;
  }
  /* The fridge's own registry index: found, not assumed, because a
   * re-declare updates in place (this object may sit anywhere if the sketch
   * declared other endpoints first). */
  uint8_t ownIndex = HEARTH_NO_PARENT;
  for (uint8_t i = 0; i < hearthDeclaredCount(); i++) {
    if (hearthDeclaredAt(i) == this) {
      ownIndex = i;
      break;
    }
  }
  if (ownIndex == HEARTH_NO_PARENT) {
    return false;  // structurally unreachable: the declare above succeeded
  }
  for (uint8_t i = 0; i < _cabinetCount; i++) {
    uint8_t variant = _cabinets[i].hearthOwnedLevels ? 1 : 0;
    if (!hearthDeclare(&_cabinets[i], kCabinetDeviceType, variant, ownIndex)) {
      return false;
    }
  }
  currentMode = 0;
  modesCount = 0;
  started = true;
  return true;
}

/*
 * Builds and sends "AT+MTMODES=<ep>,82,<mode1>,<tag1>,"<label1>",..."
 * (AT_MT_SPEC.md S3.20.1's cluster-aware form) for exactly the triples
 * given. Wire-only: the caller decides whether/what to commit to the cache
 * afterwards (house discipline: a failed write must not update it).
 */
bool MatterRefrigerator::hearthSendModes(const uint8_t *sendModes, const uint16_t *sendTags, const char *const *labels, uint8_t count) {
  char cmd[500];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u,%lu", (unsigned)getEndPointId(), (unsigned long)kRefrigeratorTccModeClusterId);
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,%u,\"%s\"", (unsigned)sendModes[i], (unsigned)sendTags[i], labels[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * S3.20.1's grammar enforced host-side before the wire would have to, the
 * identical discipline MatterRoboticVacuum::hearthSetModeList() established:
 * count bounds, mode uniqueness within this call, per-label
 * length/printability/quote-exclusion, every violation
 * Hearth.hearthSetError(1) with no wire traffic. <tag>'s 0..0xFFFF range is
 * already guaranteed by its uint16_t parameter type. Cache commit only
 * after a successful wire write.
 */
bool MatterRefrigerator::setSupportedModes(const uint8_t *newModes, const uint16_t *newTags, const char *const *labels, uint8_t count) {
  if (!started) {
    return false;
  }
  if (newModes == nullptr || newTags == nullptr || labels == nullptr || count == 0 || count > kMaxModes) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
      if (newModes[i] == newModes[j]) {
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
    /* S3.20.1's own grammar: every byte printable ASCII (0x20..0x7E), and
     * never a '"'. A comma is deliberately NOT rejected: legal inside a
     * quoted label. */
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
  if (!hearthSendModes(newModes, newTags, labels, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(modes, newModes, count * sizeof(uint8_t));
  memcpy(tags, newTags, count * sizeof(uint16_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(modeLabels[i], labels[i], kMaxLabelLen);
    modeLabels[i][kMaxLabelLen] = '\0';
  }
  modesCount = count;
  return true;
}

void MatterRefrigerator::onChangeMode(std::function<bool(uint8_t)> cb) {
  _onChangeModeCB = cb;
}

uint8_t MatterRefrigerator::getCurrentMode() {
  return currentMode;
}

bool MatterRefrigerator::setDoorOpenAlarm(bool active) {
  return setAlarmState(kDoorOpenAlarmBit, active);
}

/*
 * AT+MTALARM=<ep>,<bit>,<0|1> (AT_MT_SPEC.md S3.22's fridge form).
 * getEndPointId() == 0 is checked directly here, not through the private
 * endpoint-addressable guard: AT+MTALARM is its own command, not an
 * attribute write, the same pattern MatterSmokeCOAlarm and the
 * AT+MTOPSTATE senders establish. The bit passes through unvalidated past
 * its uint8_t type: the firmware checks it against the endpoint's Supported
 * bitmap and answers +MTERR:1 (S3.22), which hearthCommand() surfaces as
 * lastError() exactly like any other wire rejection.
 */
bool MatterRefrigerator::setAlarmState(uint8_t bit, bool active) {
  if (!started) {
    return false;
  }
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTALARM=%u,%u,%u", (unsigned)getEndPointId(), (unsigned)bit, active ? 1u : 0u);
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first: resends the parent's cached 0x0052 SupportedModes list,
 * which the firmware does not persist across a reboot (S3.20.1), the same
 * B120 shape as MatterModeSelect's own hook. A no-op when nothing has been
 * set yet. The owned cabinets resend their own state from their own hooks,
 * which run after this one in registry order.
 */
void MatterRefrigerator::hearthOnReconciled() {
  if (!started || modesCount == 0) {
    return;
  }
  const char *labelPtrs[kMaxModes];
  for (uint8_t i = 0; i < modesCount; i++) {
    labelPtrs[i] = modeLabels[i];
  }
  hearthSendModes(modes, tags, labelPtrs, modesCount);
}

/*
 * A documented no-op returning `started`: see the header comment.
 * CurrentMode never raises a +MTATTR URC (the 0.6.0 rule), and this class
 * registers no other attribute interest, so hearthDispatchAttr()
 * (Hearth.cpp) has nothing legitimate to route here. Present only because
 * MatterEndPoint declares this pure virtual, the MatterRoboticVacuum
 * precedent.
 */
bool MatterRefrigerator::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

/*
 * AT_MT_SPEC.md S3.17/S3.20.1: the firmware forwards a controller-invoked
 * ChangeToMode on the parent's 0x0052 cluster here for a verdict, the
 * requested mode as fields.value[0]. The cache updates ONLY on an allow
 * (the 0.6.0 rule, see the header comment); no callback registered denies
 * by default (fail closed). Everything else -- an unstarted endpoint, the
 * wrong cluster, an unrecognised command id -- defers to the base class
 * default rather than returning false directly.
 */
bool MatterRefrigerator::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (started && cluster_id == kRefrigeratorTccModeClusterId && command_id == kChangeToModeCommandId) {
    uint8_t requested = (fields.count > 0 && fields.present[0]) ? (uint8_t)fields.value[0] : 0;
    bool allow = _onChangeModeCB ? _onChangeModeCB(requested) : false;
    if (allow) {
      currentMode = requested;
    }
    return allow;
  }
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}
