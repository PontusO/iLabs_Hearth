/*
 * test_dem.cpp - Task 6 (energy round C1): MatterDeviceEnergyManagement
 * (0x050D), the standalone Energy Smart Appliance endpoint: Task 5's
 * HearthDemControl plus identity, nothing else.
 *
 * THE ROUND'S ADJUDICATED LINE, pinned in both halves here (Task 5's
 * review, item 10; the ledger's ACTION for this task): REPORT_ONLY is NOT
 * `enabled = false` on the helper. `enabled = false` models "no DEM
 * cluster at all" (MatterBatteryStorage's NO_DEM: the firmware answers
 * +MTERR:3, cluster-missing, for the WHOLE surface), while REPORT_ONLY
 * models "cluster present, PowerAdjustment feature absent" (the firmware
 * answers +MTERR:4, attribute-missing, for AT+MTDEMCAP alone, while every
 * AT+MTMEAS field 0-6 push still works: AT_MT_SPEC.md S3.26's own error
 * table). So REPORT_ONLY keeps the helper enabled and gates exactly ONE
 * method in the OWNER class, the MatterWaterHeater::hearthRefusedOnMinimal()
 * precedent:
 *
 *   - test_report_only_refuses_capability_zero_traffic() pins the refusal
 *     (false, error 1, zero wire traffic);
 *   - test_report_only_keeps_state_and_identity_pushes() pins that every
 *     other push still reaches the wire on REPORT_ONLY.
 *
 * The rest is the class's own wiring on top of a helper whose mechanics
 * test_demcontrol.cpp already pins: declaration and variant validation,
 * the started guard in front of every delegation, the +MTCMD dispatch
 * shape (three flat fields for PowerAdjustRequest, payload-less cancel),
 * the reconcile split, and the Instance-served rule for cluster 152.
 */
#include <stdio.h>
#include <stdint.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterDeviceEnergyManagement.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(
  MockStream &s, MatterDeviceEnergyManagement &dev, MatterDeviceEnergyManagement::Variant_t v = MatterDeviceEnergyManagement::CONTROLLABLE
) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterDeviceEnergyManagement::CONTROLLABLE ? "+MTEP:0,1,0x050D\r\nOK\r\n" : "+MTEP:0,1,0x050D,1\r\nOK\r\n");
  Matter.begin();
}

static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

/* ===== declaration ===== */

