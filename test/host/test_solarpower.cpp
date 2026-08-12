/*
 * test_solarpower.cpp - Task 6 (energy round C1): MatterSolarPower
 * (0x0017), the third consumer of the shared HearthMeasurementPush helper.
 * Surface-wise this is the heat pump plus a variant byte: the electrical
 * push surface and identity, nothing else.
 *
 * What this suite pins:
 *
 * - Declaration (0x0017, variants 0 and 1), begin() traffic-free, an
 *   out-of-range variant refused host-side before any declaration.
 * - The measurement surface delegating to the shared helper with the
 *   electrical sensor's exact wire pins, including values past 2^32 and
 *   the signed generation direction a solar array reports.
 * - NO_ENERGY (variant 1, the current-clamp shape: the firmware grafts no
 *   EEM cluster, mt_devtypes.cpp's mk_solar_power() passing `variant == 0`
 *   as its energy flag): the energy adders refuse host-side with error 1
 *   and zero wire traffic, the POWER_ONLY precedent, while every
 *   power-side setter keeps working.
 * - B229 reconcile semantics via the helper: nothing re-pushed,
 *   wire-pushed memory cleared, accumulators preserved.
 * - The Instance-served rule: injected +MTATTR URCs naming clusters
 *   144/145 never move the caches.
 * - No +MTCMD dispatch path: a probe subclass proves nothing in the
 *   class's machinery ever consults the fields override.
 */
#include <stdio.h>
#include <stdint.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterSolarPower.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* The variant rides the AT+MTEP? reply's fourth field exactly as the
 * electrical sensor's does: absent at 0, "1" for NO_ENERGY. */
static void bringUp(MockStream &s, MatterSolarPower &dev, MatterSolarPower::Variant_t v = MatterSolarPower::FULL) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterSolarPower::FULL ? "+MTEP:0,1,0x0017\r\nOK\r\n" : "+MTEP:0,1,0x0017,1\r\nOK\r\n");
  Matter.begin();
}

static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

/* ===== declaration ===== */

