/*
 * test_batterystorage.cpp - Task 6 (energy round C1): MatterBatteryStorage
 * (0x0018), the round's widest class: three surfaces on one endpoint.
 *
 *   - the shared HearthMeasurementPush surface (clusters 144/145), present
 *     on BOTH variants (mt_devtypes.cpp's mk_battery_storage() grafts the
 *     sensor with `energy = true` unconditionally, unlike solar's
 *     `variant == 0`), so the energy adders work even on NO_DEM: pinned
 *     below, since it is the one place this class deliberately differs
 *     from its sibling;
 *   - the seven ember-served PowerSource battery attributes over the
 *     ordinary AT+MTATTR path (design spec 3.5: "no new surface"), with
 *     the nullable/non-nullable seed split the firmware's own creation
 *     calls dictate;
 *   - Task 5's HearthDemControl on the FULL variant only; NO_DEM sets the
 *     helper's `enabled` false, so the WHOLE DEM surface refuses
 *     host-side with error 1 and zero wire traffic (the firmware answers
 *     +MTERR:3, cluster-missing, for that variant: AT_MT_SPEC.md S3.26).
 *
 * The ember reconcile rule is the one this suite pins hardest, and it is a
 * READ-AND-PIN rather than a new invention (design spec 4.2's explicit
 * step). What the read found is that this library has BOTH behaviours,
 * for good reasons: MatterPowerSource and MatterThermostat override
 * hearthOnReconciled() not at all and MatterWaterHeater deliberately skips
 * its thermostat half (right for a resampled value, which self-heals),
 * while MatterTemperatureControlledCabinet::hearthOnReconciled()
 * (.cpp:214-243, inherited by MatterOvenCavity) re-pushes its four
 * TemperatureControl attributes unconditionally because the alternative
 * was BENCH-OBSERVED to leave the device stale forever (.cpp:190-213).
 * This class therefore applies the round's own configuration-versus-
 * sampled split per attribute (the class header lists all seven with the
 * reason each lands where it does), and both halves are pinned here:
 *
 *   - BatCapacity and BatChargeState are CONFIG: re-pushed on reconcile
 *     with exact wire lines, because a nameplate written once at setup()
 *     and a state asserted on rare transitions cannot self-heal;
 *   - the five sampled readings follow B229: nothing re-sent, but the
 *     wire-pushed memory is cleared, so the next identical sample reaches
 *     the wire instead of being suppressed.
 */
#include <stdio.h>
#include <stdint.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterBatteryStorage.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterBatteryStorage &dev, MatterBatteryStorage::Variant_t v = MatterBatteryStorage::FULL) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterBatteryStorage::FULL ? "+MTEP:0,1,0x0018\r\nOK\r\n" : "+MTEP:0,1,0x0018,1\r\nOK\r\n");
  Matter.begin();
}

static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

/* ===== declaration ===== */