static void test_begin_declares_0x050d(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  check("declared as device type 0x050D", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x050D);
  check("CONTROLLABLE declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("begin() issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_begin_report_only_declares_variant_1(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev, MatterDeviceEnergyManagement::REPORT_ONLY);
  check("REPORT_ONLY still declares 0x050D", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x050D);
  check("REPORT_ONLY declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_out_of_range_variant_refused(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  Hearth.hearthSetError(0);
  check("a smuggled-in variant 2 is refused", !dev.begin((MatterDeviceEnergyManagement::Variant_t)2));
  check("with error 1", Hearth.lastError() == 1);
  check("and nothing was declared", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setters_before_begin_and_before_reconcile(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setESAType() before begin() fails", !dev.setESAType(5));
  check("pushAdjustmentEnergyUse() before begin() fails", !dev.pushAdjustmentEnergyUse(1));
  check("endAdjustment() before begin() fails", !dev.endAdjustment());
  check("setPowerAdjustmentCapability() before begin() fails", !dev.setPowerAdjustmentCapability(1, nullptr, 0));
  check("begin() declares", dev.begin());
  check("setESAType() before reconcile fails", !dev.setESAType(5));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== CONTROLLABLE: the whole surface ===== */

static void test_state_and_identity_wire_pins(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  check("setESAType(5) sends field 0", dev.setESAType(5));
  s.expect("AT+MTMEAS=1,152,1,1", "OK\r\n");
  check("setESACanGenerate(true) sends field 1 as 1", dev.setESACanGenerate(true));
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
  s.expect("AT+MTMEAS=1,152,6,120000", "OK\r\n");
  check("field 6 always writes (event carrier, no cache)", dev.pushAdjustmentEnergyUse(120000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_capability_arities_on_controllable(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  s.expect("AT+MTDEMCAP=1,1,0", "OK\r\n");
  check("n=0 sends the bare header (capability reads null)", dev.setPowerAdjustmentCapability(1, nullptr, 0));

  /* AT_MT_SPEC.md S3.26's own worked example, endpoint 1: byte for byte. */
  const MatterDeviceEnergyManagement::PowerAdjustEntry two[2] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
  };
  s.expect("AT+MTDEMCAP=1,1,2,1000,5000,60,3600,500,2000,30,600", "OK\r\n");
  check("n=2 matches the spec's worked example verbatim", dev.setPowerAdjustmentCapability(1, two, 2));

  const MatterDeviceEnergyManagement::PowerAdjustEntry four[4] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
    {100, 300, 10, 60},
    {5000000000LL, 6000000000LL, 1, 4294967295u},
  };
  s.expect("AT+MTDEMCAP=1,0,4,1000,5000,60,3600,500,2000,30,600,100,300,10,60,5000000000,6000000000,1,4294967295", "OK\r\n");
  check("n=4, the wire bound, entries past 2^32", dev.setPowerAdjustmentCapability(0, four, 4));

  Hearth.hearthSetError(0);
  const MatterDeviceEnergyManagement::PowerAdjustEntry five[5] = {
    {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4},
  };
  check("n=5 refused host-side (the helper's bound)", !dev.setPowerAdjustmentCapability(1, five, 5));
  check("with error 1", Hearth.lastError() == 1);
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

static void test_power_adjust_accept_then_end(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
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
  check("an accept answers AT+MTCMDRESP alone (the firmware owns the state change)", s.scriptDrained());

  s.expect("AT+MTMEAS=1,152,6,45000", "OK\r\n");
  check("the sketch's energy figure pushes after the callback returned", dev.pushAdjustmentEnergyUse(45000));
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("endAdjustment() pushes ESAState Online for real", dev.endAdjustment());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_power_adjust_deny_and_missing_callback(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  g_pa_calls = 0;
  g_pa_verdict = false;
  dev.onPowerAdjust(pa_cb);
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,152,0,3000,120,0");
  Hearth.poll();
  check("a deny answers verdict 0", g_pa_calls == 1 && s.scriptDrained());

  MockStream s2;
  MatterDeviceEnergyManagement dev2;
  bringUp(s2, dev2);
  s2.expect("AT+MTCMDRESP=9,0", "OK\r\n");
  s2.injectURC("+MTCMD:9,1,152,0,3000,120,0");
  Hearth.poll();
  check("no registered callback denies by default (fail closed)", s2.scriptDrained());
  check("no unexpected commands", s2.unexpected().empty());
}

static void test_cancel_dispatch(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  g_cancel_calls = 0;
  g_cancel_verdict = true;
  dev.onCancelPowerAdjust(cancel_cb);
  s.expect("AT+MTCMDRESP=10,1", "OK\r\n");
  s.injectURC("+MTCMD:10,1,152,1");
  Hearth.poll();
  check("the payload-less cancel reached its callback", g_cancel_calls == 1);
  check("and answered verdict 1 with nothing else", s.scriptDrained());

  g_cancel_verdict = false;
  s.expect("AT+MTCMDRESP=11,0", "OK\r\n");
  s.injectURC("+MTCMD:11,1,152,1");
  Hearth.poll();
  check("a denied cancel answers verdict 0", g_cancel_calls == 2 && s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A tail shorter than S3.17's documented three-field arity is malformed
 * and denied WITHOUT consulting the callback (the WaterHeater Boost
 * mask-mismatch precedent, carried into this dispatch by Task 5's
 * reference shape). */
static void test_short_power_adjust_tail_denied_unconsulted(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  g_pa_calls = 0;
  g_pa_verdict = true;
  dev.onPowerAdjust(pa_cb);
  s.expect("AT+MTCMDRESP=12,0", "OK\r\n");
  s.injectURC("+MTCMD:12,1,152,0,5000,60");
  Hearth.poll();
  check("a two-field tail is denied without consulting the callback", g_pa_calls == 0);
  check("verdict 0, nothing else", s.scriptDrained() && s.unexpected().empty());
}

/* ===== REPORT_ONLY: the adjudicated gate, both halves ===== */

static void test_report_only_refuses_capability_zero_traffic(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev, MatterDeviceEnergyManagement::REPORT_ONLY);
  const MatterDeviceEnergyManagement::PowerAdjustEntry entries[1] = {{1000, 5000, 60, 3600}};

  Hearth.hearthSetError(0);
  check("setPowerAdjustmentCapability refused on REPORT_ONLY", !dev.setPowerAdjustmentCapability(1, entries, 1));
  check("with error 1", Hearth.lastError() == 1);
  check("ZERO wire traffic: the answer (+MTERR:4) is already known host-side", s.scriptDrained() && s.unexpected().empty());

  /* the n=0 "clear to null" form is refused just the same: there is no
   * PowerAdjustmentCapability attribute on this variant to clear */
  Hearth.hearthSetError(0);
  check("even the n=0 form is refused", !dev.setPowerAdjustmentCapability(1, nullptr, 0));
  check("with error 1", Hearth.lastError() == 1);
  check("still zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_report_only_keeps_state_and_identity_pushes(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev, MatterDeviceEnergyManagement::REPORT_ONLY);
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  check("setESAType still reaches the wire on REPORT_ONLY", dev.setESAType(5));
  s.expect("AT+MTMEAS=1,152,1,1", "OK\r\n");
  check("setESACanGenerate still reaches the wire", dev.setESACanGenerate(true));
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("setESAState still reaches the wire", dev.setESAState(1));
  s.expect("AT+MTMEAS=1,152,3,-1000", "OK\r\n");
  check("setAbsMinPower still reaches the wire", dev.setAbsMinPower(-1000));
  s.expect("AT+MTMEAS=1,152,4,5000000000", "OK\r\n");
  check("setAbsMaxPower still reaches the wire", dev.setAbsMaxPower(5000000000LL));
  s.expect("AT+MTMEAS=1,152,5,1", "OK\r\n");
  check("setOptOutState still reaches the wire", dev.setOptOutState(1));
  s.expect("AT+MTMEAS=1,152,6,45000", "OK\r\n");
  check("the field-6 energy carrier still reaches the wire", dev.pushAdjustmentEnergyUse(45000));
  s.expect("AT+MTMEAS=1,152,2,3", "OK\r\n");
  check("a state change to PowerAdjustActive still reaches the wire", dev.setESAState(3));
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("endAdjustment() still reaches the wire", dev.endAdjustment());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A REPORT_ONLY endpoint's reconcile re-pushes the scalar configuration
 * exactly as CONTROLLABLE does, and can never emit an AT+MTDEMCAP line,
 * because a refused capability call caches nothing. */
static void test_report_only_reconcile_never_emits_demcap(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev, MatterDeviceEnergyManagement::REPORT_ONLY);
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  check("baseline setESAType(5)", dev.setESAType(5));
  const MatterDeviceEnergyManagement::PowerAdjustEntry entries[1] = {{1000, 5000, 60, 3600}};
  check("the refused capability call", !dev.setPowerAdjustmentCapability(1, entries, 1));
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  reconcile(dev);
  check("reconcile re-pushed ESAType and NO AT+MTDEMCAP line", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the reconcile split ===== */

static void test_reconcile_split(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  check("baseline setESAType(5)", dev.setESAType(5));
  s.expect("AT+MTMEAS=1,152,1,1", "OK\r\n");
  check("baseline setESACanGenerate(true)", dev.setESACanGenerate(true));
  s.expect("AT+MTMEAS=1,152,3,-1000", "OK\r\n");
  check("baseline setAbsMinPower(-1000)", dev.setAbsMinPower(-1000));
  s.expect("AT+MTMEAS=1,152,4,5000000000", "OK\r\n");
  check("baseline setAbsMaxPower(5000000000)", dev.setAbsMaxPower(5000000000LL));
  const MatterDeviceEnergyManagement::PowerAdjustEntry entries[1] = {{1000, 5000, 60, 3600}};
  s.expect("AT+MTDEMCAP=1,1,1,1000,5000,60,3600", "OK\r\n");
  check("baseline setPowerAdjustmentCapability", dev.setPowerAdjustmentCapability(1, entries, 1));
  s.expect("AT+MTMEAS=1,152,2,3", "OK\r\n");
  check("baseline setESAState(3)", dev.setESAState(3));
  s.expect("AT+MTMEAS=1,152,5,2", "OK\r\n");
  check("baseline setOptOutState(2)", dev.setOptOutState(2));

  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  s.expect("AT+MTMEAS=1,152,1,1", "OK\r\n");
  s.expect("AT+MTMEAS=1,152,3,-1000", "OK\r\n");
  s.expect("AT+MTMEAS=1,152,4,5000000000", "OK\r\n");
  s.expect("AT+MTDEMCAP=1,1,1,1000,5000,60,3600", "OK\r\n");
  reconcile(dev);
  check("reconcile re-pushed the configuration half and the capability list", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  check("repeat setESAType(5) after reconcile is still a no-op", dev.setESAType(5));
  s.expect("AT+MTMEAS=1,152,2,3", "OK\r\n");
  check("setESAState(3) after reconcile reaches the wire again (B229)", dev.setESAState(3));
  s.expect("AT+MTMEAS=1,152,5,2", "OK\r\n");
  check("setOptOutState(2) after reconcile reaches the wire again (B229)", dev.setOptOutState(2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_reconcile_before_any_push_is_silent(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  reconcile(dev);
  check("a reconcile with nothing configured emits nothing", s.scriptDrained() && s.unexpected().empty());
}

/* ===== Instance-served: an injected +MTATTR on 152 moves nothing ===== */

static void test_injected_mtattr_on_152_moves_no_cache(void) {
  MockStream s;
  MatterDeviceEnergyManagement dev;
  bringUp(s, dev);
  s.expect("AT+MTMEAS=1,152,2,1", "OK\r\n");
  check("baseline setESAState(1)", dev.setESAState(1));
  s.injectURC("+MTATTR:1,152,2,3");
  s.injectURC("+MTATTR:1,152,0,9");
  Hearth.poll();
  check("no wire traffic resulted", s.scriptDrained() && s.unexpected().empty());
  check("the ESAState cache did not move (repeat of 1 is still a no-op)", dev.setESAState(1));
  check("still no traffic", s.scriptDrained() && s.unexpected().empty());
  s.expect("AT+MTMEAS=1,152,2,3", "OK\r\n");
  check("a real change to 3 still writes", dev.setESAState(3));
  check("script drained", s.scriptDrained());
}

/* ===== probe subclass: the dispatch surface is cluster 152 only ===== */

class ProbeDem : public MatterDeviceEnergyManagement {
public:
  int fieldsCalls = 0;
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    fieldsCalls++;
    return MatterDeviceEnergyManagement::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }
};

static void test_probe_dispatch_scope(void) {
  MockStream s;
  ProbeDem dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x050D\r\nOK\r\n");
  Matter.begin();

  s.expect("AT+MTMEAS=1,152,0,5", "OK\r\n");
  dev.setESAType(5);
  check("no push path consults the command-fields override", dev.fieldsCalls == 0);

  s.expect("AT+MTCMDRESP=13,0", "OK\r\n");
  s.injectURC("+MTCMD:13,1,158,0,2");
  Hearth.poll();
  check("a non-152 forward is denied fail-closed", s.scriptDrained());
  check("through this class's override exactly once", dev.fieldsCalls == 1);

  /* an unknown command id on cluster 152 falls through to the same base
   * default: only 0 and 1 are adjudicated (S3.17) */
  s.expect("AT+MTCMDRESP=14,0", "OK\r\n");
  s.injectURC("+MTCMD:14,1,152,4,1");
  Hearth.poll();
  check("an unadjudicated command on 152 is denied fail-closed too", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterDeviceEnergyManagement tests =====\n");
  test_begin_declares_0x050d();
  test_begin_report_only_declares_variant_1();
  test_out_of_range_variant_refused();
  test_rebegin_after_reconcile_refused();
  test_setters_before_begin_and_before_reconcile();
  test_state_and_identity_wire_pins();
  test_capability_arities_on_controllable();
  test_power_adjust_accept_then_end();
  test_power_adjust_deny_and_missing_callback();
  test_cancel_dispatch();
  test_short_power_adjust_tail_denied_unconsulted();
  test_report_only_refuses_capability_zero_traffic();
  test_report_only_keeps_state_and_identity_pushes();
  test_report_only_reconcile_never_emits_demcap();
  test_reconcile_split();
  test_reconcile_before_any_push_is_silent();
  test_injected_mtattr_on_152_moves_no_cache();
  test_probe_dispatch_scope();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
