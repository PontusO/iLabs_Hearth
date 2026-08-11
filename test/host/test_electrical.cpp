/*
 * test_electrical.cpp - Task 7 (energy round A): MatterElectricalSensor
 * (0x0510) and MatterElectricalMeter (0x0514), the two measurement-push
 * device types behind AT+MTMEAS (AT_MT_SPEC.md S3.25). The first consumers
 * of Task 6's 64-bit value pipeline in a concrete endpoint class.
 *
 * What this suite pins, beyond the sibling-class basics:
 *
 * - Exact AT+MTMEAS wire lines for every setter, including values past
 *   2^32 in both signs (the whole point of Task 6), and the batched
 *   three-pair pushMeasurements() line.
 * - Null-until-pushed semantics: every power field starts null ON THE
 *   FABRIC (S3.25), so the class must not suppress a first push merely
 *   because the value equals the cache's zero-initialised default. A
 *   has-value flag, not a magic value, models this host-side.
 * - Energy adders accumulate LOCALLY and push the new cumulative total
 *   (the firmware serves cumulative counters, not deltas); POWER_ONLY
 *   refuses both adders host-side with error 1 and zero wire traffic.
 * - The 0.6.0 Instance-served rule (S3.25: "no AT+MTATTR path, no +MTATTR
 *   URCs"): an injected +MTATTR URC naming cluster 144 or 145 must not
 *   move the cache. The host's own pushed values are the single source of
 *   truth.
 * - No +MTCMD dispatch path: neither cluster has commands, the class
 *   overrides no command virtual, and a probe subclass proves nothing in
 *   the class's own machinery ever consults the fields override.
 * - Measurements are NOT re-pushed on reconcile (volatile readings, unlike
 *   the cabinet's labels): hearthOnReconciled() on a sensor holding pushed
 *   values must issue no traffic. It DOES invalidate the wire-pushed
 *   memory (the has-value flags): after a co-processor reboot the
 *   fabric-side fields are null again, so a setter repeating its
 *   pre-reboot value must reach the wire, not no-op against a fabric
 *   state that no longer exists. The host cache values and the energy
 *   accumulators survive the reconcile untouched.
 */
#include <stdio.h>
#include <stdint.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterElectricalSensor.h"
#include "MatterEndpoints/MatterElectricalMeter.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* begin() issues no AT traffic (measurements are volatile, nothing to
 * reconcile; see the class header), so bring-up is a plain single-endpoint
 * adopt. The variant rides the AT+MTEP? reply's fourth field exactly as
 * the cabinet's does: absent at 0, "1" for POWER_ONLY. */
static void bringUpSensor(MockStream &s, MatterElectricalSensor &dev,
                          MatterElectricalSensor::Variant_t v = MatterElectricalSensor::FULL) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterElectricalSensor::FULL ? "+MTEP:0,1,0x0510\r\nOK\r\n" : "+MTEP:0,1,0x0510,1\r\nOK\r\n");
  Matter.begin();
}

static void bringUpMeter(MockStream &s, MatterElectricalMeter &dev,
                         MatterElectricalMeter::Variant_t v = MatterElectricalMeter::FULL) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterElectricalMeter::FULL ? "+MTEP:0,1,0x0514\r\nOK\r\n" : "+MTEP:0,1,0x0514,1\r\nOK\r\n");
  Matter.begin();
}

/* ===== declaration, both types, both variants ===== */

