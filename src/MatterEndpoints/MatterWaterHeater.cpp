/*
 * MatterWaterHeater.cpp - implementation. See the header for the design
 * notes: the four surfaces on one endpoint, the variant refusals, the
 * null-until-pushed 0x94 discipline, the reconcile split, the Boost mask
 * unpacking and the verdict-then-push deferral, and the thermostat cache
 * seeds (the Task 3 ledger contract).
 */
#include "MatterEndpoints/MatterWaterHeater.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <string.h>

namespace {
/* water_heater (ESP_MATTER_WATER_HEATER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:140,
 * "#define ESP_MATTER_WATER_HEATER_DEVICE_TYPE_ID 0x050F"). Given as a
 * plain integer: there is no esp-matter header on a host build. */
const uint32_t kWaterHeaterDeviceType = 0x050F;
}  // namespace

MatterWaterHeater::MatterWaterHeater() : meas(this) {}

MatterWaterHeater::~MatterWaterHeater() {
  end();
}

bool MatterWaterHeater::begin(Variant_t variant) {
  /* An enum parameter does not stop a cast from smuggling in a third
   * value, and the variant byte travels the wire verbatim, so validate it
   * host-side, the electrical sensor's shape. */
  if (variant != FULL && variant != MINIMAL) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthDeclare(this, kWaterHeaterDeviceType, (uint8_t)variant)) {
    return false;
  }
  variantSel = variant;
  heaterTypes = 0;
  heatDemand = 0;
  tankVolume = 0;
  estHeatRequired = 0;
  tankPercentage = 0;
  hasHeaterTypes = hasHeatDemand = hasTankVolume = hasEstHeatRequired = hasTankPercentage = false;
  pendingBoostPush = kBoostPushNone;
  modesCount = 0;
  currentWaterHeaterMode = 0;
  /* THE LEDGER CONTRACT (firmware Task 3, reviewer-mandated carry): seed
   * the thermostat cache from the C6's OWN cluster defaults, or the
   * first-write-swallow defect recurs (an unchanged-value write suppressed
   * by a cache that does not match the device). OccupiedHeatingSetpoint
   * 2000 (esp_matter_feature.h: "occupied_heating_setpoint(2000)") and
   * SystemMode 1/Auto (esp_matter_cluster.h: "system_mode(1)"); the
   * firmware thunk overrides neither. LocalTemperature's device default is
   * NULL, so it gets a has-flag instead of a seed: the first push always
   * writes (see setLocalTemperature()). */
  heatingSetpointTemperature = 2000;
  localTemperature = 2000;
  hasLocalTemperature = false;
  currentMode = THERMOSTAT_MODE_AUTO;
  meas.reset();
  /* Set explicitly, never left to the constructor default (Task 5's
   * ledger note): the helper's `enabled` gates its energy adders, and on
   * MINIMAL this class refuses the whole measurement surface itself
   * before ever delegating, so the flag matters on FULL only; it is still
   * assigned on both paths so the state is never implicit. */
  meas.enabled = (variant == FULL);
  started = true;
  return true;
}

void MatterWaterHeater::end() {
  started = false;
  modesCount = 0;
  pendingBoostPush = kBoostPushNone;
}

/*
 * "AT+MTMEAS=<ep>,148,<field>,<value>" (AT_MT_SPEC.md S3.25's 0x94 table).
 * getEndPointId() == 0 is checked directly here, the
 * HearthMeasurementPush::hearthSendPowerPairs() pattern: AT+MTMEAS does
 * not ride the AT+MTATTR guard path. %lld always: field 4 is the one
 * signed field and the unsigned fields never carry a negative value here.
 */
bool MatterWaterHeater::hearthSendWhmPair(uint8_t field, int64_t value) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu,%u,%lld", (unsigned)getEndPointId(), (unsigned long)kWhmClusterId, (unsigned)field, (long long)value);
  return Hearth.hearthCommand(cmd) == 0;
}

/* The five WHM field setters share one shape, the electrical setters'
 * null-until-pushed discipline: no-op only when the field has been pushed
 * before AND the value is unchanged; cache and has-flag commit only on a
 * successful write. */