static void test_begin_declares_0x0017(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  check("declared as device type 0x0017", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0017);
  check("FULL declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("begin() issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("voltage cache starts 0 (null on the fabric until pushed)", dev.getVoltage() == 0);
  check("energy accumulators start 0", dev.getEnergyImported() == 0 && dev.getEnergyExported() == 0);
}

static void test_begin_no_energy_declares_variant_1(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev, MatterSolarPower::NO_ENERGY);
  check("NO_ENERGY still declares 0x0017", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0017);
  check("NO_ENERGY declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
  check("no unexpected commands", s.unexpected().empty());
}

/* An enum parameter does not stop a cast from smuggling in a third value,
 * and the variant byte travels the wire verbatim (the electrical sensor's
 * own validation shape). */
static void test_out_of_range_variant_refused(void) {
  MockStream s;
  MatterSolarPower dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  Hearth.hearthSetError(0);
  check("a smuggled-in variant 2 is refused", !dev.begin((MatterSolarPower::Variant_t)2));
  check("with error 1", Hearth.lastError() == 1);
  check("and nothing was declared", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setter_before_begin_and_before_reconcile(void) {
  MockStream s;
  MatterSolarPower dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setVoltage() before begin() fails", !dev.setVoltage(230000));
  check("addEnergyExported() before begin() fails", !dev.addEnergyExported(1));
  check("begin() declares", dev.begin());
  check("setVoltage() before reconcile fails", !dev.setVoltage(230000));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== the measurement surface: the electrical sensor's exact pins ===== */

static void test_measurement_setters_exact_wire_pins(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,415000", "OK\r\n");
  check("setVoltage(415000) sends field 0 on cluster 144", dev.setVoltage(415000));
  check("voltage cache updated", dev.getVoltage() == 415000);
  s.expect("AT+MTMEAS=1,144,1,12000", "OK\r\n");
  check("setActiveCurrent(12000) sends field 1", dev.setActiveCurrent(12000));
  check("current cache updated", dev.getActiveCurrent() == 12000);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) sends field 3", dev.setFrequency(50000));
  check("frequency cache updated", dev.getFrequency() == 50000);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A solar array GENERATES: its ActivePower is negative in the sign
 * convention the electrical clusters use for export, and the magnitude of
 * a real string inverter runs past 2^32 milliwatts (5 kW = 5000000 mW is
 * not, but the pipeline is pinned at the S3.25 example-4 vector anyway,
 * exactly as the DEM helper's own suite pins it). */
static void test_signed_generation_past_2_to_32(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,2,-5000000000", "OK\r\n");
  check("setActivePower(-5000000000) keeps sign and magnitude", dev.setActivePower(-5000000000LL));
  check("cache carries the negative value", dev.getActivePower() == -5000000000LL);
  s.expect("AT+MTMEAS=1,144,2,5000000000", "OK\r\n");
  check("and back positive past 2^32", dev.setActivePower(5000000000LL));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_push_measurements_batches_one_line(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,415000,1,-12000,2,-4980000", "OK\r\n");
  check("pushMeasurements() emits one three-pair line", dev.pushMeasurements(415000, -12000, -4980000));
  s.expect("AT+MTMEAS=1,144,0,415000,1,-12000,2,-4980000", "OK\r\n");
  check("an identical batch writes again (a fresh sample, S3.25)", dev.pushMeasurements(415000, -12000, -4980000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_noop_on_unchanged_and_failed_write(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,415000", "OK\r\n");
  check("first setVoltage(415000) writes", dev.setVoltage(415000));
  check("repeat setVoltage(415000) is a no-op", dev.setVoltage(415000));
  check("no second wire line", s.scriptDrained());
  s.expect("AT+MTMEAS=1,144,0,999", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !dev.setVoltage(999));
  check("cache untouched (still 415000)", dev.getVoltage() == 415000);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== FULL: the energy adders ===== */

static void test_energy_adders_enabled_on_full(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,145,1,5000000000", "OK\r\n");
  check("addEnergyExported(5000000000) pushes the total past 2^32", dev.addEnergyExported(5000000000ULL));
  s.expect("AT+MTMEAS=1,145,1,5000001000", "OK\r\n");
  check("a second add pushes the NEW cumulative total", dev.addEnergyExported(1000));
  check("exported accumulator is 5000001000", dev.getEnergyExported() == 5000001000ULL);
  s.expect("AT+MTMEAS=1,145,0,20000", "OK\r\n");
  check("addEnergyImported(20000) pushes field 0 (night-time house draw)", dev.addEnergyImported(20000));
  check("imported accumulator is 20000", dev.getEnergyImported() == 20000ULL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== NO_ENERGY: the adders refuse host-side, the power side works ===== */

static void test_no_energy_refuses_adders_zero_traffic(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev, MatterSolarPower::NO_ENERGY);
  Hearth.hearthSetError(0);
  check("addEnergyImported refused on NO_ENERGY", !dev.addEnergyImported(1000));
  check("with error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("addEnergyExported refused on NO_ENERGY", !dev.addEnergyExported(1000));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic for both refusals", s.scriptDrained() && s.unexpected().empty());
  check("the accumulators never moved", dev.getEnergyImported() == 0 && dev.getEnergyExported() == 0);
}

static void test_no_energy_keeps_the_power_surface(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev, MatterSolarPower::NO_ENERGY);
  s.expect("AT+MTMEAS=1,144,0,415000", "OK\r\n");
  check("setVoltage still works on NO_ENERGY", dev.setVoltage(415000));
  s.expect("AT+MTMEAS=1,144,2,-4980000", "OK\r\n");
  check("setActivePower still works on NO_ENERGY", dev.setActivePower(-4980000));
  s.expect("AT+MTMEAS=1,144,0,415000,1,-12000,2,-4980000", "OK\r\n");
  check("pushMeasurements still works on NO_ENERGY", dev.pushMeasurements(415000, -12000, -4980000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== B229 reconcile semantics via the shared helper ===== */

static void test_reconcile_b229_semantics(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("baseline setFrequency(50000)", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,145,1,1500000", "OK\r\n");
  check("baseline addEnergyExported(1500000)", dev.addEnergyExported(1500000));
  reconcile(dev);
  check("hearthOnReconciled() issues no traffic", s.scriptDrained());
  check("and nothing unscripted was sent", s.unexpected().empty());
  check("cache values survive locally", dev.getFrequency() == 50000 && dev.getEnergyExported() == 1500000ULL);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) after reconcile reaches the wire again", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,145,1,1500000", "OK\r\n");
  check("an add of 0 re-seeds the fabric with the preserved total", dev.addEnergyExported(0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the Instance-served rule ===== */

static void test_injected_mtattr_on_measurement_clusters_ignored(void) {
  MockStream s;
  MatterSolarPower dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,415000", "OK\r\n");
  check("baseline voltage push", dev.setVoltage(415000));
  s.expect("AT+MTMEAS=1,145,1,1500000", "OK\r\n");
  check("baseline energy push", dev.addEnergyExported(1500000));
  s.injectURC("+MTATTR:1,144,0,999");
  s.injectURC("+MTATTR:1,145,1,999");
  Hearth.poll();
  check("an injected +MTATTR on cluster 144 does not move the voltage cache", dev.getVoltage() == 415000);
  check("an injected +MTATTR on cluster 145 does not move the accumulator", dev.getEnergyExported() == 1500000ULL);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== no +MTCMD dispatch path ===== */

class ProbeSolarPower : public MatterSolarPower {
public:
  int fieldsCalls = 0;
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    fieldsCalls++;
    return MatterSolarPower::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }
};

static void test_no_cmd_dispatch_path_consulted(void) {
  MockStream s;
  ProbeSolarPower dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0017\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTMEAS=1,144,0,415000,1,-12000,2,-4980000", "OK\r\n");
  dev.pushMeasurements(415000, -12000, -4980000);
  s.expect("AT+MTMEAS=1,145,1,1000", "OK\r\n");
  dev.addEnergyExported(1000);
  s.injectURC("+MTATTR:1,144,0,999");
  Hearth.poll();
  check("the command-fields override was never consulted by the class's own machinery", dev.fieldsCalls == 0);
  /* a spurious forward at this endpoint denies via the base default: the
   * class registers no commands and overrides no command virtual */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,152,0,5000,60,1");
  Hearth.poll();
  check("a spurious DEM forward is denied fail-closed (solar carries no DEM cluster)", s.scriptDrained());
  check("the base default answered it, one override call", dev.fieldsCalls == 1);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterSolarPower tests =====\n");
  test_begin_declares_0x0017();
  test_begin_no_energy_declares_variant_1();
  test_out_of_range_variant_refused();
  test_rebegin_after_reconcile_refused();
  test_setter_before_begin_and_before_reconcile();
  test_measurement_setters_exact_wire_pins();
  test_signed_generation_past_2_to_32();
  test_push_measurements_batches_one_line();
  test_noop_on_unchanged_and_failed_write();
  test_energy_adders_enabled_on_full();
  test_no_energy_refuses_adders_zero_traffic();
  test_no_energy_keeps_the_power_surface();
  test_reconcile_b229_semantics();
  test_injected_mtattr_on_measurement_clusters_ignored();
  test_no_cmd_dispatch_path_consulted();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
