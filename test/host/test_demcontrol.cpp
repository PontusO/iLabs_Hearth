/*
 * test_demcontrol.cpp - Task 5 (energy round C1): HearthDemControl, the
 * shared DeviceEnergyManagement (0x0098, cluster 152) surface helper Task 6
 * embeds into MatterBatteryStorage (FULL variant) and
 * MatterDeviceEnergyManagement (both variants).
 *
 * No concrete endpoint class exists yet (Task 6 builds those), so this
 * suite exercises the helper through DemProbeEndpoint, a small
 * MatterEndPoint that embeds one HearthDemControl and hand-rolls the
 * cluster_id/command_id dispatch a real owner will, the test_cmdfields.cpp
 * FieldsProbe shape. The probe's own +MTCMD field parsing (power/duration/
 * cause read directly off fields.value[0..2]) is exactly what a Task 6
 * owner class will need to write itself, since HearthDemControl never
 * touches HearthCmdFields (see HearthDemControl.h's header comment): it is
 * pinned here as the reference shape, not something this class does for
 * its caller.
 *
 * BEYOND THIS TASK'S OWN FILE LIST, DISCLOSED (the MatterWaterHeater/Boost
 * five-field precedent, 0.9.0): HearthCmdFields.value[] (MatterEndPoint.h)
 * widened from uint32_t to int64_t, and hearthDispatchCmd()'s tail parse
 * (Hearth.cpp) from strtoul() to HearthCompat.h's hearthParseWireValue()
 * (already used for +MTATTR's identical width problem), because
 * PowerAdjustRequest's `power` field is genuinely int64 mW on the wire
 * (AT_MT_SPEC.md S3.17) and the design spec's harness section names
 * 5000000000, the >2^32 canonical vector, as the live bench's
 * PowerAdjustRequest power -- a value the previous uint32_t field could
 * not carry without silent truncation. Every existing +MTCMD consumer
 * narrows its own read of `value[i]` down to a smaller type already, so
 * this widening changes nothing for any LEGITIMATE existing payload
 * (mode ids, presence masks, durations, cook times, percentages, all well
 * under 2^32) -- test_cmdfields.cpp and every device-type suite pass
 * unchanged. It is NOT bit-for-bit identical for a hypothetical value at
 * or above 2^32 on the RP2350 target (32-bit `unsigned long`, where
 * strtoul() saturates rather than truncates), a distinction this host
 * suite (x86-64, 64-bit `unsigned long`) is structurally unable to
 * observe either way; see MatterEndPoint.h's own widening comment for the
 * precise claim (review round 1, F2). test_demcap_arity_four_at_the_bound
 * above and test_power_adjust_accept_carries_the_canonical_above_2_32_
 * vector below are this suite's own pins for the new range.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "HearthDemControl.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

namespace {
const uint32_t kDemClusterId = 0x0098;  // 152
const uint32_t kPowerAdjustRequestCmd = 0;
const uint32_t kCancelPowerAdjustRequestCmd = 1;
}  // namespace

class DemProbeEndpoint : public MatterEndPoint {
public:
  HearthDemControl dem;
  int reconciledCalls = 0;

  DemProbeEndpoint() : dem(this) {}

  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    return true;
  }

  /* The Task 6 reference shape: parse cluster 152's raw fields, then
   * delegate to the helper's two entry points. power/duration/cause are
   * unconditionally present per AT_MT_SPEC.md S3.17's documented arity
   * (three fixed fields, no presence mask), so a short tail is malformed
   * and denied without consulting the callback, the WaterHeater Boost
   * mask-mismatch precedent. */
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    if (cluster_id == kDemClusterId && command_id == kPowerAdjustRequestCmd) {
      if (fields.count < 3 || !fields.present[0] || !fields.present[1] || !fields.present[2]) {
        return false;
      }
      int64_t power = fields.value[0];
      uint32_t duration = (uint32_t)fields.value[1];
      uint8_t cause = (uint8_t)fields.value[2];
      return dem.hearthOnPowerAdjustRequest(power, duration, cause);
    }
    if (cluster_id == kDemClusterId && command_id == kCancelPowerAdjustRequestCmd) {
      return dem.hearthOnCancelPowerAdjustRequest();
    }
    return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
  }

  void hearthOnReconciled() override {
    reconciledCalls++;
    dem.onReconciled();
  }
};