bool MatterWaterHeater::hearthSetWhmField(uint8_t field, int64_t value, int64_t *cache, bool *hasFlag) {
  if (*hasFlag && *cache == value) {
    return true;
  }
  if (!hearthSendWhmPair(field, value)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  *cache = value;
  *hasFlag = true;
  return true;
}

/* MINIMAL builds neither gated feature nor the electrical graft, so the
 * answer is already known host-side: error 1, zero wire traffic (the
 * POWER_ONLY precedent). */
bool MatterWaterHeater::hearthRefusedOnMinimal() {
  if (variantSel == MINIMAL) {
    Hearth.hearthSetError(1);
    return true;
  }
  return false;
}

bool MatterWaterHeater::setHeaterTypes(uint8_t bitmap) {
  if (!started) {
    return false;
  }
  int64_t cache = heaterTypes;
  bool ok = hearthSetWhmField(kFieldHeaterTypes, bitmap, &cache, &hasHeaterTypes);
  if (ok) {
    heaterTypes = (uint8_t)cache;
  }
  return ok;
}

bool MatterWaterHeater::setHeatDemand(uint8_t bitmap) {
  if (!started) {
    return false;
  }
  int64_t cache = heatDemand;
  bool ok = hearthSetWhmField(kFieldHeatDemand, bitmap, &cache, &hasHeatDemand);
  if (ok) {
    heatDemand = (uint8_t)cache;
  }
  return ok;
}

bool MatterWaterHeater::setTankVolume(uint16_t litres) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  int64_t cache = tankVolume;
  bool ok = hearthSetWhmField(kFieldTankVolume, litres, &cache, &hasTankVolume);
  if (ok) {
    tankVolume = (uint16_t)cache;
  }
  return ok;
}

bool MatterWaterHeater::setEstimatedHeatRequired(int64_t mwh) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return hearthSetWhmField(kFieldEstHeatRequired, mwh, &estHeatRequired, &hasEstHeatRequired);
}

bool MatterWaterHeater::setTankPercentage(uint8_t pct) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  int64_t cache = tankPercentage;
  bool ok = hearthSetWhmField(kFieldTankPercentage, pct, &cache, &hasTankPercentage);
  if (ok) {
    tankPercentage = (uint8_t)cache;
  }
  return ok;
}

void MatterWaterHeater::onBoost(std::function<bool(const BoostInfo &)> cb) {
  _onBoostCB = cb;
}

void MatterWaterHeater::onCancelBoost(std::function<bool()> cb) {
  _onCancelBoostCB = cb;
}

/*
 * The sketch's own timer path: push BoostState Inactive directly (the
 * firmware derives BoostEnded from the transition, S3.25; a push repeating
 * Inactive derives nothing, which is why this does not track the state
 * host-side: repeating is legal and honest). Clears any deferred push
 * first so an explicit end can never be followed by a stale Active.
 */
bool MatterWaterHeater::endBoost() {
  if (!started) {
    return false;
  }
  pendingBoostPush = kBoostPushNone;
  return hearthSendWhmPair(kFieldBoostState, 0);
}

/*
 * "AT+MTMODES=<ep>,158,<mode>,<tag>,"<label>",..." (AT_MT_SPEC.md
 * S3.20.1's cluster-aware form). Wire-only, the caller commits the cache;
 * the MatterRoboticVacuum::hearthSendModes() shape on this class's one
 * mode cluster.
 */
