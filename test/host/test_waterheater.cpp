/*
 * test_waterheater.cpp - Task 6 (energy round B): MatterWaterHeater
 * (0x050F), the first WaterHeaterManagement (0x0094, 148) consumer of
 * AT+MTMEAS (AT_MT_SPEC.md S3.25) and the first five-field +MTCMD consumer
 * (S3.17's Boost payload: duration, presence mask, up to three appended
 * numeric optionals).
 *
 * What this suite pins:
 *
 * - Declaration, both variants (FULL 0 / MINIMAL 1), the variant riding
 *   the AT+MTEP? reply's fourth field.
 * - Exact AT+MTMEAS wire lines for every 0x94 setter, including an
 *   EstimatedHeatRequired above 2^32 (the 64-bit pipeline seam).
 * - MINIMAL refusals: the tank trio (TankVolume, EstimatedHeatRequired,
 *   TankPercentage) and the WHOLE measurement surface refuse HOST-side
 *   with error 1 and zero wire traffic (the POWER_ONLY precedent; the
 *   variant builds neither the EnergyManagement/TankPercent features nor
 *   the electrical sensor graft, S3.25/S3.9).
 * - Boost dispatch: the packed presence mask unpacks into BoostInfo
 *   (mask bits 0-4 presence in canonical order, bits 8-9 the two bool
 *   VALUES; appended fields only for the present numeric optionals,
 *   canonical order, AT_MT_SPEC.md S3.17 and the firmware's
 *   main/include/mt_matter.h MT_BOOST_* constants). An accepting verdict
 *   answers AT+MTCMDRESP=<seq>,1 FOLLOWED by the library's own BoostState
 *   Active push (which is what fires the firmware-derived BoostStarted
 *   event); a deny answers verdict 0 and pushes NOTHING.
 * - CancelBoost: same adjudication, Inactive push on acceptance.
 * - The reconcile split, both halves: HeaterTypes and TankVolume are
 *   configuration the C6 does not persist and are RE-pushed (the
 *   cabinet-labels precedent); HeatDemand, BoostState, TankPercentage and
 *   EstimatedHeatRequired are volatile and follow B229 (wire-pushed
 *   memory cleared, values not re-sent); the embedded measurement helper
 *   keeps its own B229 semantics.
 * - An injected +MTATTR naming cluster 148 must not move any cache
 *   (S3.25's Instance-served rule), while ember-served Thermostat URCs
 *   (cluster 0x0201) DO drive the thermostat callbacks.
 * - The Task 3 ledger contract: the thermostat cache seeds from the C6's
 *   own cluster defaults (OccupiedHeatingSetpoint 2000, SystemMode 1) at
 *   begin(), so an unchanged-value first write is a TRUE no-op, not a
 *   swallowed write against a device that holds a different value.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterWaterHeater.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterWaterHeater &dev,
                    MatterWaterHeater::Variant_t v = MatterWaterHeater::FULL) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterWaterHeater::FULL ? "+MTEP:0,1,0x050F\r\nOK\r\n" : "+MTEP:0,1,0x050F,1\r\nOK\r\n");
  Matter.begin();
}

/* The override is protected (the sibling-class convention); the base's
 * public virtual is the dispatch surface Hearth.cpp itself uses. */
static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

/* ===== declaration, both variants ===== */