static void test_begin_declares_0x0018(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  check("declared as device type 0x0018", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0018);
  check("FULL declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("begin() issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("measurement caches start 0", dev.getVoltage() == 0 && dev.getActivePower() == 0);
}

static void test_begin_no_dem_declares_variant_1(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev, MatterBatteryStorage::NO_DEM);
  check("NO_DEM still declares 0x0018", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0018);
  check("NO_DEM declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_out_of_range_variant_refused(void) {
  MockStream s;
  MatterBatteryStorage dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  Hearth.hearthSetError(0);
  check("a smuggled-in variant 2 is refused", !dev.begin((MatterBatteryStorage::Variant_t)2));
  check("with error 1", Hearth.lastError() == 1);
  check("and nothing was declared", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setters_before_begin_and_before_reconcile(void) {
  MockStream s;
  MatterBatteryStorage dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setBatVoltage() before begin() fails", !dev.setBatVoltage(48000));
  check("setESAType() before begin() fails", !dev.setESAType(5));
  check("setVoltage() before begin() fails", !dev.setVoltage(48000));
  check("begin() declares", dev.begin());
  check("setVoltage() before reconcile fails", !dev.setVoltage(48000));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("setESAType() before reconcile fails", !dev.setESAType(5));
  check("and reports the unaddressable-endpoint code too", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== the measurement surface, both variants ===== */

static void test_measurement_surface_wire_pins(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  check("setVoltage(48000) sends field 0 on cluster 144", dev.setVoltage(48000));
  s.expect("AT+MTMEAS=1,144,1,-25000", "OK\r\n");
  check("setActiveCurrent(-25000) sends field 1 signed (discharging)", dev.setActiveCurrent(-25000));
  s.expect("AT+MTMEAS=1,144,2,5000000000", "OK\r\n");
  check("setActivePower(5000000000) survives past 2^32 (charging)", dev.setActivePower(5000000000LL));
  check("cache carries it", dev.getActivePower() == 5000000000LL);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) sends field 3", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,144,0,48000,1,-25000,2,-1200000", "OK\r\n");
  check("pushMeasurements() emits one three-pair line", dev.pushMeasurements(48000, -25000, -1200000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The one place this class deliberately differs from MatterSolarPower: the
 * firmware grafts EEM on BOTH battery variants (mk_battery_storage() passes
 * `true`, never `variant == 0`), so NO_DEM keeps the energy adders. */
static void test_energy_adders_enabled_on_both_variants(void) {
  {
    MockStream s;
    MatterBatteryStorage dev;
    bringUp(s, dev);
    s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
    check("FULL: addEnergyImported pushes the total", dev.addEnergyImported(1500000));
    s.expect("AT+MTMEAS=1,145,1,900000", "OK\r\n");
    check("FULL: addEnergyExported pushes the total", dev.addEnergyExported(900000));
    check("script drained", s.scriptDrained() && s.unexpected().empty());
  }
  {
    MockStream s;
    MatterBatteryStorage dev;
    bringUp(s, dev, MatterBatteryStorage::NO_DEM);
    s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
    check("NO_DEM: addEnergyImported STILL pushes (EEM is grafted on both variants)", dev.addEnergyImported(1500000));
    s.expect("AT+MTMEAS=1,145,1,900000", "OK\r\n");
    check("NO_DEM: addEnergyExported STILL pushes", dev.addEnergyExported(900000));
    check("accumulators tracked", dev.getEnergyImported() == 1500000ULL && dev.getEnergyExported() == 900000ULL);
    check("script drained", s.scriptDrained() && s.unexpected().empty());
  }
}

/* ===== the seven ember battery attributes (cluster 47) ===== */

static void test_battery_attribute_wire_pins(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);

  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("setBatVoltage(48000) writes attr 0x0B as uint32 mV", dev.setBatVoltage(48000));

  s.expect("AT+MTATTR=1,47,12,150,1", "+MTATTR:1,47,12,150\r\nOK\r\n");
  check("setBatPercentRemaining(150) writes attr 0x0C RAW half-percent (75%)", dev.setBatPercentRemaining(150));

  s.expect("AT+MTATTR=1,47,13,5400,1", "+MTATTR:1,47,13,5400\r\nOK\r\n");
  check("setBatTimeRemaining(5400) writes attr 0x0D as uint32 seconds", dev.setBatTimeRemaining(5400));

  s.expect("AT+MTATTR=1,47,26,1,1", "+MTATTR:1,47,26,1\r\nOK\r\n");
  check("setBatChargeState(1) writes attr 0x1A as enum8 (IsCharging)", dev.setBatChargeState(1));

  s.expect("AT+MTATTR=1,47,29,16000,1", "+MTATTR:1,47,29,16000\r\nOK\r\n");
  check("setBatChargingCurrent(16000) writes attr 0x1D as uint32 mA", dev.setBatChargingCurrent(16000));

  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  check("setBatCapacity(13500000) writes attr 0x18 as uint32 mWh", dev.setBatCapacity(13500000));

  s.expect("AT+MTATTR=1,47,27,7200,1", "+MTATTR:1,47,27,7200\r\nOK\r\n");
  check("setBatTimeToFullCharge(7200) writes attr 0x1B as uint32 seconds", dev.setBatTimeToFullCharge(7200));

  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The five NULLABLE attributes (the firmware creates BatVoltage,
 * BatPercentRemaining, BatTimeRemaining, BatTimeToFullCharge and
 * BatChargingCurrent as nullable<> with no value, mt_devtypes.cpp's
 * mk_battery_storage()) carry a has-flag, so a FIRST push of 0 still
 * writes: the MatterWaterHeater LocalTemperature precedent for a
 * null-defaulted ember attribute. */
static void test_nullable_attributes_first_push_of_zero_writes(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTATTR=1,47,11,0,1", "+MTATTR:1,47,11,0\r\nOK\r\n");
  check("a first setBatVoltage(0) still writes (fabric value is null)", dev.setBatVoltage(0));
  check("a repeat setBatVoltage(0) is a no-op", dev.setBatVoltage(0));
  s.expect("AT+MTATTR=1,47,12,0,1", "+MTATTR:1,47,12,0\r\nOK\r\n");
  check("a first setBatPercentRemaining(0) still writes", dev.setBatPercentRemaining(0));
  s.expect("AT+MTATTR=1,47,13,0,1", "+MTATTR:1,47,13,0\r\nOK\r\n");
  check("a first setBatTimeRemaining(0) still writes", dev.setBatTimeRemaining(0));
  s.expect("AT+MTATTR=1,47,27,0,1", "+MTATTR:1,47,27,0\r\nOK\r\n");
  check("a first setBatTimeToFullCharge(0) still writes", dev.setBatTimeToFullCharge(0));
  s.expect("AT+MTATTR=1,47,29,0,1", "+MTATTR:1,47,29,0\r\nOK\r\n");
  check("a first setBatChargingCurrent(0) still writes", dev.setBatChargingCurrent(0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The two NON-nullable attributes seed from the device's own boot value
 * instead (BatCapacity is created with 0, BatChargeState comes from
 * feature::rechargeable::add()'s config default 0 = kUnknown), so a write
 * of that value is a genuine no-op, the thermostat-seed precedent: seeding
 * anything else would re-create the first-write-swallow defect. */
static void test_non_nullable_attributes_seed_from_device_defaults(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  check("setBatCapacity(0) matches the device's boot value: no-op", dev.setBatCapacity(0));
  check("setBatChargeState(0) matches kUnknown at boot: no-op", dev.setBatChargeState(0));
  check("neither reached the wire", s.scriptDrained() && s.unexpected().empty());
  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  check("a real BatCapacity change writes", dev.setBatCapacity(13500000));
  check("script drained", s.scriptDrained());
}

static void test_battery_attribute_noop_and_failed_write(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("baseline setBatVoltage(48000)", dev.setBatVoltage(48000));
  check("repeat is a no-op", dev.setBatVoltage(48000));
  check("no second wire line", s.scriptDrained());
  s.expect("AT+MTATTR=1,47,11,49000,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !dev.setBatVoltage(49000));
  s.expect("AT+MTATTR=1,47,11,49000,1", "+MTATTR:1,47,11,49000\r\nOK\r\n");
  check("the cache stayed untouched: the same value writes again", dev.setBatVoltage(49000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Ember-served means the URC direction exists too: a controller (or the
 * firmware itself) moving one of these attributes updates the cache, the
 * MatterPowerSource::attributeChangeCB() precedent. Observable through the
 * no-op discipline: after the URC, writing the same value sends nothing. */
static void test_injected_mtattr_on_cluster_47_updates_cache(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.injectURC("+MTATTR:1,47,12,180");
  s.injectURC("+MTATTR:1,47,26,2");
  Hearth.poll();
  check("a +MTATTR on BatPercentRemaining moved the cache (a same-value write no-ops)", dev.setBatPercentRemaining(180));
  check("a +MTATTR on BatChargeState moved the cache too", dev.setBatChargeState(2));
  check("neither reached the wire", s.scriptDrained() && s.unexpected().empty());
  s.expect("AT+MTATTR=1,47,12,190,1", "+MTATTR:1,47,12,190\r\nOK\r\n");
  check("a different value still writes", dev.setBatPercentRemaining(190));
  check("script drained", s.scriptDrained());
}

/* ===== the DEM surface on FULL ===== */

static void test_dem_surface_wire_pins_on_full(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  check("setESAType(5) sends field 0 on cluster 152", dev.setESAType(5));
  s.expect("AT+MTMEAS=1,152,1,1", "OK\r\n");
  check("setESACanGenerate(true) sends field 1", dev.setESACanGenerate(true));
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("setESAState(1) sends field 2", dev.setESAState(1));
  s.expect("AT+MTMEAS=1,152,3,-5000000000", "OK\r\n");
  check("setAbsMinPower(-5000000000) survives past 2^32, negative", dev.setAbsMinPower(-5000000000LL));
  s.expect("AT+MTMEAS=1,152,4,5000000000", "OK\r\n");
  check("setAbsMaxPower(5000000000) survives past 2^32", dev.setAbsMaxPower(5000000000LL));
  s.expect("AT+MTMEAS=1,152,5,2", "OK\r\n");
  check("setOptOutState(2) sends field 5", dev.setOptOutState(2));
  s.expect("AT+MTMEAS=1,152,6,120000", "OK\r\n");
  check("pushAdjustmentEnergyUse(120000) sends field 6", dev.pushAdjustmentEnergyUse(120000));
  const MatterBatteryStorage::PowerAdjustEntry entries[2] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
  };
  s.expect("AT+MTDEMCAP=1,1,2,1000,5000,60,3600,500,2000,30,600", "OK\r\n");
  check("setPowerAdjustmentCapability sends the whole list", dev.setPowerAdjustmentCapability(1, entries, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static int g_pa_calls = 0;
static bool g_pa_verdict = true;
static int64_t g_pa_power = 0;
static uint32_t g_pa_duration = 0;
static uint8_t g_pa_cause = 0;
static bool pa_cb(int64_t p, uint32_t d, uint8_t c) {
  g_pa_calls++;
  g_pa_power = p;
  g_pa_duration = d;
  g_pa_cause = c;
  return g_pa_verdict;
}

static int g_cancel_calls = 0;
static bool g_cancel_verdict = true;
static bool cancel_cb(void) {
  g_cancel_calls++;
  return g_cancel_verdict;
}

static void test_power_adjust_dispatch_on_full(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  g_pa_calls = 0;
  g_pa_verdict = true;
  dev.onPowerAdjust(pa_cb);

  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,152,0,5000000000,60,1");
  Hearth.poll();
  check("the callback ran once", g_pa_calls == 1);
  check("power carries the full 5000000000 (above 2^32)", g_pa_power == 5000000000LL);
  check("duration 60", g_pa_duration == 60);
  check("cause 1", g_pa_cause == 1);
  check("an accept answers AT+MTCMDRESP alone, no state push", s.scriptDrained());

  /* the accept silently armed the ESAState cache, so the sketch's
   * normal-completion path is a REAL wire line (S3.17) */
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("endAdjustment() pushes ESAState Online for real", dev.endAdjustment());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_power_adjust_deny_and_cancel_on_full(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  g_pa_calls = 0;
  g_pa_verdict = false;
  dev.onPowerAdjust(pa_cb);
  g_cancel_calls = 0;
  g_cancel_verdict = true;
  dev.onCancelPowerAdjust(cancel_cb);

  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,152,0,3000,120,0");
  Hearth.poll();
  check("a denied request answers verdict 0", g_pa_calls == 1 && s.scriptDrained());

  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,1,152,1");
  Hearth.poll();
  check("the payload-less cancel reached its callback", g_cancel_calls == 1);
  check("and answered verdict 1 with nothing else", s.scriptDrained());

  /* a short PowerAdjustRequest tail is malformed: denied without
   * consulting the callback (S3.17's documented three-field arity) */
  g_pa_calls = 0;
  s.expect("AT+MTCMDRESP=10,0", "OK\r\n");
  s.injectURC("+MTCMD:10,1,152,0,3000");
  Hearth.poll();
  check("a short tail is denied without consulting the callback", g_pa_calls == 0 && s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== NO_DEM: the whole DEM surface refuses host-side ===== */

static void test_no_dem_refuses_the_whole_dem_surface(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev, MatterBatteryStorage::NO_DEM);
  const MatterBatteryStorage::PowerAdjustEntry entries[1] = {{1000, 5000, 60, 3600}};

  Hearth.hearthSetError(0);
  check("setESAType refused", !dev.setESAType(5));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setESACanGenerate refused", !dev.setESACanGenerate(true));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setESAState refused", !dev.setESAState(1));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setAbsMinPower refused", !dev.setAbsMinPower(-1000));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setAbsMaxPower refused", !dev.setAbsMaxPower(1000));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setOptOutState refused", !dev.setOptOutState(1));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("pushAdjustmentEnergyUse refused", !dev.pushAdjustmentEnergyUse(100));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setPowerAdjustmentCapability refused", !dev.setPowerAdjustmentCapability(1, entries, 1));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("endAdjustment refused", !dev.endAdjustment());
  check("error 1", Hearth.lastError() == 1);
  check("zero wire traffic for the whole batch", s.scriptDrained() && s.unexpected().empty());
}

static void test_no_dem_denies_forwarded_commands(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev, MatterBatteryStorage::NO_DEM);
  g_pa_calls = 0;
  g_pa_verdict = true;
  dev.onPowerAdjust(pa_cb);

  s.expect("AT+MTCMDRESP=11,0", "OK\r\n");
  s.injectURC("+MTCMD:11,1,152,0,5000,60,1");
  Hearth.poll();
  check("a forward on a DEM-less variant denies without consulting the callback", g_pa_calls == 0);
  check("verdict 0, no push", s.scriptDrained() && s.unexpected().empty());
}

static void test_no_dem_keeps_the_battery_attributes(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev, MatterBatteryStorage::NO_DEM);
  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("setBatVoltage still works on NO_DEM (PowerSource is on both variants)", dev.setBatVoltage(48000));
  s.expect("AT+MTATTR=1,47,26,3,1", "+MTATTR:1,47,26,3\r\nOK\r\n");
  check("setBatChargeState still works on NO_DEM", dev.setBatChargeState(3));
  check("script drained", s.scriptDrained() && s.unexpected().empty());
}

/* ===== reconcile: three splits on one endpoint ===== */

static void test_reconcile_splits(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);

  /* measurement (volatile, B229) */
  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  check("baseline setVoltage(48000)", dev.setVoltage(48000));
  /* DEM configuration (re-pushed) and DEM volatile (cleared, not resent) */
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  check("baseline setESAType(5)", dev.setESAType(5));
  s.expect("AT+MTMEAS=1,152,4,5000000000", "OK\r\n");
  check("baseline setAbsMaxPower(5000000000)", dev.setAbsMaxPower(5000000000LL));
  s.expect("AT+MTMEAS=1,152,5,1", "OK\r\n");
  check("baseline setOptOutState(1)", dev.setOptOutState(1));
  /* ember CONFIG (re-pushed) */
  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  check("baseline setBatCapacity(13500000)", dev.setBatCapacity(13500000));
  s.expect("AT+MTATTR=1,47,26,1,1", "+MTATTR:1,47,26,1\r\nOK\r\n");
  check("baseline setBatChargeState(1)", dev.setBatChargeState(1));
  /* ember SAMPLED (cleared, not resent) */
  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("baseline setBatVoltage(48000)", dev.setBatVoltage(48000));

  /* the reconcile itself: DEM configuration and the two ember CONFIG
   * attributes, in that order, and nothing else. The ember lines are
   * pushed through updateAttributeVal() directly, so they are ordinary
   * AT+MTATTR mode-1 writes, byte-identical to the setters' own. */
  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  s.expect("AT+MTATTR=1,47,26,1,1", "+MTATTR:1,47,26,1\r\nOK\r\n");
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  s.expect("AT+MTMEAS=1,152,4,5000000000", "OK\r\n");
  reconcile(dev);
  check("reconcile re-pushed the two ember CONFIG attributes and the DEM configuration, nothing else", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  /* ember CONFIG half: the cache is untouched by its own re-push, so an
   * ordinary repeat write is still the no-op it always was */
  check("repeat setBatCapacity(13500000) after reconcile is still a no-op", dev.setBatCapacity(13500000));
  check("repeat setBatChargeState(1) after reconcile is still a no-op", dev.setBatChargeState(1));
  check("neither reached the wire", s.scriptDrained() && s.unexpected().empty());

  /* ember SAMPLED half: B229. The value was NOT re-sent by the reconcile
   * above, and the wire-pushed memory was cleared, so the next identical
   * sample reaches the wire again instead of being suppressed. */
  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("setBatVoltage(48000) after reconcile reaches the wire again", dev.setBatVoltage(48000));

  /* measurement half: B229 */
  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  check("setVoltage(48000) after reconcile reaches the wire again", dev.setVoltage(48000));
  /* DEM volatile half: B229 */
  s.expect("AT+MTMEAS=1,152,5,1", "OK\r\n");
  check("setOptOutState(1) after reconcile reaches the wire again", dev.setOptOutState(1));
  /* DEM configuration half: wire memory survives the re-push */
  check("repeat setESAType(5) after reconcile is still a no-op", dev.setESAType(5));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The five sampled attributes, one at a time: none is re-sent by the
 * reconcile, and every one of them reaches the wire again on the next
 * identical push. */
static void test_reconcile_sampled_attributes_clear_without_resend(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("baseline setBatVoltage(48000)", dev.setBatVoltage(48000));
  s.expect("AT+MTATTR=1,47,12,150,1", "+MTATTR:1,47,12,150\r\nOK\r\n");
  check("baseline setBatPercentRemaining(150)", dev.setBatPercentRemaining(150));
  s.expect("AT+MTATTR=1,47,13,5400,1", "+MTATTR:1,47,13,5400\r\nOK\r\n");
  check("baseline setBatTimeRemaining(5400)", dev.setBatTimeRemaining(5400));
  s.expect("AT+MTATTR=1,47,27,7200,1", "+MTATTR:1,47,27,7200\r\nOK\r\n");
  check("baseline setBatTimeToFullCharge(7200)", dev.setBatTimeToFullCharge(7200));
  s.expect("AT+MTATTR=1,47,29,16000,1", "+MTATTR:1,47,29,16000\r\nOK\r\n");
  check("baseline setBatChargingCurrent(16000)", dev.setBatChargingCurrent(16000));

  reconcile(dev);
  check("the reconcile re-sent NONE of the five sampled readings", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  check("setBatVoltage(48000) reaches the wire again", dev.setBatVoltage(48000));
  s.expect("AT+MTATTR=1,47,12,150,1", "+MTATTR:1,47,12,150\r\nOK\r\n");
  check("setBatPercentRemaining(150) reaches the wire again", dev.setBatPercentRemaining(150));
  s.expect("AT+MTATTR=1,47,13,5400,1", "+MTATTR:1,47,13,5400\r\nOK\r\n");
  check("setBatTimeRemaining(5400) reaches the wire again", dev.setBatTimeRemaining(5400));
  s.expect("AT+MTATTR=1,47,27,7200,1", "+MTATTR:1,47,27,7200\r\nOK\r\n");
  check("setBatTimeToFullCharge(7200) reaches the wire again", dev.setBatTimeToFullCharge(7200));
  s.expect("AT+MTATTR=1,47,29,16000,1", "+MTATTR:1,47,29,16000\r\nOK\r\n");
  check("setBatChargingCurrent(16000) reaches the wire again", dev.setBatChargingCurrent(16000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The FullAPI example's exact shape, and the concrete defect this split
 * exists to prevent: a nameplate pushed once in setup() and never again.
 * Without the re-push the C6's own reboot leaves BatCapacity at its
 * creation default 0 forever, because the sketch's next call (if it ever
 * makes one) no-ops against a cache that still says 13500000. */
static void test_reconcile_repushes_a_write_once_nameplate(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  check("setup()-time setBatCapacity(13500000)", dev.setBatCapacity(13500000));
  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  reconcile(dev);
  check("the reconcile re-pushed the nameplate unprompted", s.scriptDrained());
  /* and again on a later reconcile: this is not a first-boot special case */
  s.expect("AT+MTATTR=1,47,24,13500000,1", "+MTATTR:1,47,24,13500000\r\nOK\r\n");
  reconcile(dev);
  check("and on every later reconcile too", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A reconcile before the sketch has written either config attribute emits
 * NOTHING on cluster 47: the re-push restates what this host asserted, and
 * it has asserted nothing yet. */
static void test_reconcile_before_any_ember_write_is_silent(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  reconcile(dev);
  check("no AT+MTATTR line at all", s.scriptDrained() && s.unexpected().empty());
}

/* An incoming +MTATTR URC moves the cache but is NOT an assertion by this
 * host, so it does not arm the re-push: the device told us this value, and
 * restating it back at the device on the next reconcile would be this
 * library inventing configuration it was never given. */
static void test_urc_moved_config_is_not_repushed(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.injectURC("+MTATTR:1,47,24,9000000");
  s.injectURC("+MTATTR:1,47,26,2");
  Hearth.poll();
  check("the URCs moved the caches (a same-value write no-ops)", dev.setBatCapacity(9000000) && dev.setBatChargeState(2));
  check("and neither reached the wire", s.scriptDrained() && s.unexpected().empty());
  /* the no-op setter calls above DID assert those values host-side, so the
   * reconcile now re-pushes them; what is pinned here is that the URC on
   * its own would not have. */
  s.expect("AT+MTATTR=1,47,24,9000000,1", "+MTATTR:1,47,24,9000000\r\nOK\r\n");
  s.expect("AT+MTATTR=1,47,26,2,1", "+MTATTR:1,47,26,2\r\nOK\r\n");
  reconcile(dev);
  check("after a host-side assertion of the same values, the reconcile re-pushes them", s.scriptDrained());

  MockStream s2;
  MatterBatteryStorage dev2;
  bringUp(s2, dev2);
  s2.injectURC("+MTATTR:1,47,24,9000000");
  s2.injectURC("+MTATTR:1,47,26,2");
  Hearth.poll();
  reconcile(dev2);
  check("a URC alone never arms the re-push", s2.scriptDrained() && s2.unexpected().empty());
}

static void test_reconcile_on_no_dem_is_dem_silent(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev, MatterBatteryStorage::NO_DEM);
  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  check("baseline setVoltage(48000)", dev.setVoltage(48000));
  reconcile(dev);
  check("a NO_DEM reconcile emits nothing at all", s.scriptDrained() && s.unexpected().empty());
  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  check("the measurement B229 clearing still happened", dev.setVoltage(48000));
  check("script drained", s.scriptDrained());
}

/* ===== Instance-served: an injected +MTATTR on 152 moves no DEM cache ===== */

static void test_injected_mtattr_on_152_moves_no_dem_cache(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("baseline setESAState(1)", dev.setESAState(1));
  s.injectURC("+MTATTR:1,152,2,3");
  s.injectURC("+MTATTR:1,152,0,9");
  Hearth.poll();
  check("no wire traffic resulted from the injected URCs", s.scriptDrained() && s.unexpected().empty());
  /* the cache still says 1, so a repeat push is still a no-op: the URC
   * moved nothing (S3.25's Instance-served rule for this cluster family) */
  check("the ESAState cache did not move (repeat of 1 is still a no-op)", dev.setESAState(1));
  check("still no traffic", s.scriptDrained() && s.unexpected().empty());
  s.expect("AT+MTMEAS=1,152,2,3", "OK\r\n");
  check("and a real change to 3 still writes", dev.setESAState(3));
  check("script drained", s.scriptDrained());
}

static void test_injected_mtattr_on_measurement_clusters_ignored(void) {
  MockStream s;
  MatterBatteryStorage dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  check("baseline voltage push", dev.setVoltage(48000));
  s.injectURC("+MTATTR:1,144,0,999");
  s.injectURC("+MTATTR:1,145,0,999");
  Hearth.poll();
  check("the measurement caches did not move", dev.getVoltage() == 48000 && dev.getEnergyImported() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== probe subclass: the dispatch surface is cluster 152 only ===== */

class ProbeBatteryStorage : public MatterBatteryStorage {
public:
  int fieldsCalls = 0;
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    fieldsCalls++;
    return MatterBatteryStorage::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }
};

static void test_probe_dispatch_scope(void) {
  MockStream s;
  ProbeBatteryStorage dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0018\r\nOK\r\n");
  Matter.begin();

  s.expect("AT+MTMEAS=1,144,0,48000", "OK\r\n");
  dev.setVoltage(48000);
  s.expect("AT+MTATTR=1,47,11,48000,1", "+MTATTR:1,47,11,48000\r\nOK\r\n");
  dev.setBatVoltage(48000);
  check("no push path consults the command-fields override", dev.fieldsCalls == 0);

  /* a forward on a cluster this class does not adjudicate falls through to
   * the fail-closed base default */
  s.expect("AT+MTCMDRESP=12,0", "OK\r\n");
  s.injectURC("+MTCMD:12,1,148,0,3600,0");
  Hearth.poll();
  check("a non-152 forward is denied fail-closed", s.scriptDrained());
  check("through this class's override exactly once", dev.fieldsCalls == 1);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterBatteryStorage tests =====\n");
  test_begin_declares_0x0018();
  test_begin_no_dem_declares_variant_1();
  test_out_of_range_variant_refused();
  test_setters_before_begin_and_before_reconcile();
  test_measurement_surface_wire_pins();
  test_energy_adders_enabled_on_both_variants();
  test_battery_attribute_wire_pins();
  test_nullable_attributes_first_push_of_zero_writes();
  test_non_nullable_attributes_seed_from_device_defaults();
  test_battery_attribute_noop_and_failed_write();
  test_injected_mtattr_on_cluster_47_updates_cache();
  test_dem_surface_wire_pins_on_full();
  test_power_adjust_dispatch_on_full();
  test_power_adjust_deny_and_cancel_on_full();
  test_no_dem_refuses_the_whole_dem_surface();
  test_no_dem_denies_forwarded_commands();
  test_no_dem_keeps_the_battery_attributes();
  test_reconcile_splits();
  test_reconcile_sampled_attributes_clear_without_resend();
  test_reconcile_repushes_a_write_once_nameplate();
  test_reconcile_before_any_ember_write_is_silent();
  test_urc_moved_config_is_not_repushed();
  test_reconcile_on_no_dem_is_dem_silent();
  test_injected_mtattr_on_152_moves_no_dem_cache();
  test_injected_mtattr_on_measurement_clusters_ignored();
  test_probe_dispatch_scope();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