bool MatterWaterHeater::hearthSendModes(const uint8_t *m, const uint16_t *t, const char *const *l, uint8_t count) {
  char cmd[500];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u,%lu", (unsigned)getEndPointId(), (unsigned long)kWaterHeaterModeClusterId);
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,%u,\"%s\"", (unsigned)m[i], (unsigned)t[i], l[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Host-side grammar enforcement (S3.20.1), the identical discipline
 * MatterRoboticVacuum::hearthSetModeList() established: count bounds, mode
 * uniqueness within THIS call's list, per-label length/printability/
 * quote-exclusion (a comma stays legal inside a quoted label). Every
 * violation reports Hearth.hearthSetError(1) without reaching the wire;
 * cache commit only after a successful write.
 */
bool MatterWaterHeater::setSupportedModes(const uint8_t *m, const uint16_t *t, const char *const *l, uint8_t count) {
  if (!started) {
    return false;
  }
  if (m == nullptr || t == nullptr || l == nullptr || count == 0 || count > kMaxModes) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
      if (m[i] == m[j]) {
        Hearth.hearthSetError(1);
        return false;
      }
    }
    if (l[i] == nullptr) {
      Hearth.hearthSetError(1);
      return false;
    }
    size_t len = strlen(l[i]);
    if (len == 0 || len > kMaxLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    for (const char *p = l[i]; *p != '\0'; p++) {
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
  if (!hearthSendModes(m, t, l, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(modes, m, count * sizeof(uint8_t));
  memcpy(tags, t, count * sizeof(uint16_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(labels[i], l[i], kMaxLabelLen);
    labels[i][kMaxLabelLen] = '\0';
  }
  modesCount = count;
  return true;
}

void MatterWaterHeater::onChangeWaterHeaterMode(std::function<bool(uint8_t)> cb) {
  _onChangeWaterHeaterModeCB = cb;
}

uint8_t MatterWaterHeater::getCurrentWaterHeaterMode() {
  return currentWaterHeaterMode;
}

/* The shared ember write shape (updateAttributeVal = AT+MTATTR mode 1,
 * reported to subscribers), cache on success only: MatterThermostat's
 * setRawTemperature() without the read-before-write it also skips. */
bool MatterWaterHeater::hearthWriteThermostatInt16(uint32_t attribute_id, int16_t raw, int16_t *cache) {
  esp_matter_attr_val_t val = esp_matter_int16(raw);
  if (!updateAttributeVal(kThermostatClusterId, attribute_id, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  *cache = raw;
  return true;
}

bool MatterWaterHeater::setMode(ThermostatMode_t mode) {
  if (!started) {
    return false;
  }
  /* membership, not a controlSequence switch: the water heater's cluster
   * is heating-only by construction and carries no sequence to gate on */
  if (mode != THERMOSTAT_MODE_OFF && mode != THERMOSTAT_MODE_AUTO && mode != THERMOSTAT_MODE_HEAT && mode != THERMOSTAT_MODE_EMERGENCY_HEAT) {
    return false;
  }
  if (currentMode == mode) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_enum8((uint8_t)mode);
  if (!updateAttributeVal(kThermostatClusterId, kSystemModeAttrId, &val)) {
    return false;
  }
  currentMode = mode;
  return true;
}

/*
 * No 7..30 C limit check, deliberately (see the header comment): the
 * thermostat cluster server's Min/MaxHeatSetpointLimit are OPTIONAL
 * attributes this cluster instance does not create, and a water heater
 * legitimately runs to 60-80 C. The firmware/ember side answers anything
 * it will not take.
 */
bool MatterWaterHeater::setHeatingSetpoint(double setpointHeatingTemperature) {
  if (!started) {
    return false;
  }
  int16_t raw = static_cast<int16_t>(setpointHeatingTemperature * 100.0f);
  if (heatingSetpointTemperature == raw) {
    return true;
  }
  return hearthWriteThermostatInt16(kHeatingSetpointAttrId, raw, &heatingSetpointTemperature);
}

/*
 * First call ALWAYS writes: the fabric-side LocalTemperature starts null
 * (esp-matter's nullable config default), so the seeded cache must not
 * suppress a first push that happens to equal it, the null-until-pushed
 * discipline (see the header comment's seed section).
 */
bool MatterWaterHeater::setLocalTemperature(double temperature) {
  if (!started) {
    return false;
  }
  int16_t raw = static_cast<int16_t>(temperature * 100.0f);
  if (hasLocalTemperature && localTemperature == raw) {
    return true;
  }
  if (!hearthWriteThermostatInt16(kLocalTemperatureAttrId, raw, &localTemperature)) {
    return false;
  }
  hasLocalTemperature = true;
  return true;
}

/* ---- the measurement surface: started guard, MINIMAL refusal, then the
 * shared helper (the electrical sensor's delegation shape with one extra
 * gate in front) ---- */

bool MatterWaterHeater::setVoltage(int64_t mv) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.setVoltage(mv);
}

bool MatterWaterHeater::setActiveCurrent(int64_t ma) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.setActiveCurrent(ma);
}

bool MatterWaterHeater::setActivePower(int64_t mw) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.setActivePower(mw);
}

bool MatterWaterHeater::setFrequency(int64_t mhz) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.setFrequency(mhz);
}

bool MatterWaterHeater::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.pushMeasurements(mv, ma, mw);
}

bool MatterWaterHeater::addEnergyImported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.addEnergyImported(mwh);
}

bool MatterWaterHeater::addEnergyExported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnMinimal()) {
    return false;
  }
  return meas.addEnergyExported(mwh);
}

