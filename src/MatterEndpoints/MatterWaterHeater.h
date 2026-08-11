/*
 * MatterWaterHeater.h - Task 6 (energy round B): the Water Heater, device
 * type 0x050F. A Hearth original: arduino-esp32's Matter library ships no
 * water heater class at all (see Hearth.h's umbrella comment), so the
 * public surface below is this port's own design against the firmware's
 * wire contract (docs/AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.25) and the
 * round's design spec 4.2.
 *
 * Device type 0x050F is water_heater
 * (esp_matter_endpoint.h's ESP_MATTER_WATER_HEATER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:140,
 * "#define ESP_MATTER_WATER_HEATER_DEVICE_TYPE_ID 0x050F"). One endpoint
 * carries FOUR host-visible surfaces at once, which is why this class is a
 * direct MatterEndPoint child embedding the shared HearthMeasurementPush
 * helper rather than a thin subclass of anything:
 *
 * - WaterHeaterManagement (0x0094, 148 decimal): host-pushed state over
 *   AT+MTMEAS's 0x94 field table (S3.25), Instance-served on the C6, plus
 *   the adjudicated Boost/CancelBoost command forwards (S3.17).
 * - WaterHeaterMode (0x009E, 158 decimal): the ModeBase surface,
 *   SupportedModes over the cluster-aware AT+MTMODES form (S3.20.1) and an
 *   adjudicated ChangeToMode forward.
 * - Thermostat (0x0201, 513 decimal), heating feature only: EMBER-served,
 *   so unlike everything above these attributes ride the ordinary
 *   AT+MTATTR path in both directions and controller writes raise +MTATTR
 *   URCs that drive the onChange* callbacks (the MatterThermostat
 *   machinery, design spec 3.5).
 * - At the FULL variant, the composed Electrical Sensor graft the device
 *   type XML mandates: the measurement-push surface (clusters 144/145,
 *   S3.25), delegated to the embedded HearthMeasurementPush helper with
 *   the electrical sensor's exact semantics.
 *
 * VARIANTS (S3.9's 0x050F rows): FULL (0) is the conformant composition
 * above; MINIMAL (1) is the SDK-bare build, disclosed sub-conformant (the
 * XML mandates the composed Electrical Sensor): WaterHeaterManagement
 * serves HeaterTypes, HeatDemand and BoostState only (no EnergyManagement
 * or TankPercent features), and there is no electrical graft at all. On
 * MINIMAL the tank trio (setTankVolume, setEstimatedHeatRequired,
 * setTankPercentage) and the WHOLE measurement surface therefore refuse
 * HOST-side with Hearth error 1 and zero wire traffic: the answer is
 * already known without spending the round trip (the electrical sensor's
 * POWER_ONLY precedent).
 *
 * THE 0x94 SURFACE IS INSTANCE-SERVED (S3.25's "no AT+MTATTR path, no
 * +MTATTR URCs" rule): a controller can never write any of it, no +MTATTR
 * URC ever reports it, and an injected one naming cluster 148 must not
 * move any cache (pinned in test_waterheater.cpp). The setters follow the
 * electrical classes' null-until-pushed discipline: per-field has-flags,
 * a first push of a value equal to the zero-initialised cache still
 * writes, a repeat after a successful push no-ops, and the cache commits
 * only on an OK-answered line.
 *
 * THE RECONCILE SPLIT (design spec 4.2, both halves test-pinned):
 * HeaterTypes and TankVolume are CONFIGURATION the C6 does not persist
 * and are re-pushed on every reconcile (the cabinet-labels precedent), as
 * is the WaterHeaterMode SupportedModes list (S3.20.1's own "not
 * persisted" policy). HeatDemand, BoostState, TankPercentage and
 * EstimatedHeatRequired are VOLATILE and follow B229: the wire-pushed
 * memory (has-flags) is cleared so a repeated value reaches the wire
 * again, the values are NOT re-sent (a stale sample re-reported as fresh
 * would be a lie), and after a co-processor reboot the delegate serves
 * its defaults until the sketch pushes fresh state, which is the honest
 * picture. The embedded measurement helper keeps its own B229 semantics
 * via meas.onReconciled(). The thermostat attributes are ember-owned and
 * follow MatterThermostat's precedent: nothing is re-pushed.
 *
 * BOOST AND THE PRESENCE MASK (S3.17; the constants mirror the firmware's
 * main/include/mt_matter.h MT_BOOST_* values, read from that source, not
 * transcribed from memory). The forward is
 * "+MTCMD:<seq>,<ep>,148,0,<duration>,<mask>[,<v1>[,<v2>[,<v3>]]]", the
 * wire's first five-field tail: bits 0-4 of <mask> are presence flags for
 * the five optional Boost parameters in canonical order (oneShot,
 * emergencyBoost, temporarySetpoint, targetPercentage, targetReheat),
 * bits 8-9 carry the VALUES of the two bools when present (a present bool
 * appends no field), and the appended values are only the present NUMERIC
 * optionals, canonical order. This class unpacks the mask into BoostInfo
 * so a sketch never touches the encoding. A tail shorter than the mask
 * promises is malformed and denied WITHOUT consulting the callback: the
 * mask is the authority on what follows it.
 *
 * THE VERDICT-THEN-PUSH SHAPE: the onBoost()/onCancelBoost() verdict
 * becomes the AT+MTCMDRESP answer (allow maps to Status::Success on the
 * wire, deny/timeout to Status::Failure, S3.17), and on an ACCEPT the
 * library itself then pushes BoostState (Active for Boost, Inactive for
 * CancelBoost) over AT+MTMEAS, which is what makes the firmware derive
 * the BoostStarted/BoostEnded event (S3.25: an accepted Boost's cached
 * parameters are consumed by the BoostStarted emission). The push cannot
 * be issued from inside the dispatch (a wire write there is refused
 * HEARTH_CMD_REENTRANT), so it is deferred through
 * Hearth.hearthRequestDeferredWork()/hearthOnDeferredWork() and lands
 * immediately AFTER the verdict on the wire, in the same poll. A boost
 * the sketch ends on its own timer calls endBoost(), a direct Inactive
 * push from ordinary sketch context; the firmware's in-state guard
 * answers a CancelBoost while already Inactive by itself, so this host is
 * never woken for that case.
 *
 * THE THERMOSTAT CACHE SEEDS FROM THE C6'S OWN DEFAULTS, a reviewer-
 * mandated carry from this round's firmware Task 3: OccupiedHeatingSetpoint
 * 2000 and SystemMode 1 (Auto) are esp-matter's construction defaults
 * (esp_matter_feature.h's heating config "occupied_heating_setpoint(2000)",
 * esp_matter_cluster.h's thermostat config "system_mode(1)"; the firmware
 * thunk overrides neither), so seeding anything else re-creates the
 * first-write-swallow defect: an unchanged-value write suppressed by a
 * cache that does not match the device. LocalTemperature has no non-null
 * boot value to seed from (the config default is null), so it carries a
 * has-flag instead: the FIRST setLocalTemperature() always writes, even
 * at the cache's own seeded 2000, the same principle expressed the
 * null-until-pushed way. Unlike MatterThermostat's helper, the heating
 * setpoint setter enforces NO 7..30 C limit: those numbers are the
 * thermostat cluster server's OPTIONAL limit attributes, which this
 * cluster instance does not create, and a water heater legitimately runs
 * to 60-80 C.
 *
 * NAMING where the reused surfaces would collide: the thermostat helpers
 * keep MatterThermostat's own names (setMode/getMode/onChangeMode,
 * setHeatingSetpoint, setLocalTemperature, onChange*), and the ModeBase
 * mode-change surface takes the cluster-qualified names
 * (onChangeWaterHeaterMode/getCurrentWaterHeaterMode) exactly the way
 * MatterRoboticVacuum qualified onChangeRunMode/onChangeCleanMode when
 * one class carried several mode surfaces. Both precedents are reused
 * unchanged; only the collision (one class, two "onChangeMode" meanings)
 * is resolved, and it is resolved the way the library already resolved it
 * once. CurrentMode on WaterHeaterMode follows the 0.6.0 Instance-served
 * rule (S3.20.1): no +MTATTR URC ever fires for it, so the cache updates
 * only on an onChangeWaterHeaterMode() allow verdict (B196: a same-mode
 * ChangeToMode short-circuits firmware-side and never reaches this host).
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"
#include "HearthMeasurementPush.h"

class MatterWaterHeater : public MatterEndPoint {
public:
  // The 0x050F variant byte (AT_MT_SPEC.md S3.9): FULL is the conformant
  // composition (EnergyManagement|TankPercent features plus the electrical
  // sensor graft), MINIMAL the disclosed sub-conformant SDK-bare build
  // (HeaterTypes/HeatDemand/BoostState only, no graft).
  enum Variant_t {
    FULL = 0,
    MINIMAL = 1
  };

  // The heating-only subset of the Thermostat cluster's SystemModeEnum,
  // the same class-scoped transcription MatterThermostat carries (values
  // identical, gaps included: kCool is 3 and absent here because the
  // cluster is created with the heating feature only). AUTO is the C6's
  // own boot default (esp_matter_cluster.h: "system_mode(1)").
  enum ThermostatMode_t {
    THERMOSTAT_MODE_OFF = 0,
    THERMOSTAT_MODE_AUTO = 1,
    THERMOSTAT_MODE_HEAT = 4,
    THERMOSTAT_MODE_EMERGENCY_HEAT = 5
  };

  // The unpacked Boost command (S3.17's mask encoding, see the header
  // comment): has-flags per optional, values only meaningful when the
  // matching flag is set. setpoint is Matter temperature hundredths.
  struct BoostInfo {
    uint32_t duration;
    bool hasOneShot, oneShot, hasEmergency, emergency, hasSetpoint;
    int16_t setpoint;
    bool hasTargetPct;
    uint8_t targetPct;
    bool hasReheat;
    uint8_t reheat;
  };

  MatterWaterHeater();
  ~MatterWaterHeater();

  // declares the endpoint only (device type 0x050F plus the variant byte)
  // and seeds every cache, the thermostat ones from the C6's own cluster
  // defaults (see the header comment). No wire traffic.
  bool begin(Variant_t variant = FULL);
  // this will stop processing Water Heater Matter events
  void end();

  // WaterHeaterManagement pushes onto the 0x94 field table (S3.25), one
  // AT+MTMEAS line each: null-until-pushed has-flag discipline, cache on
  // success only. The two bitmaps take WaterHeaterHeatSourceBitmap values
  // (defined bits 0x01..0x10); range enforcement is the firmware's,
  // answered +MTERR:1.
  bool setHeaterTypes(uint8_t bitmap);
  bool setHeatDemand(uint8_t bitmap);
  // FULL only (EnergyManagement / TankPercent features): on MINIMAL these
  // three refuse host-side with Hearth error 1 and zero wire traffic.
  // EstimatedHeatRequired is int64 mWh with an XML minimum of 0: a
  // negative value travels signed and is refused by the firmware (+MTERR:1),
  // the 64-bit pipeline's signedness-first rule.
  bool setTankVolume(uint16_t litres);
  bool setEstimatedHeatRequired(int64_t mwh);
  bool setTankPercentage(uint8_t pct);

  // register the host's verdict for a controller-invoked Boost /
  // CancelBoost on WaterHeaterManagement (S3.17). No callback registered
  // denies by default (fail closed); the verdict IS the wire response, and
  // an accept is followed by this class's own BoostState push (see the
  // header comment's verdict-then-push section).
  void onBoost(std::function<bool(const BoostInfo &)> cb);
  void onCancelBoost(std::function<bool()> cb);
  // end a boost from the sketch's own timer: pushes BoostState Inactive
  // directly (the firmware derives BoostEnded). Call from loop(), never
  // from inside a verdict callback (a wire write there is refused).
  bool endBoost();

  // replace WaterHeaterMode's (158) SupportedModes list (S3.20.1): 1..8
  // mode/tag/label triples, each mode 0..255 unique within the call, each
  // tag a bare u16 (0 = kManual 0x4001, this cluster's tag-0 default on
  // every mode), each label 1..32 bytes of printable ASCII with no '"'.
  // Re-sent automatically on every later reconcile.
  bool setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);
  // register the host's verdict for a controller-invoked ChangeToMode on
  // WaterHeaterMode (S3.17/S3.20.1); the callback's argument is the
  // requested mode. Cluster-qualified name: see the header comment's
  // naming section. The cache updates only on an allow.
  void onChangeWaterHeaterMode(std::function<bool(uint8_t)> cb);
  // cached WaterHeaterMode CurrentMode; no wire round trip (S3.20.1's
  // Instance-served rule), updated only on an allowed ChangeToMode.
  uint8_t getCurrentWaterHeaterMode();

  // ---- the MatterThermostat attribute helpers, heating-only subset ----
  // (ember-served, ordinary AT+MTATTR both directions; see the header
  // comment for the seed values and the deliberate absence of the 7..30 C
  // limit check)

  // set the thermostat SystemMode; no-op when unchanged, refused for a
  // value outside ThermostatMode_t
  bool setMode(ThermostatMode_t mode);
  ThermostatMode_t getMode() {
    return currentMode;
  }

  // set the heating setpoint in Celsius degrees
  bool setHeatingSetpoint(double setpointHeatingTemperature);
  // get the heating setpoint in Celsius degrees
  double getHeatingSetpoint() {
    return heatingSetpointTemperature / 100.0;
  }

  // push the measured water temperature in Celsius degrees (the sketch is
  // the temperature source). First call always writes: the fabric-side
  // value starts null (see the header comment).
  bool setLocalTemperature(double temperature);
  // returns the local temperature with 1/100th of a degree precision
  double getLocalTemperature() {
    return (double)localTemperature / 100.0;
  }

  // User Callbacks for controller-driven Thermostat changes, the
  // MatterThermostat shapes: fired from +MTATTR URCs.
  using EndPointModeCB = std::function<bool(ThermostatMode_t)>;
  void onChangeMode(EndPointModeCB onChangeCB) {
    _onChangeModeCB = onChangeCB;
  }
  using EndPointTemperatureCB = std::function<bool(float)>;
  void onChangeLocalTemperature(EndPointTemperatureCB onChangeCB) {
    _onChangeTemperatureCB = onChangeCB;
  }
  using EndPointHeatingSetpointCB = std::function<bool(double)>;
  void onChangeHeatingSetpoint(EndPointHeatingSetpointCB onChangeCB) {
    _onChangeHeatingSetpointCB = onChangeCB;
  }
  using EndPointCB = std::function<bool(void)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  // ---- the measurement-push surface (FULL only), the shared helper ----
  // (the electrical sensor's exact semantics; on MINIMAL every one of
  // these refuses host-side with Hearth error 1 and zero wire traffic)
  bool setVoltage(int64_t mv);
  bool setActiveCurrent(int64_t ma);
  bool setActivePower(int64_t mw);
  bool setFrequency(int64_t mhz);
  bool pushMeasurements(int64_t mv, int64_t ma, int64_t mw);
  bool addEnergyImported(uint64_t mwh);
  bool addEnergyExported(uint64_t mwh);
  int64_t getVoltage();
  int64_t getActiveCurrent();
  int64_t getActivePower();
  int64_t getFrequency();
  uint64_t getEnergyImported();
  uint64_t getEnergyExported();

  // this function is called by Matter internal event processor. Thermostat
  // attributes (cluster 0x0201) drive the onChange* callbacks; cluster 148
  // is a documented ignore (Instance-served, S3.25: an injected +MTATTR
  // naming it must not move any cache).
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  // Hearth's own addition (MatterEndPoint.h): adjudicates Boost (148,0),
  // CancelBoost (148,1) and ChangeToMode (158,0); everything else defers
  // to the fail-closed base default.
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  // clang-format off
  /* Wire constants. The 0x94 field ids and the Boost mask bits are
   * mirrored from the firmware's main/include/mt_matter.h (the MT_WHM_F_*,
   * MT_BOOST_P_* and MT_BOOST_V_* defines, read from that source, not
   * transcribed from memory); the wire grammar is AT_MT_SPEC.md S3.25 (field table)
   * and S3.17 (Boost payload). Cluster/attribute/command ids follow the
   * library's usual plain-integer pattern: there is no esp-matter or
   * connectedhomeip header on a host build to pull named constants from. */
  static const uint32_t kWhmClusterId            = 0x0094;  // 148, WaterHeaterManagement
  static const uint8_t  kFieldHeaterTypes        = 0;       // MT_WHM_F_HEATER_TYPES, bitmap8
  static const uint8_t  kFieldHeatDemand         = 1;       // MT_WHM_F_HEAT_DEMAND, bitmap8
  static const uint8_t  kFieldBoostState         = 2;       // MT_WHM_F_BOOST_STATE, enum8 0/1
  static const uint8_t  kFieldTankVolume         = 3;       // MT_WHM_F_TANK_VOLUME, u16, EM feature
  static const uint8_t  kFieldEstHeatRequired    = 4;       // MT_WHM_F_EST_HEAT_REQ, int64 mWh, EM feature
  static const uint8_t  kFieldTankPercentage     = 5;       // MT_WHM_F_TANK_PERCENT, u8 0-100, TP feature
  static const uint32_t kBoostCommandId          = 0;       // Boost
  static const uint32_t kCancelBoostCommandId    = 1;       // CancelBoost
  static const uint16_t kBoostPresentOneShot     = 0x0001;  // MT_BOOST_P_ONESHOT
  static const uint16_t kBoostPresentEmergency   = 0x0002;  // MT_BOOST_P_EMERGENCY
  static const uint16_t kBoostPresentSetpoint    = 0x0004;  // MT_BOOST_P_SETPOINT, appended
  static const uint16_t kBoostPresentTargetPct   = 0x0008;  // MT_BOOST_P_TARGET_PCT, appended
  static const uint16_t kBoostPresentReheat      = 0x0010;  // MT_BOOST_P_REHEAT, appended
  static const uint16_t kBoostValueOneShot       = 0x0100;  // MT_BOOST_V_ONESHOT
  static const uint16_t kBoostValueEmergency     = 0x0200;  // MT_BOOST_V_EMERGENCY
  static const uint32_t kWaterHeaterModeClusterId = 0x009E; // 158, WaterHeaterMode
  static const uint32_t kChangeToModeCommandId   = 0;       // ModeBase ChangeToMode
  static const uint32_t kThermostatClusterId     = 0x0201;  // 513, Thermostat (ember-served)
  static const uint32_t kLocalTemperatureAttrId  = 0x0000;
  static const uint32_t kHeatingSetpointAttrId   = 0x0012;  // OccupiedHeatingSetpoint
  static const uint32_t kSystemModeAttrId        = 0x001C;
  static const uint8_t  kMaxModes    = 8;   // AT_MT_SPEC.md S3.20.1: 1..8 triples
  static const uint8_t  kMaxLabelLen = 32;  // S3.20.1: 1..32 printable ASCII bytes
  // clang-format on

  // the deferred BoostState push an accepted Boost/CancelBoost arms (see
  // the header comment's verdict-then-push section)
  enum BoostPush_t {
    kBoostPushNone = 0,
    kBoostPushActive,
    kBoostPushInactive
  };

  bool started = false;
  Variant_t variantSel = FULL;

  // WHM caches with the null-until-pushed has-flags
  uint8_t heaterTypes = 0;
  uint8_t heatDemand = 0;
  uint16_t tankVolume = 0;
  int64_t estHeatRequired = 0;
  uint8_t tankPercentage = 0;
  bool hasHeaterTypes = false;
  bool hasHeatDemand = false;
  bool hasTankVolume = false;
  bool hasEstHeatRequired = false;
  bool hasTankPercentage = false;

  uint8_t pendingBoostPush = kBoostPushNone;

  // WaterHeaterMode list cache (re-sent on reconcile) and CurrentMode
  uint8_t modes[kMaxModes];
  uint16_t tags[kMaxModes];
  char labels[kMaxModes][kMaxLabelLen + 1];  // +1 for the terminating NUL
  uint8_t modesCount = 0;
  uint8_t currentWaterHeaterMode = 0;

  // thermostat caches, seeded from the C6's own defaults at begin()
  int16_t heatingSetpointTemperature = 2000;
  int16_t localTemperature = 2000;
  bool hasLocalTemperature = false;
  ThermostatMode_t currentMode = THERMOSTAT_MODE_AUTO;

  std::function<bool(const BoostInfo &)> _onBoostCB = nullptr;
  std::function<bool()> _onCancelBoostCB = nullptr;
  std::function<bool(uint8_t)> _onChangeWaterHeaterModeCB = nullptr;
  EndPointModeCB _onChangeModeCB = nullptr;
  EndPointTemperatureCB _onChangeTemperatureCB = nullptr;
  EndPointHeatingSetpointCB _onChangeHeatingSetpointCB = nullptr;
  EndPointCB _onChangeCB = nullptr;

  // one "AT+MTMEAS=<ep>,148,<field>,<value>" line; wire-only, the caller
  // commits the cache (house discipline: a failed write must not update it)
  bool hearthSendWhmPair(uint8_t field, int64_t value);
  // the shared has-flag setter shape behind the five WHM field setters
  bool hearthSetWhmField(uint8_t field, int64_t value, int64_t *cache, bool *hasFlag);
  // true when the call was refused for the MINIMAL variant (error 1 set)
  bool hearthRefusedOnMinimal();
  // "AT+MTMODES=<ep>,158,..." for exactly the triples given; wire-only
  bool hearthSendModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);
  // the shared thermostat write shape (updateAttributeVal, cache on success)
  bool hearthWriteThermostatInt16(uint32_t attribute_id, int16_t raw, int16_t *cache);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile: re-pushes
  // the configuration half (HeaterTypes, TankVolume, the mode list),
  // clears the volatile has-flags without re-sending (B229), and delegates
  // the electrical fields to meas.onReconciled(). See the header comment.
  void hearthOnReconciled() override;

  // Hearth's own hook (MatterEndPoint.h, this round): sends the BoostState
  // push an accepted Boost/CancelBoost deferred behind its verdict.
  void hearthOnDeferredWork() override;

  // the embedded measurement-push surface (design spec 4.1); begin()
  // resets it and sets `enabled` explicitly from the variant
  HearthMeasurementPush meas;
};