static void test_begin_declares_0x050F_both_variants(void) {
  {
    MockStream s; MatterWaterHeater dev;
    bringUp(s, dev);
    check("declared as device type 0x050F", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x050F);
    check("FULL declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
    check("adopted endpoint 1", dev.getEndPointId() == 1);
    check("begin() issued no AT traffic beyond the declaration", s.scriptDrained());
    check("no unexpected commands", s.unexpected().empty());
    check("heating setpoint cache seeds 20.00C (the C6's own default 2000)",
          dev.getHeatingSetpoint() > 19.99 && dev.getHeatingSetpoint() < 20.01);
    check("system mode cache seeds AUTO (the C6's own default 1)", dev.getMode() == MatterWaterHeater::THERMOSTAT_MODE_AUTO);
    check("current WaterHeaterMode cache starts 0", dev.getCurrentWaterHeaterMode() == 0);
    check("measurement caches start 0", dev.getVoltage() == 0 && dev.getEnergyImported() == 0);
  }
  {
    MockStream s; MatterWaterHeater dev;
    bringUp(s, dev, MatterWaterHeater::MINIMAL);
    check("MINIMAL still declares 0x050F", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x050F);
    check("MINIMAL declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
    check("no unexpected commands (MINIMAL)", s.unexpected().empty());
  }
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setter_before_begin_and_before_reconcile(void) {
  MockStream s; MatterWaterHeater dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setHeaterTypes() before begin() fails", !dev.setHeaterTypes(4));
  check("begin() declares", dev.begin());
  check("setHeaterTypes() before reconcile fails", !dev.setHeaterTypes(4));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== 0x94 setters: exact wire pins ===== */

static void test_whm_setters_exact_wire_pins(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  check("setHeaterTypes(4) sends field 0", dev.setHeaterTypes(4));
  s.expect("AT+MTMEAS=1,148,1,4", "OK\r\n");
  check("setHeatDemand(4) sends field 1", dev.setHeatDemand(4));
  s.expect("AT+MTMEAS=1,148,3,200", "OK\r\n");
  check("setTankVolume(200) sends field 3", dev.setTankVolume(200));
  s.expect("AT+MTMEAS=1,148,5,60", "OK\r\n");
  check("setTankPercentage(60) sends field 5", dev.setTankPercentage(60));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The 64-bit seam: EstimatedHeatRequired is int64 mWh on the wire and must
 * survive past 2^32. 5000000000 truncates to 705032704 on a 32-bit
 * pipeline. */
static void test_estimated_heat_required_past_2_to_32(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,148,4,5000000000", "OK\r\n");
  check("setEstimatedHeatRequired(5000000000) survives past 2^32", dev.setEstimatedHeatRequired(5000000000LL));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_whm_setter_noop_on_unchanged_and_failed_write(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  check("first setHeaterTypes(4) writes", dev.setHeaterTypes(4));
  check("repeat setHeaterTypes(4) is a no-op", dev.setHeaterTypes(4));
  check("no second wire line", s.scriptDrained());
  /* a first push of 0 is a real change: the fabric-side default is served
   * from the delegate but the host has never confirmed it (the electrical
   * null-until-pushed discipline applied to the delegate defaults) */
  s.expect("AT+MTMEAS=1,148,1,0", "OK\r\n");
  check("a first setHeatDemand(0) still writes", dev.setHeatDemand(0));
  s.expect("AT+MTMEAS=1,148,1,2", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !dev.setHeatDemand(2));
  check("cache untouched: repeating the last accepted value is a no-op", dev.setHeatDemand(0));
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== MINIMAL refusals: error 1, zero wire traffic ===== */

static void test_minimal_refuses_tank_trio_and_measurement_surface(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev, MatterWaterHeater::MINIMAL);
  Hearth.hearthSetError(0);
  check("setTankVolume() refused on MINIMAL", !dev.setTankVolume(200));
  check("with error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setEstimatedHeatRequired() refused on MINIMAL", !dev.setEstimatedHeatRequired(1000));
  check("with error 1 too", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setTankPercentage() refused on MINIMAL", !dev.setTankPercentage(50));
  check("with error 1 as well", Hearth.lastError() == 1);
  /* the WHOLE measurement surface refuses on MINIMAL: variant 1 has no
   * electrical sensor graft at all, so the answer is known host-side */
  Hearth.hearthSetError(0);
  check("setVoltage() refused on MINIMAL", !dev.setVoltage(230000));
  check("with error 1 (voltage)", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setActiveCurrent() refused on MINIMAL", !dev.setActiveCurrent(433));
  check("setActivePower() refused on MINIMAL", !dev.setActivePower(99590));
  check("setFrequency() refused on MINIMAL", !dev.setFrequency(50000));
  check("pushMeasurements() refused on MINIMAL", !dev.pushMeasurements(1, 2, 3));
  check("addEnergyImported() refused on MINIMAL", !dev.addEnergyImported(1000));
  check("addEnergyExported() refused on MINIMAL", !dev.addEnergyExported(1000));
  check("with error 1 (the last refusal)", Hearth.lastError() == 1);
  check("zero wire traffic for the whole batch", s.scriptDrained() && s.unexpected().empty());
  /* the ungated fields still work on MINIMAL, that is the variant's point */
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  check("setHeaterTypes() still writes on MINIMAL", dev.setHeaterTypes(4));
  s.expect("AT+MTMEAS=1,148,1,4", "OK\r\n");
  check("setHeatDemand() still writes on MINIMAL", dev.setHeatDemand(4));
  check("script drained", s.scriptDrained());
}

/* ===== the measurement surface on FULL (delegated helper) ===== */

static void test_full_measurement_surface_delegates(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("setVoltage(230000) sends the electrical wire pin", dev.setVoltage(230000));
  check("voltage cache updated", dev.getVoltage() == 230000);
  s.expect("AT+MTMEAS=1,144,0,231000,1,440,2,101000", "OK\r\n");
  check("pushMeasurements() batches one three-pair line", dev.pushMeasurements(231000, 440, 101000));
  check("getters read the batch", dev.getActiveCurrent() == 440 && dev.getActivePower() == 101000);
  s.expect("AT+MTMEAS=1,145,0,5000000000", "OK\r\n");
  check("addEnergyImported(5000000000) accumulates past 2^32", dev.addEnergyImported(5000000000ULL));
  check("imported accumulator carries the full value", dev.getEnergyImported() == 5000000000ULL);
  s.expect("AT+MTMEAS=1,145,1,20000", "OK\r\n");
  check("addEnergyExported(20000) pushes field 1", dev.addEnergyExported(20000));
  check("frequency getter still 0 (never pushed)", dev.getFrequency() == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== Boost dispatch: the packed mask, verdict, and Active push ===== */

static void test_boost_accept_unpacks_mask_and_pushes_active(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  MatterWaterHeater::BoostInfo seen = {};
  int calls = 0;
  dev.onBoost([&](const MatterWaterHeater::BoostInfo &info) {
    calls++;
    seen = info;
    return true;
  });
  /* the spec's own worked example: duration 3600, oneShot true,
   * targetPercentage 80: mask = bit0|bit3|bit8 = 265 (AT_MT_SPEC.md
   * S3.17), one appended value. */
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.expect("AT+MTMEAS=1,148,2,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,148,0,3600,265,80");
  Hearth.poll();
  check("the boost callback ran exactly once", calls == 1);
  check("duration is 3600", seen.duration == 3600);
  check("oneShot present and true", seen.hasOneShot && seen.oneShot);
  check("emergencyBoost absent", !seen.hasEmergency && !seen.emergency);
  check("temporarySetpoint absent", !seen.hasSetpoint);
  check("targetPercentage present and 80", seen.hasTargetPct && seen.targetPct == 80);
  check("targetReheat absent", !seen.hasReheat);
  check("accept answered AT+MTCMDRESP=7,1 FOLLOWED by the BoostState Active push", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* All three numeric optionals present at once: the five-field tail
 * (duration, mask, setpoint, targetPct, reheat), the wire form that forced
 * the +MTCMD parse window from four positions to five this round. Mask:
 * bit0 oneShot present, bit8 its value, bits 2|3|4 the three appended
 * numerics = 0x011D = 285. */
static void test_boost_five_field_tail_all_numerics(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  MatterWaterHeater::BoostInfo seen = {};
  dev.onBoost([&](const MatterWaterHeater::BoostInfo &info) {
    seen = info;
    return true;
  });
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.expect("AT+MTMEAS=1,148,2,1", "OK\r\n");
  s.injectURC("+MTCMD:9,1,148,0,7200,285,5500,80,60");
  Hearth.poll();
  check("duration is 7200", seen.duration == 7200);
  check("oneShot present and true", seen.hasOneShot && seen.oneShot);
  check("temporarySetpoint present and 5500 (55.00C hundredths)", seen.hasSetpoint && seen.setpoint == 5500);
  check("targetPercentage present and 80", seen.hasTargetPct && seen.targetPct == 80);
  check("targetReheat present and 60 (the fifth tail field)", seen.hasReheat && seen.reheat == 60);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_boost_deny_sends_verdict_0_and_no_push(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  int calls = 0;
  dev.onBoost([&](const MatterWaterHeater::BoostInfo &) {
    calls++;
    return false;
  });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,148,0,3600,265,80");
  Hearth.poll();
  check("the boost callback ran", calls == 1);
  check("deny answered AT+MTCMDRESP=8,0 and pushed NOTHING", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_boost_without_callback_denies(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTCMDRESP=12,0", "OK\r\n");
  s.injectURC("+MTCMD:12,1,148,0,3600,265,80");
  Hearth.poll();
  check("no registered callback denies by default, no push", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A tail shorter than the mask promises is malformed: denied fail-closed
 * WITHOUT consulting the callback (the mask is the authority on what
 * follows it). */
static void test_boost_mask_tail_mismatch_denies_without_callback(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  int calls = 0;
  dev.onBoost([&](const MatterWaterHeater::BoostInfo &) {
    calls++;
    return true;
  });
  s.expect("AT+MTCMDRESP=13,0", "OK\r\n");
  s.injectURC("+MTCMD:13,1,148,0,3600,28,5500"); /* mask promises three appended, only one came */
  Hearth.poll();
  check("the callback was never consulted", calls == 0);
  check("the mismatch denied with verdict 0 and no push", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== CancelBoost: same shape, Inactive push on acceptance ===== */

static void test_cancel_boost_accept_pushes_inactive(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  int calls = 0;
  dev.onCancelBoost([&]() {
    calls++;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.expect("AT+MTMEAS=1,148,2,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,148,1");
  Hearth.poll();
  check("the cancel callback ran exactly once", calls == 1);
  check("accept answered AT+MTCMDRESP=7,1 followed by the Inactive push", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cancel_boost_deny_no_push(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  dev.onCancelBoost([]() {
    return false;
  });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,148,1");
  Hearth.poll();
  check("deny answered verdict 0 and pushed nothing", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== endBoost: the sketch's own timer path ===== */

static void test_end_boost_pushes_inactive_directly(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,148,2,0", "OK\r\n");
  check("endBoost() pushes BoostState Inactive", dev.endBoost());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_end_boost_before_begin_fails(void) {
  MockStream s; MatterWaterHeater dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("endBoost() before begin() fails", !dev.endBoost());
  check("no traffic issued", s.unexpected().empty());
}

/* ===== WaterHeaterMode: the ModeBase surface on cluster 158 ===== */

static void test_supported_modes_wire_pin_and_grammar(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  const uint8_t modes[2] = {1, 2};
  const uint16_t tags[2] = {0, 0};
  const char *labels[2] = {"Eco", "Boost"};
  s.expect("AT+MTMODES=1,158,1,0,\"Eco\",2,0,\"Boost\"", "OK\r\n");
  check("setSupportedModes() sends the cluster-aware form on 158", dev.setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  /* host-side grammar refusals, the RVC discipline: error 1, no wire */
  Hearth.hearthSetError(0);
  const uint8_t dup[2] = {1, 1};
  check("a repeated mode value is refused host-side", !dev.setSupportedModes(dup, tags, labels, 2));
  check("with error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  const char *badLabels[2] = {"Eco", "Bo\"ost"};
  check("a label with '\"' is refused host-side", !dev.setSupportedModes(modes, tags, badLabels, 2));
  check("with error 1 too", Hearth.lastError() == 1);
  check("zero wire traffic for the refusals", s.scriptDrained() && s.unexpected().empty());
}

static void test_change_water_heater_mode_dispatch(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  int seen = -1;
  dev.onChangeWaterHeaterMode([&](uint8_t mode) {
    seen = mode;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,158,0,2");
  Hearth.poll();
  check("ChangeToMode delivered the requested mode", seen == 2);
  check("the allow verdict answered AT+MTCMDRESP=7,1 (no push: ModeBase has none)", s.scriptDrained());
  check("the mode cache updated on the allow", dev.getCurrentWaterHeaterMode() == 2);
  /* a deny leaves the cache alone, matching the device's own CurrentMode */
  dev.onChangeWaterHeaterMode([&](uint8_t) {
    return false;
  });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,158,0,3");
  Hearth.poll();
  check("a denied ChangeToMode leaves the cache untouched", dev.getCurrentWaterHeaterMode() == 2);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== thermostat helpers: ember-served, AT+MTATTR both directions ===== */

static void test_heating_setpoint_seed_and_write(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  /* THE LEDGER PIN (Task 3 contract): the cache seeds from the C6's own
   * esp-matter default 2000, so setHeatingSetpoint(20.0) is a TRUE no-op.
   * Seeded wrong (upstream's 1600), this would be a swallowed write
   * against a device really holding 2000. */
  check("setHeatingSetpoint(20.0) against the seeded cache is a no-op", dev.setHeatingSetpoint(20.0));
  check("and issued no traffic", s.scriptDrained());
  s.expect("AT+MTATTR=1,513,18,5500,1", "+MTATTR:1,513,18,5500\r\nOK\r\n");
  check("setHeatingSetpoint(55.0) writes attr 0x12 (a water heater runs past 30C)", dev.setHeatingSetpoint(55.0));
  check("getter reads back 55.0", dev.getHeatingSetpoint() > 54.99 && dev.getHeatingSetpoint() < 55.01);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_system_mode_seed_and_write(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  check("setMode(AUTO) against the seeded cache is a no-op", dev.setMode(MatterWaterHeater::THERMOSTAT_MODE_AUTO));
  check("and issued no traffic", s.scriptDrained());
  s.expect("AT+MTATTR=1,513,28,0,1", "+MTATTR:1,513,28,0\r\nOK\r\n");
  check("setMode(OFF) writes SystemMode", dev.setMode(MatterWaterHeater::THERMOSTAT_MODE_OFF));
  check("mode cached", dev.getMode() == MatterWaterHeater::THERMOSTAT_MODE_OFF);
  s.expect("AT+MTATTR=1,513,28,4,1", "+MTATTR:1,513,28,4\r\nOK\r\n");
  check("setMode(HEAT) writes SystemMode", dev.setMode(MatterWaterHeater::THERMOSTAT_MODE_HEAT));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* LocalTemperature starts NULL on the fabric (esp-matter's nullable
 * default), so the first push must reach the wire even at the cache's
 * seeded value: the null-until-pushed discipline, the same first-write-
 * swallow principle as the setpoint seed, applied to a field with no
 * non-null boot value to seed from. */
static void test_local_temperature_first_push_of_seed_value_writes(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTATTR=1,513,0,2000,1", "+MTATTR:1,513,0,2000\r\nOK\r\n");
  check("a first setLocalTemperature(20.0) writes despite the seeded cache", dev.setLocalTemperature(20.0));
  check("a repeat setLocalTemperature(20.0) after the push is a no-op", dev.setLocalTemperature(20.0));
  check("exactly one wire line", s.scriptDrained());
  s.expect("AT+MTATTR=1,513,0,4550,1", "+MTATTR:1,513,0,4550\r\nOK\r\n");
  check("setLocalTemperature(45.5) writes the tank temperature", dev.setLocalTemperature(45.5));
  check("getter reads back 45.5", dev.getLocalTemperature() > 45.49 && dev.getLocalTemperature() < 45.51);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_thermostat_urcs_drive_callbacks(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  double seenSetpoint = 0;
  float seenTemp = 0;
  int seenMode = -1, anyChanges = 0;
  dev.onChangeHeatingSetpoint([&](double c) {
    seenSetpoint = c;
    return true;
  });
  dev.onChangeLocalTemperature([&](float c) {
    seenTemp = c;
    return true;
  });
  dev.onChangeMode([&](MatterWaterHeater::ThermostatMode_t m) {
    seenMode = (int)m;
    return true;
  });
  dev.onChange([&]() {
    anyChanges++;
    return true;
  });
  s.injectURC("+MTATTR:1,513,18,5500");
  s.injectURC("+MTATTR:1,513,0,4820");
  s.injectURC("+MTATTR:1,513,28,4");
  Hearth.poll();
  check("controller setpoint write drove onChangeHeatingSetpoint(55.0)", seenSetpoint > 54.99 && seenSetpoint < 55.01);
  check("and the setpoint cache", dev.getHeatingSetpoint() > 54.99 && dev.getHeatingSetpoint() < 55.01);
  check("local temperature URC drove onChangeLocalTemperature(48.2)", seenTemp > 48.19f && seenTemp < 48.21f);
  check("SystemMode URC drove onChangeMode(HEAT)", seenMode == 4);
  check("and the mode cache", dev.getMode() == MatterWaterHeater::THERMOSTAT_MODE_HEAT);
  check("the generic onChange fired for all three", anyChanges == 3);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rejecting_callback_leaves_cache(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  dev.onChangeHeatingSetpoint([](double) {
    return false;
  });
  s.injectURC("+MTATTR:1,513,18,1800");
  Hearth.poll();
  check("a rejecting setpoint callback leaves the cache at the seed", dev.getHeatingSetpoint() > 19.99 && dev.getHeatingSetpoint() < 20.01);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the Instance-served rule: cluster 148 +MTATTR must not move
 * anything, while cluster 513 (above) does ===== */

static void test_injected_mtattr_on_cluster_148_ignored(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  check("baseline setHeaterTypes(4)", dev.setHeaterTypes(4));
  s.injectURC("+MTATTR:1,148,0,7");
  Hearth.poll();
  /* if the injected line had moved the cache to 7, this repeat of 4 would
   * write; the no-op is the proof the cache did not move */
  check("repeat setHeaterTypes(4) is still a no-op after the injected URC", dev.setHeaterTypes(4));
  check("no wire traffic beyond the baseline", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the probe: only the +MTCMD path consults the fields override ===== */

class ProbeWaterHeater : public MatterWaterHeater {
public:
  int fieldsCalls = 0;
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    fieldsCalls++;
    return MatterWaterHeater::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }
};

static void test_only_the_cmd_path_consults_the_fields_override(void) {
  MockStream s; ProbeWaterHeater dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x050F\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  dev.setHeaterTypes(4);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  dev.setVoltage(230000);
  s.expect("AT+MTMEAS=1,148,2,0", "OK\r\n");
  dev.endBoost();
  s.injectURC("+MTATTR:1,148,0,7");
  s.injectURC("+MTATTR:1,513,18,1800");
  Hearth.poll();
  check("pushes and +MTATTR URCs never consult the fields override", dev.fieldsCalls == 0);
  dev.onBoost([](const MatterWaterHeater::BoostInfo &) {
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.expect("AT+MTMEAS=1,148,2,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,148,0,3600,265,80");
  Hearth.poll();
  check("a Boost forward consults it exactly once", dev.fieldsCalls == 1);
  /* an unrecognised cluster defers to the fail-closed base default */
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,96,0");
  Hearth.poll();
  check("an unrecognised cluster forward denies via the base default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the reconcile split ===== */

static void test_reconcile_repushes_config_not_volatile(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  check("baseline setHeaterTypes(4)", dev.setHeaterTypes(4));
  s.expect("AT+MTMEAS=1,148,3,200", "OK\r\n");
  check("baseline setTankVolume(200)", dev.setTankVolume(200));
  s.expect("AT+MTMEAS=1,148,1,2", "OK\r\n");
  check("baseline setHeatDemand(2)", dev.setHeatDemand(2));
  s.expect("AT+MTMEAS=1,148,5,60", "OK\r\n");
  check("baseline setTankPercentage(60)", dev.setTankPercentage(60));
  s.expect("AT+MTMEAS=1,148,4,1200000", "OK\r\n");
  check("baseline setEstimatedHeatRequired(1200000)", dev.setEstimatedHeatRequired(1200000));
  /* the split: configuration (HeaterTypes, TankVolume) is RE-pushed, the
   * cabinet-labels precedent; the volatile fields are NOT (B229) */
  s.expect("AT+MTMEAS=1,148,0,4", "OK\r\n");
  s.expect("AT+MTMEAS=1,148,3,200", "OK\r\n");
  reconcile(dev);
  check("reconcile re-pushed HeaterTypes and TankVolume, nothing else", s.scriptDrained());
  check("and nothing unscripted was sent", s.unexpected().empty());
  /* config wire-memory survives the re-push: a repeat is still a no-op */
  check("repeat setHeaterTypes(4) after reconcile is still a no-op", dev.setHeaterTypes(4));
  check("repeat setTankVolume(200) after reconcile is still a no-op", dev.setTankVolume(200));
  /* volatile wire-memory is cleared, values kept: the same value writes
   * again, the B229 shape */
  s.expect("AT+MTMEAS=1,148,1,2", "OK\r\n");
  check("setHeatDemand(2) after reconcile reaches the wire again", dev.setHeatDemand(2));
  s.expect("AT+MTMEAS=1,148,5,60", "OK\r\n");
  check("setTankPercentage(60) after reconcile reaches the wire again", dev.setTankPercentage(60));
  s.expect("AT+MTMEAS=1,148,4,1200000", "OK\r\n");
  check("setEstimatedHeatRequired(1200000) after reconcile reaches the wire again", dev.setEstimatedHeatRequired(1200000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_reconcile_resends_mode_list_and_measurement_b229(void) {
  MockStream s; MatterWaterHeater dev;
  bringUp(s, dev);
  const uint8_t modes[2] = {1, 2};
  const uint16_t tags[2] = {0, 0};
  const char *labels[2] = {"Eco", "Boost"};
  s.expect("AT+MTMODES=1,158,1,0,\"Eco\",2,0,\"Boost\"", "OK\r\n");
  check("baseline mode list", dev.setSupportedModes(modes, tags, labels, 2));
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("baseline setFrequency(50000)", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline addEnergyImported(1500000)", dev.addEnergyImported(1500000));
  /* the mode list is configuration and re-sent; the measurement helper
   * keeps its B229 semantics: nothing re-sent, wire-memory cleared,
   * accumulators preserved */
  s.expect("AT+MTMODES=1,158,1,0,\"Eco\",2,0,\"Boost\"", "OK\r\n");
  reconcile(dev);
  check("reconcile re-sent the mode list and NO measurement", s.scriptDrained());
  check("and nothing unscripted was sent", s.unexpected().empty());
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) after reconcile reaches the wire again", dev.setFrequency(50000));
  check("the energy accumulator survived", dev.getEnergyImported() == 1500000ULL);
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("an add of 0 re-seeds the fabric with the preserved total", dev.addEnergyImported(0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterWaterHeater tests =====\n");
  test_begin_declares_0x050F_both_variants();
  test_rebegin_after_reconcile_refused();
  test_setter_before_begin_and_before_reconcile();
  test_whm_setters_exact_wire_pins();
  test_estimated_heat_required_past_2_to_32();
  test_whm_setter_noop_on_unchanged_and_failed_write();
  test_minimal_refuses_tank_trio_and_measurement_surface();
  test_full_measurement_surface_delegates();
  test_boost_accept_unpacks_mask_and_pushes_active();
  test_boost_five_field_tail_all_numerics();
  test_boost_deny_sends_verdict_0_and_no_push();
  test_boost_without_callback_denies();
  test_boost_mask_tail_mismatch_denies_without_callback();
  test_cancel_boost_accept_pushes_inactive();
  test_cancel_boost_deny_no_push();
  test_end_boost_pushes_inactive_directly();
  test_end_boost_before_begin_fails();
  test_supported_modes_wire_pin_and_grammar();
  test_change_water_heater_mode_dispatch();
  test_heating_setpoint_seed_and_write();
  test_system_mode_seed_and_write();
  test_local_temperature_first_push_of_seed_value_writes();
  test_thermostat_urcs_drive_callbacks();
  test_rejecting_callback_leaves_cache();
  test_injected_mtattr_on_cluster_148_ignored();
  test_only_the_cmd_path_consults_the_fields_override();
  test_reconcile_repushes_config_not_volatile();
  test_reconcile_resends_mode_list_and_measurement_b229();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