int64_t MatterWaterHeater::getVoltage() {
  return meas.getVoltage();
}

int64_t MatterWaterHeater::getActiveCurrent() {
  return meas.getActiveCurrent();
}

int64_t MatterWaterHeater::getActivePower() {
  return meas.getActivePower();
}

int64_t MatterWaterHeater::getFrequency() {
  return meas.getFrequency();
}

uint64_t MatterWaterHeater::getEnergyImported() {
  return meas.getEnergyImported();
}

uint64_t MatterWaterHeater::getEnergyExported() {
  return meas.getEnergyExported();
}

/*
 * The reconcile split (design spec 4.2, the header comment's full
 * reasoning): configuration re-pushed, volatile has-flags cleared without
 * a resend (B229), the mode list re-sent (S3.20.1), the electrical fields
 * delegated to the helper's own B229. The re-push results are deliberately
 * unchecked, the MatterRoboticVacuum::hearthOnReconciled() shape: a failed
 * resend surfaces on the next ordinary setter call, not here.
 */
void MatterWaterHeater::hearthOnReconciled() {
  if (!started) {
    return;
  }
  if (hasHeaterTypes) {
    hearthSendWhmPair(kFieldHeaterTypes, heaterTypes);
  }
  if (hasTankVolume) {
    hearthSendWhmPair(kFieldTankVolume, tankVolume);
  }
  hasHeatDemand = false;
  hasTankPercentage = false;
  hasEstHeatRequired = false;
  pendingBoostPush = kBoostPushNone;
  if (modesCount > 0) {
    const char *labelPtrs[kMaxModes];
    for (uint8_t i = 0; i < modesCount; i++) {
      labelPtrs[i] = labels[i];
    }
    hearthSendModes(modes, tags, labelPtrs, modesCount);
  }
  meas.onReconciled();
}

/*
 * The push an accepted Boost/CancelBoost deferred behind its verdict (the
 * header comment's verdict-then-push section). The pending state is
 * claimed BEFORE the send: a firmware-refused push is not retried blind
 * (its +MTERR is a firmware anomaly the next explicit push resolves), and
 * the drain's own busy check means the send itself cannot be refused
 * re-entrant here.
 */
void MatterWaterHeater::hearthOnDeferredWork() {
  if (pendingBoostPush == kBoostPushNone) {
    return;
  }
  uint8_t v = (pendingBoostPush == kBoostPushActive) ? 1 : 0;
  pendingBoostPush = kBoostPushNone;
  hearthSendWhmPair(kFieldBoostState, v);
}

/*
 * Thermostat attributes (cluster 0x0201) are ember-served, so controller
 * writes arrive here as ordinary +MTATTR URCs and drive the callbacks,
 * MatterThermostat's exact shape minus the cooling members this heating-
 * only cluster does not carry. Cluster 148 is a documented ignore: S3.25
 * promises no +MTATTR URC ever reports it, and an injected one must not
 * move any cache (the host's own pushes are the single source of truth).
 * Returns true (consumed), the sibling classes' shape.
 */
