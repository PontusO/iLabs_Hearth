/*
 * test_heatpump.cpp - Task 6 (energy round B): MatterHeatPump (0x0309),
 * the second consumer of the shared HearthMeasurementPush helper and the
 * simplest measurement-bearing endpoint class in the library: the
 * electrical push surface plus identity, NOTHING else. PowerSource is
 * declared wired at composition on the firmware side with no host surface,
 * and the class deliberately has no mode, thermostat or boost members at
 * all (the disclosed-gap note in its header; a sketch reaching for one is
 * a compile error, which is the enforcement).
 *
 * What this suite pins:
 *
 * - Declaration (0x0309, variant 0 only), begin() traffic-free.
 * - The full measurement surface delegating to the shared helper with the
 *   electrical sensor's exact wire pins, including values past 2^32.
 * - The energy adders enabled (ledger note: `enabled` is set explicitly at
 *   begin(), never left to the constructor default behind the started
 *   guard).
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
#include "MatterEndpoints/MatterHeatPump.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterHeatPump &dev) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0309\r\nOK\r\n");
  Matter.begin();
}

static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

static void test_begin_declares_0x0309(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  check("declared as device type 0x0309", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0309);
  check("declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("begin() issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("voltage cache starts 0 (null on the fabric until pushed)", dev.getVoltage() == 0);
  check("energy accumulators start 0", dev.getEnergyImported() == 0 && dev.getEnergyExported() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setter_before_begin_and_before_reconcile(void) {
  MockStream s; MatterHeatPump dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setVoltage() before begin() fails", !dev.setVoltage(230000));
  check("addEnergyImported() before begin() fails", !dev.addEnergyImported(1));
  check("begin() declares", dev.begin());
  check("setVoltage() before reconcile fails", !dev.setVoltage(230000));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== the measurement surface: the electrical sensor's exact pins ===== */

static void test_measurement_setters_exact_wire_pins(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("setVoltage(230000) sends field 0 on cluster 144", dev.setVoltage(230000));
  check("voltage cache updated", dev.getVoltage() == 230000);
  s.expect("AT+MTMEAS=1,144,1,6520", "OK\r\n");
  check("setActiveCurrent(6520) sends field 1", dev.setActiveCurrent(6520));
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) sends field 3", dev.setFrequency(50000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A heat pump's power is SIGNED on this cluster: a negative ActivePower
 * models energy flowing back out (the defrost-cycle / export direction),
 * and the sign must survive past 2^32 through the 64-bit pipeline. */
static void test_signed_power_past_2_to_32(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,2,-5000000000", "OK\r\n");
  check("setActivePower(-5000000000) keeps sign and magnitude", dev.setActivePower(-5000000000LL));
  check("cache carries the negative value", dev.getActivePower() == -5000000000LL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_push_measurements_batches_one_line(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000,1,6520,2,1500000", "OK\r\n");
  check("pushMeasurements() emits one three-pair line", dev.pushMeasurements(230000, 6520, 1500000));
  s.expect("AT+MTMEAS=1,144,0,230000,1,6520,2,1500000", "OK\r\n");
  check("an identical batch writes again (a fresh sample, S3.25)", dev.pushMeasurements(230000, 6520, 1500000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Ledger note pin: the helper's `enabled` gate is set explicitly at
 * begin(), so the energy adders work out of the box on the heat pump's
 * single (energy-bearing) variant. */
static void test_energy_adders_enabled_and_accumulate(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,145,0,5000000000", "OK\r\n");
  check("addEnergyImported(5000000000) pushes the total past 2^32", dev.addEnergyImported(5000000000ULL));
  s.expect("AT+MTMEAS=1,145,0,5000001000", "OK\r\n");
  check("a second add pushes the NEW cumulative total", dev.addEnergyImported(1000));
  check("imported accumulator is 5000001000", dev.getEnergyImported() == 5000001000ULL);
  s.expect("AT+MTMEAS=1,145,1,20000", "OK\r\n");
  check("addEnergyExported(20000) pushes field 1", dev.addEnergyExported(20000));
  check("exported accumulator is 20000", dev.getEnergyExported() == 20000ULL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_push_leaves_caches(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("baseline setVoltage(230000)", dev.setVoltage(230000));
  s.expect("AT+MTMEAS=1,144,0,999", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !dev.setVoltage(999));
  check("cache untouched (still 230000)", dev.getVoltage() == 230000);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== B229 reconcile semantics via the shared helper ===== */

static void test_reconcile_b229_semantics(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("baseline setFrequency(50000)", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline addEnergyImported(1500000)", dev.addEnergyImported(1500000));
  reconcile(dev);
  check("hearthOnReconciled() issues no traffic", s.scriptDrained());
  check("and nothing unscripted was sent", s.unexpected().empty());
  check("cache values survive locally", dev.getFrequency() == 50000 && dev.getEnergyImported() == 1500000ULL);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) after reconcile reaches the wire again", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("an add of 0 re-seeds the fabric with the preserved total", dev.addEnergyImported(0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the Instance-served rule ===== */

static void test_injected_mtattr_on_measurement_clusters_ignored(void) {
  MockStream s; MatterHeatPump dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("baseline voltage push", dev.setVoltage(230000));
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline energy push", dev.addEnergyImported(1500000));
  s.injectURC("+MTATTR:1,144,0,999");
  s.injectURC("+MTATTR:1,145,0,999");
  Hearth.poll();
  check("an injected +MTATTR on cluster 144 does not move the voltage cache", dev.getVoltage() == 230000);
  check("an injected +MTATTR on cluster 145 does not move the accumulator", dev.getEnergyImported() == 1500000ULL);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== no +MTCMD dispatch path ===== */

class ProbeHeatPump : public MatterHeatPump {
public:
  int fieldsCalls = 0;
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    fieldsCalls++;
    return MatterHeatPump::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }
};

static void test_no_cmd_dispatch_path_consulted(void) {
  MockStream s; ProbeHeatPump dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0309\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTMEAS=1,144,0,230000,1,6520,2,1500000", "OK\r\n");
  dev.pushMeasurements(230000, 6520, 1500000);
  s.expect("AT+MTMEAS=1,145,0,1000", "OK\r\n");
  dev.addEnergyImported(1000);
  s.injectURC("+MTATTR:1,144,0,999");
  Hearth.poll();
  check("the command-fields override was never consulted by the class's own machinery", dev.fieldsCalls == 0);
  /* a spurious forward at this endpoint denies via the base default: the
   * class registers no commands and overrides no command virtual */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,148,0,3600,265,80");
  Hearth.poll();
  check("a spurious forward is denied fail-closed", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterHeatPump tests =====\n");
  test_begin_declares_0x0309();
  test_rebegin_after_reconcile_refused();
  test_setter_before_begin_and_before_reconcile();
  test_measurement_setters_exact_wire_pins();
  test_signed_power_past_2_to_32();
  test_push_measurements_batches_one_line();
  test_energy_adders_enabled_and_accumulate();
  test_failed_push_leaves_caches();
  test_reconcile_b229_semantics();
  test_injected_mtattr_on_measurement_clusters_ignored();
  test_no_cmd_dispatch_path_consulted();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