static void test_sensor_begin_declares_0x0510_variant0(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  check("sensor declared as device type 0x0510", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0510);
  check("sensor FULL declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("sensor adopted endpoint 1", dev.getEndPointId() == 1);
  check("begin() issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("voltage cache starts 0 (null on the fabric until pushed)", dev.getVoltage() == 0);
  check("current cache starts 0", dev.getActiveCurrent() == 0);
  check("power cache starts 0", dev.getActivePower() == 0);
  check("frequency cache starts 0", dev.getFrequency() == 0);
  check("imported energy accumulator starts 0", dev.getEnergyImported() == 0);
  check("exported energy accumulator starts 0", dev.getEnergyExported() == 0);
}

static void test_sensor_begin_power_only_declares_variant1(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev, MatterElectricalSensor::POWER_ONLY);
  check("sensor POWER_ONLY still declares 0x0510", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0510);
  check("sensor POWER_ONLY declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_meter_begin_declares_0x0514_both_variants(void) {
  {
    MockStream s; MatterElectricalMeter dev;
    bringUpMeter(s, dev);
    check("meter declared as device type 0x0514", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0514);
    check("meter FULL declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
    check("meter adopted endpoint 1", dev.getEndPointId() == 1);
    check("no unexpected commands", s.unexpected().empty());
  }
  {
    MockStream s; MatterElectricalMeter dev;
    bringUpMeter(s, dev, MatterElectricalMeter::POWER_ONLY);
    check("meter POWER_ONLY declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
    check("no unexpected commands (POWER_ONLY meter)", s.unexpected().empty());
  }
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== individual setters: exact wire pins, cache on success only ===== */

static void test_setters_exact_wire_pins(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("setVoltage(230000) sends the exact wire pin", dev.setVoltage(230000));
  check("voltage cache updated", dev.getVoltage() == 230000);
  s.expect("AT+MTMEAS=1,144,1,433", "OK\r\n");
  check("setActiveCurrent(433) sends field 1", dev.setActiveCurrent(433));
  check("current cache updated", dev.getActiveCurrent() == 433);
  s.expect("AT+MTMEAS=1,144,2,99590", "OK\r\n");
  check("setActivePower(99590) sends field 2", dev.setActivePower(99590));
  check("power cache updated", dev.getActivePower() == 99590);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) sends field 3", dev.setFrequency(50000));
  check("frequency cache updated", dev.getFrequency() == 50000);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The Task 6 seam: values past 2^32 in both signs must reach the wire at
 * full width. 5000000000 is > 2^32; a 32-bit pipeline truncates it to
 * 705032704 and negates nothing it should. */
static void test_setters_past_2_to_32_both_signs(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,5000000000", "OK\r\n");
  check("setVoltage(5000000000) survives past 2^32", dev.setVoltage(5000000000LL));
  check("cache carries the full value", dev.getVoltage() == 5000000000LL);
  s.expect("AT+MTMEAS=1,144,2,-5000000000", "OK\r\n");
  check("setActivePower(-5000000000) keeps sign and magnitude", dev.setActivePower(-5000000000LL));
  check("cache carries the negative value", dev.getActivePower() == -5000000000LL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setter_noop_on_unchanged_pushed_value(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("first setVoltage(230000) writes", dev.setVoltage(230000));
  check("repeat setVoltage(230000) is a no-op", dev.setVoltage(230000));
  check("no second wire line", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Null-until-pushed: the fabric-side value starts null, not 0, so a first
 * setVoltage(0) is a real change and must reach the wire even though the
 * host cache is zero-initialised. This is the has-value flag's whole job. */
static void test_first_push_of_zero_still_writes(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,0", "OK\r\n");
  check("first setVoltage(0) writes despite the zero-initialised cache", dev.setVoltage(0));
  check("repeat setVoltage(0) after the push is a no-op", dev.setVoltage(0));
  check("exactly one wire line", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_setter_leaves_cache(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("baseline push", dev.setVoltage(230000));
  s.expect("AT+MTMEAS=1,144,0,999", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !dev.setVoltage(999));
  check("cache untouched (still 230000)", dev.getVoltage() == 230000);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setter_before_reconcile_fails_without_traffic(void) {
  MockStream s; MatterElectricalSensor dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", dev.begin());
  check("setVoltage() before reconcile fails", !dev.setVoltage(230000));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

static void test_setter_before_begin_fails(void) {
  MockStream s; MatterElectricalSensor dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setVoltage() before begin() fails", !dev.setVoltage(230000));
  check("pushMeasurements() before begin() fails", !dev.pushMeasurements(1, 2, 3));
  check("addEnergyImported() before begin() fails", !dev.addEnergyImported(1));
  check("no traffic issued", s.unexpected().empty());
}

/* ===== pushMeasurements: ONE wire line, three pairs ===== */

static void test_push_measurements_single_three_pair_line(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000,1,433,2,99590", "OK\r\n");
  check("pushMeasurements() emits one three-pair line", dev.pushMeasurements(230000, 433, 99590));
  check("voltage cache updated", dev.getVoltage() == 230000);
  check("current cache updated", dev.getActiveCurrent() == 433);
  check("power cache updated", dev.getActivePower() == 99590);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A batch is a fresh sample of volatile readings: it always reaches the
 * wire, even byte-identical to the previous one (each push re-reports the
 * fields dirty, S3.25, so subscriptions fire per sample). */
static void test_push_measurements_always_writes(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000,1,433,2,99590", "OK\r\n");
  s.expect("AT+MTMEAS=1,144,0,230000,1,433,2,99590", "OK\r\n");
  check("first identical batch writes", dev.pushMeasurements(230000, 433, 99590));
  check("second identical batch writes again", dev.pushMeasurements(230000, 433, 99590));
  check("both lines reached the wire", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_push_measurements_full_width_values(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,5000000000,1,-5000000000,2,9223372036854775807", "OK\r\n");
  check("a batch carries full-width 64-bit values",
        dev.pushMeasurements(5000000000LL, -5000000000LL, 9223372036854775807LL));
  check("INT64_MAX landed in the power cache", dev.getActivePower() == 9223372036854775807LL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_push_measurements_leaves_all_caches(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000,1,433,2,99590", "OK\r\n");
  check("baseline batch", dev.pushMeasurements(230000, 433, 99590));
  s.expect("AT+MTMEAS=1,144,0,1,1,2,2,3", "+MTERR:2\r\nERROR\r\n");
  check("a rejected batch returns false", !dev.pushMeasurements(1, 2, 3));
  check("voltage cache untouched", dev.getVoltage() == 230000);
  check("current cache untouched", dev.getActiveCurrent() == 433);
  check("power cache untouched", dev.getActivePower() == 99590);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== energy adders: local accumulation, cumulative-total push ===== */

static void test_energy_adders_accumulate_and_push_totals(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("addEnergyImported(1500000) pushes the total", dev.addEnergyImported(1500000));
  check("imported accumulator is 1500000", dev.getEnergyImported() == 1500000ULL);
  s.expect("AT+MTMEAS=1,145,0,2000000", "OK\r\n");
  check("addEnergyImported(500000) pushes the NEW cumulative total", dev.addEnergyImported(500000));
  check("imported accumulator is 2000000", dev.getEnergyImported() == 2000000ULL);
  s.expect("AT+MTMEAS=1,145,1,20000", "OK\r\n");
  check("addEnergyExported(20000) pushes field 1", dev.addEnergyExported(20000));
  check("exported accumulator is 20000", dev.getEnergyExported() == 20000ULL);
  check("imported accumulator unaffected by the exported add", dev.getEnergyImported() == 2000000ULL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_energy_past_2_to_32(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,145,0,5000000000", "OK\r\n");
  check("addEnergyImported(5000000000) survives past 2^32", dev.addEnergyImported(5000000000ULL));
  check("accumulator carries the full value", dev.getEnergyImported() == 5000000000ULL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_energy_push_leaves_accumulator(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline add", dev.addEnergyImported(1500000));
  s.expect("AT+MTMEAS=1,145,0,1500001", "+MTERR:1\r\nERROR\r\n");
  check("a rejected push returns false", !dev.addEnergyImported(1));
  check("accumulator untouched (still 1500000)", dev.getEnergyImported() == 1500000ULL);
  check("no unexpected commands", s.unexpected().empty());
}

/* POWER_ONLY refuses energy HOST-side: error 1, zero wire traffic. The
 * variant builds no ElectricalEnergyMeasurement cluster at all (S3.9), so
 * letting the push travel just to collect the firmware's +MTERR:3 would
 * waste a round trip on an answer the host already knows. */
static void test_power_only_sensor_refuses_energy_host_side(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev, MatterElectricalSensor::POWER_ONLY);
  Hearth.hearthSetError(0);
  check("addEnergyImported() refused on POWER_ONLY", !dev.addEnergyImported(1000));
  check("with error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("addEnergyExported() refused on POWER_ONLY", !dev.addEnergyExported(1000));
  check("with error 1 as well", Hearth.lastError() == 1);
  check("accumulators stayed 0", dev.getEnergyImported() == 0 && dev.getEnergyExported() == 0);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
  /* the power side still works on POWER_ONLY, that is the variant's point */
  s.expect("AT+MTMEAS=1,144,2,99590", "OK\r\n");
  check("setActivePower() still writes on POWER_ONLY", dev.setActivePower(99590));
  check("script drained", s.scriptDrained());
}

static void test_power_only_meter_refuses_energy_host_side(void) {
  MockStream s; MatterElectricalMeter dev;
  bringUpMeter(s, dev, MatterElectricalMeter::POWER_ONLY);
  Hearth.hearthSetError(0);
  check("meter addEnergyImported() refused on POWER_ONLY", !dev.addEnergyImported(1000));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

/* ===== the Instance-served rule: +MTATTR URCs never move the cache ===== */

static void test_injected_mtattr_on_measurement_clusters_ignored(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("baseline voltage push", dev.setVoltage(230000));
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline energy push", dev.addEnergyImported(1500000));
  /* S3.25: no +MTATTR URC ever reports these attributes. If one is
   * injected anyway (a firmware bug, a test rig, line noise that parses),
   * the cache must not move: the host's own pushes are the single source
   * of truth. */
  s.injectURC("+MTATTR:1,144,0,999");
  Hearth.poll();
  check("an injected +MTATTR on cluster 144 does not move the voltage cache", dev.getVoltage() == 230000);
  s.injectURC("+MTATTR:1,145,0,999");
  Hearth.poll();
  check("an injected +MTATTR on cluster 145 does not move the energy accumulator",
        dev.getEnergyImported() == 1500000ULL);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== no +MTCMD dispatch path ===== */

/* Neither measurement cluster has commands (S3.25), so nothing in this
 * class may route through the +MTCMD machinery. The probe subclass records
 * any call to the fields override; a full lifecycle (begin, reconcile,
 * setters, batch, energy adds, injected +MTATTR URCs) must never trip it. */
class ProbeElectricalSensor : public MatterElectricalSensor {
public:
  int fieldsCalls = 0;
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    fieldsCalls++;
    return MatterElectricalSensor::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }
};

static void test_no_cmd_dispatch_path_consulted(void) {
  MockStream s; ProbeElectricalSensor dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0510\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  dev.setVoltage(230000);
  s.expect("AT+MTMEAS=1,144,0,231000,1,440,2,101000", "OK\r\n");
  dev.pushMeasurements(231000, 440, 101000);
  s.expect("AT+MTMEAS=1,145,0,1000", "OK\r\n");
  dev.addEnergyImported(1000);
  s.injectURC("+MTATTR:1,144,0,999");
  Hearth.poll();
  check("the command-fields override was never consulted", dev.fieldsCalls == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== reconcile: volatile readings are NOT re-pushed ===== */

/* The override is protected (the sibling-class convention); the base's
 * public virtual is the dispatch surface Hearth.cpp itself uses, so the
 * tests invoke the hook the same way. */
static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

static void test_reconcile_does_not_repush_measurements(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000,1,433,2,99590", "OK\r\n");
  check("baseline batch", dev.pushMeasurements(230000, 433, 99590));
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline energy add", dev.addEnergyImported(1500000));
  /* The reconcile hook every stateful class overrides to resend
   * non-persisted configuration: measurements are volatile READINGS, not
   * configuration. A re-push would report a stale sample as fresh, so the
   * class's override resends NOTHING; it only clears the wire-pushed
   * memory (pinned by the two tests below). After a co-processor reboot
   * the fabric-side fields are null again until the sketch's own next
   * sample, which is the honest state. */
  reconcile(dev);
  check("hearthOnReconciled() issues no traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
  check("caches survive locally regardless", dev.getVoltage() == 230000 && dev.getEnergyImported() == 1500000ULL);
}

/* The other half of the reconcile contract, the review finding on the
 * first cut of this class: the fabric-side fields are null again after a
 * co-processor reboot, but the has-value flags still said "pushed", so a
 * setter repeating its pre-reboot value no-opped and the fabric field
 * stayed null forever (setFrequency, typically set once, was the classic
 * victim). hearthOnReconciled() must clear the wire-pushed memory so the
 * next setter call writes, while the host cache values themselves survive
 * (the getters keep answering the last pushed sample). */
static void test_reconcile_clears_wire_pushed_memory(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("baseline setFrequency(50000)", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,144,0,230000,1,433,2,99590", "OK\r\n");
  check("baseline batch", dev.pushMeasurements(230000, 433, 99590));
  check("repeat setFrequency(50000) before reconcile is a no-op", dev.setFrequency(50000));
  reconcile(dev);
  check("getters still answer the cached values after reconcile",
        dev.getFrequency() == 50000 && dev.getVoltage() == 230000 && dev.getActiveCurrent() == 433 && dev.getActivePower() == 99590);
  s.expect("AT+MTMEAS=1,144,3,50000", "OK\r\n");
  check("setFrequency(50000) after reconcile reaches the wire again", dev.setFrequency(50000));
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("setVoltage(230000) after reconcile reaches the wire again", dev.setVoltage(230000));
  check("a repeat after the re-push is a no-op again", dev.setFrequency(50000) && dev.setVoltage(230000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The energy side of the same finding, decided deliberately: the
 * accumulators are the HOST-side running totals, the single source of
 * truth, so the reconcile must NOT reset them. There is no wire-pushed
 * flag to clear either: the adders always push the cumulative total, so
 * the first add after the reboot re-seeds the fabric's counter with the
 * preserved total by construction, even an add of 0. */
static void test_reconcile_preserves_energy_accumulators(void) {
  MockStream s; MatterElectricalSensor dev;
  bringUpSensor(s, dev);
  s.expect("AT+MTMEAS=1,145,0,1500000", "OK\r\n");
  check("baseline energy add", dev.addEnergyImported(1500000));
  reconcile(dev);
  check("accumulator survives the reconcile", dev.getEnergyImported() == 1500000ULL);
  s.expect("AT+MTMEAS=1,145,0,1500250", "OK\r\n");
  check("the next add pushes the preserved total plus the delta", dev.addEnergyImported(250));
  s.expect("AT+MTMEAS=1,145,0,1500250", "OK\r\n");
  check("an add of 0 re-seeds the fabric with the unchanged total", dev.addEnergyImported(0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* the meter inherits the override with everything else */
static void test_meter_inherits_reconcile_invalidation(void) {
  MockStream s; MatterElectricalMeter dev;
  bringUpMeter(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("meter baseline setVoltage(230000)", dev.setVoltage(230000));
  reconcile(dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("meter setVoltage(230000) after reconcile reaches the wire again", dev.setVoltage(230000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the meter inherits the whole surface ===== */

static void test_meter_inherits_the_full_surface(void) {
  MockStream s; MatterElectricalMeter dev;
  bringUpMeter(s, dev);
  s.expect("AT+MTMEAS=1,144,0,230000", "OK\r\n");
  check("meter setVoltage() carries the sensor's wire pin", dev.setVoltage(230000));
  s.expect("AT+MTMEAS=1,144,0,231000,1,440,2,101000", "OK\r\n");
  check("meter pushMeasurements() batches identically", dev.pushMeasurements(231000, 440, 101000));
  s.expect("AT+MTMEAS=1,145,0,5000000000", "OK\r\n");
  check("meter addEnergyImported() accumulates past 2^32", dev.addEnergyImported(5000000000ULL));
  check("meter getters read the same caches",
        dev.getVoltage() == 231000 && dev.getActivePower() == 101000 && dev.getEnergyImported() == 5000000000ULL);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterElectricalSensor / MatterElectricalMeter tests =====\n");
  test_sensor_begin_declares_0x0510_variant0();
  test_sensor_begin_power_only_declares_variant1();
  test_meter_begin_declares_0x0514_both_variants();
  test_rebegin_after_reconcile_refused();
  test_setters_exact_wire_pins();
  test_setters_past_2_to_32_both_signs();
  test_setter_noop_on_unchanged_pushed_value();
  test_first_push_of_zero_still_writes();
  test_failed_setter_leaves_cache();
  test_setter_before_reconcile_fails_without_traffic();
  test_setter_before_begin_fails();
  test_push_measurements_single_three_pair_line();
  test_push_measurements_always_writes();
  test_push_measurements_full_width_values();
  test_failed_push_measurements_leaves_all_caches();
  test_energy_adders_accumulate_and_push_totals();
  test_energy_past_2_to_32();
  test_failed_energy_push_leaves_accumulator();
  test_power_only_sensor_refuses_energy_host_side();
  test_power_only_meter_refuses_energy_host_side();
  test_injected_mtattr_on_measurement_clusters_ignored();
  test_no_cmd_dispatch_path_consulted();
  test_reconcile_does_not_repush_measurements();
  test_reconcile_clears_wire_pushed_memory();
  test_reconcile_preserves_energy_accumulators();
  test_meter_inherits_reconcile_invalidation();
  test_meter_inherits_the_full_surface();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