bool MatterWaterHeater::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kThermostatClusterId) {
    return ret;
  }
  if (attribute_id == kSystemModeAttrId) {
    ThermostatMode_t newMode = (ThermostatMode_t)val->val.u8;
    if (_onChangeModeCB != NULL) {
      ret &= _onChangeModeCB(newMode);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      /* no write back: this callback is itself the notification of a
       * change already applied, and a write from inside it would loop */
      currentMode = newMode;
    }
  } else if (attribute_id == kLocalTemperatureAttrId) {
    int16_t newTemperature = val->val.i16;
    if (_onChangeTemperatureCB != NULL) {
      ret &= _onChangeTemperatureCB((float)newTemperature / 100.0f);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      localTemperature = newTemperature;
      hasLocalTemperature = true;
    }
  } else if (attribute_id == kHeatingSetpointAttrId) {
    int16_t newTemperature = val->val.i16;
    if (_onChangeHeatingSetpointCB != NULL) {
      ret &= _onChangeHeatingSetpointCB((double)newTemperature / 100.0);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      heatingSetpointTemperature = newTemperature;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterWaterHeater::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kThermostatClusterId) {
    if (attribute_id == kSystemModeAttrId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kLocalTemperatureAttrId || attribute_id == kHeatingSetpointAttrId) {
      return ESP_MATTER_VAL_TYPE_INT16;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * AT_MT_SPEC.md S3.17: Boost (148,0) carries "<duration>,<mask>[,<v1>
 * [,<v2>[,<v3>]]]" (the mask semantics in the header comment), CancelBoost
 * (148,1) is payload-less, ChangeToMode (158,0) carries the requested
 * mode. Everything else defers to the fail-closed base default. The
 * verdict is returned to the dispatcher (which sends AT+MTCMDRESP); an
 * accept arms the deferred BoostState push, sent right after the verdict
 * (see hearthOnDeferredWork() above).
 */
bool MatterWaterHeater::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (started && cluster_id == kWhmClusterId && command_id == kBoostCommandId) {
    /* duration and mask are the two mandatory leading fields */
    if (fields.count < 2 || !fields.present[0] || !fields.present[1]) {
      return false;  // malformed: denied without consulting the callback
    }
    BoostInfo info = {};
    info.duration = fields.value[0];
    uint16_t mask = (uint16_t)fields.value[1];
    info.hasOneShot = (mask & kBoostPresentOneShot) != 0;
    info.oneShot = (mask & kBoostValueOneShot) != 0;
    info.hasEmergency = (mask & kBoostPresentEmergency) != 0;
    info.emergency = (mask & kBoostValueEmergency) != 0;
    /* appended values, canonical order, only for the present NUMERIC
     * optionals: the mask is the authority on what follows it, so a tail
     * shorter than it promises is malformed and denied fail-closed. */
    uint8_t idx = 2;
    if (mask & kBoostPresentSetpoint) {
      if (idx >= fields.count || !fields.present[idx]) {
        return false;
      }
      info.hasSetpoint = true;
      /* int16 hundredths; a negative value arrives as the wire parser's
       * unsigned wrap and the truncating cast restores it */
      info.setpoint = (int16_t)(uint16_t)fields.value[idx++];
    }
    if (mask & kBoostPresentTargetPct) {
      if (idx >= fields.count || !fields.present[idx]) {
        return false;
      }
      info.hasTargetPct = true;
      info.targetPct = (uint8_t)fields.value[idx++];
    }
    if (mask & kBoostPresentReheat) {
      if (idx >= fields.count || !fields.present[idx]) {
        return false;
      }
      info.hasReheat = true;
      info.reheat = (uint8_t)fields.value[idx++];
    }
    bool allow = _onBoostCB ? _onBoostCB(info) : false;
    if (allow) {
      pendingBoostPush = kBoostPushActive;
      Hearth.hearthRequestDeferredWork();
    }
    return allow;
  }
  if (started && cluster_id == kWhmClusterId && command_id == kCancelBoostCommandId) {
    bool allow = _onCancelBoostCB ? _onCancelBoostCB() : false;
    if (allow) {
      pendingBoostPush = kBoostPushInactive;
      Hearth.hearthRequestDeferredWork();
    }
    return allow;
  }
  if (started && cluster_id == kWaterHeaterModeClusterId && command_id == kChangeToModeCommandId) {
    uint8_t requested = (fields.count > 0 && fields.present[0]) ? (uint8_t)fields.value[0] : 0;
    bool allow = _onChangeWaterHeaterModeCB ? _onChangeWaterHeaterModeCB(requested) : false;
    if (allow) {
      /* the only trustworthy record of CurrentMode: no ember signal ever
       * fires for it (S3.20.1, B196) */
      currentWaterHeaterMode = requested;
    }
    return allow;
  }
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}