static void bringUp(MockStream &s, DemProbeEndpoint &ep, uint16_t id = 2) {
  MatterEndPoint::hearthClearDeclarations();
  MatterEndPoint::hearthDeclare(&ep, 0x050D);
  ep.setEndPointId(id);
  Hearth.begin(s);
}

/* ===== construction defaults ===== */

static void test_fresh_instance_defaults_enabled(void) {
  DemProbeEndpoint ep;
  check("enabled defaults true", ep.dem.enabled);
}

/* ===== wire pins: every field-0..5 setter, first push always writes ===== */

static void test_scalar_setter_wire_pins(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTMEAS=2,152,0,5", "OK\r\n");
  check("setESAType(5) sends field 0", ep.dem.setESAType(5));

  s.expect("AT+MTMEAS=2,152,1,1", "OK\r\n");
  check("setESACanGenerate(true) sends field 1 as 1", ep.dem.setESACanGenerate(true));

  s.expect("AT+MTMEAS=2,152,2,1", "OK\r\n");
  check("setESAState(1) sends field 2", ep.dem.setESAState(1));

  s.expect("AT+MTMEAS=2,152,3,-5000000000", "OK\r\n");
  check("setAbsMinPower(-5000000000) survives past 2^32, negative", ep.dem.setAbsMinPower(-5000000000LL));

  /* the brief's explicit pin: AbsMaxPower 5000000000, the S3.25 example 4
   * vector (5 kW expressed in mW, "above what 32 bits hold"). */
  s.expect("AT+MTMEAS=2,152,4,5000000000", "OK\r\n");
  check("setAbsMaxPower(5000000000) survives past 2^32", ep.dem.setAbsMaxPower(5000000000LL));

  s.expect("AT+MTMEAS=2,152,5,2", "OK\r\n");
  check("setOptOutState(2) sends field 5", ep.dem.setOptOutState(2));

  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_scalar_setter_noop_on_unchanged_and_failed_write(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTMEAS=2,152,0,4", "OK\r\n");
  check("first setESAType(4) writes", ep.dem.setESAType(4));
  check("repeat setESAType(4) is a no-op", ep.dem.setESAType(4));
  check("no second wire line", s.scriptDrained());

  /* a first push of 0 is a real change: the fabric-side default is served
   * from the delegate but the host has never confirmed it (the electrical
   * null-until-pushed discipline). */
  s.expect("AT+MTMEAS=2,152,1,0", "OK\r\n");
  check("a first setESACanGenerate(false) still writes", ep.dem.setESACanGenerate(false));

  s.expect("AT+MTMEAS=2,152,3,1000", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !ep.dem.setAbsMinPower(1000));
  s.expect("AT+MTMEAS=2,152,3,1000", "OK\r\n");
  check("the cache stayed untouched: the same value writes again", ep.dem.setAbsMinPower(1000));
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== field 6: fire-and-forget, no cache, no no-op ===== */

static void test_energy_use_push_is_fire_and_forget(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTMEAS=2,152,6,120000", "OK\r\n");
  check("pushAdjustmentEnergyUse(120000) sends field 6", ep.dem.pushAdjustmentEnergyUse(120000));

  /* the identical value again: still a real wire line, unlike every other
   * field's no-op discipline (S3.25: "reads back through nothing... and is
   * consumed... by each PowerAdjustEnd", so there is nothing to compare
   * against). */
  s.expect("AT+MTMEAS=2,152,6,120000", "OK\r\n");
  check("an identical repeat still sends (no cache, no no-op)", ep.dem.pushAdjustmentEnergyUse(120000));

  s.expect("AT+MTMEAS=2,152,6,-30", "OK\r\n");
  check("a negative energy figure sends too", ep.dem.pushAdjustmentEnergyUse(-30));

  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== AT+MTDEMCAP: every arity 0..4, plus the n=5 host-side refusal ===== */

static void test_demcap_arity_zero_reads_null(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  s.expect("AT+MTDEMCAP=2,1,0", "OK\r\n");
  check("n=0 sends the bare header, capability reads null again", ep.dem.setPowerAdjustmentCapability(1, nullptr, 0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_demcap_arity_one(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  const HearthDemControl::PowerAdjustEntry entries[1] = {{1000, 5000, 60, 3600}};
  s.expect("AT+MTDEMCAP=2,1,1,1000,5000,60,3600", "OK\r\n");
  check("n=1 sends one entry", ep.dem.setPowerAdjustmentCapability(1, entries, 1));
  check("script drained", s.scriptDrained());
}

/* AT_MT_SPEC.md S3.26's own worked example: two local-optimization entries.
 * The spec's example is on endpoint 1; this probe is declared at endpoint 2
 * (this file's own bringUp() convention throughout), so the wire line
 * differs in its leading endpoint field only -- the cause/n/entries tail
 * from "1,2,1000,5000,60,3600,500,2000,30,600" onward matches the spec's
 * own text character for character. */
static void test_demcap_arity_two_worked_example(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  const HearthDemControl::PowerAdjustEntry entries[2] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
  };
  s.expect("AT+MTDEMCAP=2,1,2,1000,5000,60,3600,500,2000,30,600", "OK\r\n");
  check("n=2 matches the spec's worked example's cause/n/entries tail (endpoint differs: spec ep 1, probe ep 2)",
        ep.dem.setPowerAdjustmentCapability(1, entries, 2));
  check("script drained", s.scriptDrained());
}

static void test_demcap_arity_three(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  const HearthDemControl::PowerAdjustEntry entries[3] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
    {100, 300, 10, 60},
  };
  s.expect("AT+MTDEMCAP=2,2,3,1000,5000,60,3600,500,2000,30,600,100,300,10,60", "OK\r\n");
  check("n=3 sends three entries, cause GridOptimizationAdjustment", ep.dem.setPowerAdjustmentCapability(2, entries, 3));
  check("script drained", s.scriptDrained());
}

/* The 4-entry bound itself: also carries an int64 minPower past 2^32 in one
 * entry, proving the capability wire path is full-width too. */
static void test_demcap_arity_four_at_the_bound(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  const HearthDemControl::PowerAdjustEntry entries[4] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
    {100, 300, 10, 60},
    {5000000000LL, 6000000000LL, 1, 4294967295u},
  };
  s.expect(
    "AT+MTDEMCAP=2,0,4,1000,5000,60,3600,500,2000,30,600,100,300,10,60,5000000000,6000000000,1,4294967295", "OK\r\n"
  );
  check("n=4, the wire bound, entries at the bound", ep.dem.setPowerAdjustmentCapability(0, entries, 4));
  check("script drained", s.scriptDrained());
}

static void test_demcap_n5_refused_host_side(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  Hearth.hearthSetError(0);
  const HearthDemControl::PowerAdjustEntry entries[5] = {
    {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4},
  };
  check("n=5 refused host-side", !ep.dem.setPowerAdjustmentCapability(1, entries, 5));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

/* Every call is a real wire line, no no-op discipline for the capability
 * replace (S3.26: "full replacement per call"). */
static void test_demcap_repeat_call_always_sends(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  s.expect("AT+MTDEMCAP=2,1,0", "OK\r\n");
  check("first call", ep.dem.setPowerAdjustmentCapability(1, nullptr, 0));
  s.expect("AT+MTDEMCAP=2,1,0", "OK\r\n");
  check("identical repeat still sends", ep.dem.setPowerAdjustmentCapability(1, nullptr, 0));
  check("script drained", s.scriptDrained());
}

/* ===== injected +MTCMD dispatch: PowerAdjustRequest, both verdicts ===== */

typedef struct { int64_t power; uint32_t duration; uint8_t cause; int calls; bool verdict; } PACapture;
static PACapture g_pa;
static bool pa_cb(int64_t p, uint32_t d, uint8_t c) {
  g_pa.power = p;
  g_pa.duration = d;
  g_pa.cause = c;
  g_pa.calls++;
  return g_pa.verdict;
}

static void test_power_adjust_accept_pushes_no_state_change(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  g_pa = {0, 0, 0, 0, true};
  ep.dem.onPowerAdjust(pa_cb);

  /* the accept answers AT+MTCMDRESP alone: the library must NOT push a
   * state change on accept (design spec 4.1, contrast Round B's Boost). */
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,2,152,0,5000,60,1");
  Hearth.poll();

  check("the callback ran exactly once", g_pa.calls == 1);
  check("power is 5000", g_pa.power == 5000);
  check("duration is 60", g_pa.duration == 60);
  check("cause is 1 (GridOptimization)", g_pa.cause == 1);
  check("accept answered AT+MTCMDRESP=7,1 with NOTHING else on the wire", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The design spec's harness section names 5000000000 (>2^32 mW, 5 GW) as
 * the live bench's canonical PowerAdjustRequest power. Pinned here through
 * the full +MTCMD wire path (not just the setter side, which
 * test_scalar_setter_wire_pins already covers for the outbound AbsMaxPower
 * push): the widened HearthCmdFields.value[]/hearthParseWireValue() pipeline
 * (this file's header comment) must carry it into the callback intact. */
static void test_power_adjust_accept_carries_the_canonical_above_2_32_vector(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  g_pa = {0, 0, 0, 0, true};
  ep.dem.onPowerAdjust(pa_cb);

  s.expect("AT+MTCMDRESP=6,1", "OK\r\n");
  s.injectURC("+MTCMD:6,2,152,0,5000000000,60,1");
  Hearth.poll();

  check("power carries the full 5000000000 (above 2^32), not truncated", g_pa.power == 5000000000LL);
  check("accept answered AT+MTCMDRESP=6,1 with nothing else", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_power_adjust_deny_answers_verdict_zero(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  g_pa = {0, 0, 0, 0, false};
  ep.dem.onPowerAdjust(pa_cb);

  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,2,152,0,3000,120,0");
  Hearth.poll();

  check("the callback ran", g_pa.calls == 1);
  check("deny answered AT+MTCMDRESP=8,0 with nothing else", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_power_adjust_without_callback_denies(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  s.expect("AT+MTCMDRESP=9,0", "OK\r\n");
  s.injectURC("+MTCMD:9,2,152,0,3000,120,0");
  Hearth.poll();
  check("no registered callback denies by default, no push", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* An accept genuinely arms the internal cache so a LATER endAdjustment()
 * reaches the wire honestly (this header's own comment): a naive
 * "no-op if cache says Online" check would wrongly suppress it. */
static void test_power_adjust_accept_then_end_adjustment_reaches_wire(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  g_pa = {0, 0, 0, 0, true};
  ep.dem.onPowerAdjust(pa_cb);

  s.expect("AT+MTCMDRESP=10,1", "OK\r\n");
  s.injectURC("+MTCMD:10,2,152,0,5000,60,1");
  Hearth.poll();
  check("accept alone, no push", s.scriptDrained());

  /* the sketch's own normal-completion path: setESAState(Online). The
   * accept above silently moved the cache to PowerAdjustActive, so this is
   * a REAL change and must reach the wire, not no-op. */
  s.expect("AT+MTMEAS=2,152,2,1", "OK\r\n");
  check("endAdjustment() pushes ESAState Online for real", ep.dem.endAdjustment());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== injected +MTCMD dispatch: CancelPowerAdjustRequest, payload-less ===== */

static int g_cancel_calls = 0;
static bool g_cancel_verdict = true;
static bool cancel_cb() {
  g_cancel_calls++;
  return g_cancel_verdict;
}

static void test_cancel_accept_payload_less_pushes_no_state_change(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  g_cancel_calls = 0;
  g_cancel_verdict = true;
  ep.dem.onCancelPowerAdjust(cancel_cb);

  /* payload-less: no trailing fields at all on the wire line, the
   * CancelBoost precedent. The firmware resets ESAState to Online itself
   * (S3.17), so the accept answers AT+MTCMDRESP alone. */
  s.expect("AT+MTCMDRESP=11,1", "OK\r\n");
  s.injectURC("+MTCMD:11,2,152,1");
  Hearth.poll();

  check("the cancel callback ran exactly once", g_cancel_calls == 1);
  check("accept answered AT+MTCMDRESP=11,1 with nothing else", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cancel_deny_answers_verdict_zero(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  g_cancel_calls = 0;
  g_cancel_verdict = false;
  ep.dem.onCancelPowerAdjust(cancel_cb);

  s.expect("AT+MTCMDRESP=12,0", "OK\r\n");
  s.injectURC("+MTCMD:12,2,152,1");
  Hearth.poll();

  check("the cancel callback ran", g_cancel_calls == 1);
  check("deny answered verdict 0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cancel_without_callback_denies(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  s.expect("AT+MTCMDRESP=13,0", "OK\r\n");
  s.injectURC("+MTCMD:13,2,152,1");
  Hearth.poll();
  check("no registered callback denies by default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== enabled=false refusal matrix ===== */

static void test_disabled_refuses_every_call_zero_traffic(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  ep.dem.enabled = false;

  Hearth.hearthSetError(0);
  check("setESAType refused", !ep.dem.setESAType(1));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setESACanGenerate refused", !ep.dem.setESACanGenerate(true));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setESAState refused", !ep.dem.setESAState(1));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setAbsMinPower refused", !ep.dem.setAbsMinPower(1000));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setAbsMaxPower refused", !ep.dem.setAbsMaxPower(1000));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setOptOutState refused", !ep.dem.setOptOutState(1));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("pushAdjustmentEnergyUse refused", !ep.dem.pushAdjustmentEnergyUse(100));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("setPowerAdjustmentCapability refused", !ep.dem.setPowerAdjustmentCapability(1, nullptr, 0));
  check("error 1", Hearth.lastError() == 1);
  Hearth.hearthSetError(0);
  check("endAdjustment refused", !ep.dem.endAdjustment());
  check("error 1", Hearth.lastError() == 1);

  check("zero wire traffic for the whole batch", s.scriptDrained() && s.unexpected().empty());
}

/* Review round 1 (F1): onReconciled() must be gated on `enabled` too, the
 * same as every other wire-touching method -- it was not, before this
 * fix. `enabled` is public, so a sketch flipping it at runtime after
 * configuration was pushed (a variant switch, or simply disabling the
 * surface later) must not let a reconcile emit the traffic the flag
 * otherwise promises is impossible. */
static void test_disabled_reconcile_is_zero_traffic(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTMEAS=2,152,0,5", "OK\r\n");
  check("baseline setESAType(5) while enabled", ep.dem.setESAType(5));
  s.expect("AT+MTMEAS=2,152,4,5000000000", "OK\r\n");
  check("baseline setAbsMaxPower(5000000000) while enabled", ep.dem.setAbsMaxPower(5000000000LL));
  const HearthDemControl::PowerAdjustEntry entries[1] = {{1000, 5000, 60, 3600}};
  s.expect("AT+MTDEMCAP=2,1,1,1000,5000,60,3600", "OK\r\n");
  check("baseline setPowerAdjustmentCapability while enabled", ep.dem.setPowerAdjustmentCapability(1, entries, 1));
  check("baseline traffic drained before the disable", s.scriptDrained());

  ep.dem.enabled = false;
  ep.hearthOnReconciled();
  check("a reconcile while disabled sent NOTHING", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Defence in depth: an injected +MTCMD reaching a disabled helper (should
 * not happen on the real wire -- the firmware never forwards for a
 * FeatureMap-0/absent cluster -- but pinned so the fail-closed shape is
 * explicit rather than accidental) denies without consulting the callback. */
static void test_disabled_denies_forwarded_commands_without_callback(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  ep.dem.enabled = false;
  g_pa = {0, 0, 0, 0, true};
  ep.dem.onPowerAdjust(pa_cb);
  g_cancel_calls = 0;
  g_cancel_verdict = true;
  ep.dem.onCancelPowerAdjust(cancel_cb);

  s.expect("AT+MTCMDRESP=14,0", "OK\r\n");
  s.injectURC("+MTCMD:14,2,152,0,5000,60,1");
  Hearth.poll();
  check("PowerAdjustRequest denied while disabled, callback never consulted", g_pa.calls == 0);
  check("verdict 0, no push", s.scriptDrained());

  s.expect("AT+MTCMDRESP=15,0", "OK\r\n");
  s.injectURC("+MTCMD:15,2,152,1");
  Hearth.poll();
  check("CancelPowerAdjustRequest denied while disabled, callback never consulted", g_cancel_calls == 0);
  check("verdict 0, no push", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== the reconcile split, both halves ===== */

static void test_reconcile_repushes_config_and_capability_entry_by_entry(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTMEAS=2,152,0,5", "OK\r\n");
  check("baseline setESAType(5)", ep.dem.setESAType(5));
  s.expect("AT+MTMEAS=2,152,1,1", "OK\r\n");
  check("baseline setESACanGenerate(true)", ep.dem.setESACanGenerate(true));
  s.expect("AT+MTMEAS=2,152,3,-1000", "OK\r\n");
  check("baseline setAbsMinPower(-1000)", ep.dem.setAbsMinPower(-1000));
  s.expect("AT+MTMEAS=2,152,4,5000000000", "OK\r\n");
  check("baseline setAbsMaxPower(5000000000)", ep.dem.setAbsMaxPower(5000000000LL));
  const HearthDemControl::PowerAdjustEntry entries[2] = {
    {1000, 5000, 60, 3600},
    {500, 2000, 30, 600},
  };
  s.expect("AT+MTDEMCAP=2,1,2,1000,5000,60,3600,500,2000,30,600", "OK\r\n");
  check("baseline setPowerAdjustmentCapability", ep.dem.setPowerAdjustmentCapability(1, entries, 2));

  s.expect("AT+MTMEAS=2,152,2,3", "OK\r\n");
  check("baseline setESAState(3)", ep.dem.setESAState(3));
  s.expect("AT+MTMEAS=2,152,5,2", "OK\r\n");
  check("baseline setOptOutState(2)", ep.dem.setOptOutState(2));

  /* reconcile: config re-pushed verbatim, entry by entry for the
   * capability list; volatile has-flags cleared, not resent. */
  s.expect("AT+MTMEAS=2,152,0,5", "OK\r\n");
  s.expect("AT+MTMEAS=2,152,1,1", "OK\r\n");
  s.expect("AT+MTMEAS=2,152,3,-1000", "OK\r\n");
  s.expect("AT+MTMEAS=2,152,4,5000000000", "OK\r\n");
  s.expect("AT+MTDEMCAP=2,1,2,1000,5000,60,3600,500,2000,30,600", "OK\r\n");
  ep.hearthOnReconciled();
  check("reconcile re-pushed ESAType, ESACanGenerate, AbsMinPower, AbsMaxPower and the capability list, nothing else",
        s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("hearthOnReconciled ran once", ep.reconciledCalls == 1);

  /* config wire-memory survives the re-push: a repeat is still a no-op */
  check("repeat setESAType(5) after reconcile is still a no-op", ep.dem.setESAType(5));
  check("repeat setESACanGenerate(true) after reconcile is still a no-op", ep.dem.setESACanGenerate(true));
  check("repeat setAbsMinPower(-1000) after reconcile is still a no-op", ep.dem.setAbsMinPower(-1000));
  check("repeat setAbsMaxPower(5000000000) after reconcile is still a no-op", ep.dem.setAbsMaxPower(5000000000LL));

  /* volatile wire-memory is cleared, values not resent: the same value
   * writes again, the B229 shape. */
  s.expect("AT+MTMEAS=2,152,2,3", "OK\r\n");
  check("setESAState(3) after reconcile reaches the wire again", ep.dem.setESAState(3));
  s.expect("AT+MTMEAS=2,152,5,2", "OK\r\n");
  check("setOptOutState(2) after reconcile reaches the wire again", ep.dem.setOptOutState(2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Field 6 never appears in the reconcile re-push list: it keeps no cache to
 * begin with, so there is nothing to clear or resend either way. */
static void test_reconcile_never_repushes_energy_use(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  s.expect("AT+MTMEAS=2,152,6,55000", "OK\r\n");
  check("baseline pushAdjustmentEnergyUse(55000)", ep.dem.pushAdjustmentEnergyUse(55000));
  ep.hearthOnReconciled();
  check("reconcile pushed NOTHING (no config was ever set)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A capability explicitly cleared to null (n=0) is itself configuration and
 * is re-pushed on reconcile, distinct from "never configured" (which
 * pushes nothing for the capability at all, proven by the previous test's
 * silence on it). */
static void test_reconcile_repushes_capability_null_state(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  s.expect("AT+MTDEMCAP=2,1,0", "OK\r\n");
  check("baseline setPowerAdjustmentCapability(..., 0)", ep.dem.setPowerAdjustmentCapability(1, nullptr, 0));
  s.expect("AT+MTDEMCAP=2,1,0", "OK\r\n");
  ep.hearthOnReconciled();
  check("reconcile re-pushed the null capability state", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== an injected +MTATTR naming cluster 152 moves nothing (Instance-served) ===== */

static void test_injected_attr_on_152_moves_no_cache(void) {
  MockStream s;
  DemProbeEndpoint ep;
  bringUp(s, ep);
  /* the probe's attributeChangeCB always returns true and never touches
   * `dem`, so this is really pinning that HearthDemControl offers no path
   * an +MTATTR URC could reach at all: there is no getter surface on this
   * class (the spec's class block has none), consistent with S3.25's "no
   * AT+MTATTR path, no +MTATTR URCs" rule for this whole cluster family. */
  s.injectURC("+MTATTR:2,152,2,3");
  Hearth.poll();
  check("no wire traffic resulted", s.scriptDrained() && s.unexpected().empty());
}

int main(void) {
  printf("\n===== HearthDemControl tests =====\n");
  test_fresh_instance_defaults_enabled();
  test_scalar_setter_wire_pins();
  test_scalar_setter_noop_on_unchanged_and_failed_write();
  test_energy_use_push_is_fire_and_forget();
  test_demcap_arity_zero_reads_null();
  test_demcap_arity_one();
  test_demcap_arity_two_worked_example();
  test_demcap_arity_three();
  test_demcap_arity_four_at_the_bound();
  test_demcap_n5_refused_host_side();
  test_demcap_repeat_call_always_sends();
  test_power_adjust_accept_pushes_no_state_change();
  test_power_adjust_accept_carries_the_canonical_above_2_32_vector();
  test_power_adjust_deny_answers_verdict_zero();
  test_power_adjust_without_callback_denies();
  test_power_adjust_accept_then_end_adjustment_reaches_wire();
  test_cancel_accept_payload_less_pushes_no_state_change();
  test_cancel_deny_answers_verdict_zero();
  test_cancel_without_callback_denies();
  test_disabled_refuses_every_call_zero_traffic();
  test_disabled_reconcile_is_zero_traffic();
  test_disabled_denies_forwarded_commands_without_callback();
  test_reconcile_repushes_config_and_capability_entry_by_entry();
  test_reconcile_never_repushes_energy_use();
  test_reconcile_repushes_capability_null_state();
  test_injected_attr_on_152_moves_no_cache();

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
