/*
 * test_utilitymeter.cpp - Task 12 (energy round C2): MatterElectricalUtilityMeter,
 * device type 0x0511. Unlike every other measurement-push type this round
 * and Round A/B added, the wire here is ONE bundled command
 * (AT+MTMETERID) standing behind five public setters (see the class header):
 * every setter call folds the new field into the CURRENT cached identity and
 * resends the WHOLE line, and a power threshold must be set at least once
 * before ANY push (including one triggered by setMeterType()/
 * setPointOfDelivery()/etc.) can succeed at all.
 *
 * What this suite pins:
 * - The exact AT+MTMETERID wire line for every setter, including the
 *   present-but-empty-token convention for an absent pwr/apparent/src.
 * - A comma INSIDE a quoted string survives unescaped (legal content, the
 *   firmware's own scanner already tolerates it).
 * - Host-side grammar enforcement BEFORE any wire traffic: string length
 *   (64 accepted, 65 refused), printable-ASCII-only, no raw '"', the
 *   choice-b "at least one power value" rule, and the enum range checks.
 * - The power-threshold precondition on every OTHER setter, not merely
 *   setPowerThreshold() itself.
 * - The B229 reconcile pattern: hearthOnReconciled() unconditionally
 *   re-sends the full cached identity (bypassing every setter's own
 *   unchanged-value guard), and a subsequent identical setter call is still
 *   a no-op afterwards -- exactly MatterEvse::hearthOnReconciled()'s
 *   CircuitCapacity precedent, because the reconcile push has already put
 *   the fabric back in sync.
 * - attributeChangeCB() is a documented no-op (cluster 0x0B06 is
 *   Instance-served for all five attributes, task 9's report).
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterElectricalUtilityMeter.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterElectricalUtilityMeter &dev) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0511\r\nOK\r\n");
  Matter.begin();
}

static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

/* ===== declaration ===== */

static void test_begin_declares_0x0511_variant0(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  check("declared as device type 0x0511", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0511);
  check("declared variant 0 (no variant scheme, one-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("bringUp() issued no traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setters_before_begin_and_before_reconcile(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setMeterType() before begin() fails", !dev.setMeterType(0));
  check("setPointOfDelivery() before begin() fails", !dev.setPointOfDelivery("x"));
  check("setSerialNumber() before begin() fails", !dev.setSerialNumber("x"));
  check("setProtocolVersion() before begin() fails", !dev.setProtocolVersion("x"));
  check("setPowerThreshold() before begin() fails", !dev.setPowerThreshold(true, 1, false, 0, false, 0));
  check("begin() declares", dev.begin());
  Hearth.hearthSetError(0);
  check("setPowerThreshold() before reconcile (endpoint id 0) fails", !dev.setPowerThreshold(true, 1, false, 0, false, 0));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== the power-threshold precondition, before anything else can push ===== */

static void test_meter_type_before_threshold_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  Hearth.hearthSetError(0);
  check("setMeterType() before any power threshold is refused host-side", !dev.setMeterType(1));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_pod_serial_protocol_before_threshold_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  Hearth.hearthSetError(0);
  check("setPointOfDelivery() before any power threshold is refused", !dev.setPointOfDelivery("HOME"));
  check("with error 1", Hearth.lastError() == 1);
  check("setSerialNumber() before any power threshold is refused", !dev.setSerialNumber("SN1"));
  check("setProtocolVersion() before any power threshold is refused", !dev.setProtocolVersion("v1"));
  check("zero wire traffic across all three", s.scriptDrained() && s.unexpected().empty());
}

/* ===== setPowerThreshold(): the choice-b rule, ranges, no-ops ===== */

static void test_both_power_values_absent_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  Hearth.hearthSetError(0);
  check("both pwr and apparent absent is refused host-side (choice b)", !dev.setPowerThreshold(false, 0, false, 0, false, 0));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_power_threshold_pwr_only_pushes_exact_wire(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",5000000,,", "OK\r\n");
  check("pwr present only pushes the exact wire line, defaults for everything else", dev.setPowerThreshold(true, 5000000, false, 0, false, 0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("repeat with the same values is a no-op", dev.setPowerThreshold(true, 5000000, false, 0, false, 0));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());
}

static void test_power_threshold_apparent_only_pushes(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",,7000000,", "OK\r\n");
  check("apparent present only pushes the exact wire line", dev.setPowerThreshold(false, 0, true, 7000000, false, 0));
  check("script drained", s.scriptDrained());
}

static void test_power_threshold_src_nullable_absent_and_present(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",100,,", "OK\r\n");
  check("src absent renders an empty trailing token", dev.setPowerThreshold(true, 100, false, 0, false, 0));

  /* pwr unchanged, src now present: the whole struct differs on the wire's
   * own terms (wholesale replacement), so this must NOT be treated as a
   * no-op even though pwr itself did not change. */
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",100,,1", "OK\r\n");
  check("src present (Regulator=1) reaches the wire, not swallowed by pwr's own unchanged value", dev.setPowerThreshold(true, 100, false, 0, true, 1));
  check("script drained", s.scriptDrained());

  check("repeat with identical (pwr,src) is a no-op", dev.setPowerThreshold(true, 100, false, 0, true, 1));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());
}

static void test_power_threshold_src_out_of_range_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  Hearth.hearthSetError(0);
  check("src == 3 (past kEquipment == 2) is refused host-side", !dev.setPowerThreshold(true, 1, false, 0, true, 3));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

/* ===== setMeterType(): range and no-op, once a threshold exists ===== */

static void test_meter_type_pushes_and_no_ops(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));

  s.expect("AT+MTMETERID=1,2,\"\",\"\",\"\",1,,", "OK\r\n");
  check("setMeterType(2, Generic) pushes", dev.setMeterType(2));
  check("repeat setMeterType(2) is a no-op", dev.setMeterType(2));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());
}

static void test_meter_type_out_of_range_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));
  Hearth.hearthSetError(0);
  check("setMeterType(3), past kGeneric == 2, is refused host-side", !dev.setMeterType(3));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

/* ===== setPointOfDelivery()/setSerialNumber()/setProtocolVersion(): the string grammar ===== */

static void test_point_of_delivery_comma_survives_unescaped(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));

  s.expect("AT+MTMETERID=1,0,\"Suite 4, Building A\",\"\",\"\",1,,", "OK\r\n");
  check("a comma inside the quoted string survives unescaped", dev.setPointOfDelivery("Suite 4, Building A"));
  check("script drained", s.scriptDrained());
  check("repeat with the identical string is a no-op", dev.setPointOfDelivery("Suite 4, Building A"));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());
}

static void test_serial_number_64_bytes_accepted_65_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));

  char sixtyFour[65];
  memset(sixtyFour, 'A', 64);
  sixtyFour[64] = '\0';
  char cmd[400];
  snprintf(cmd, sizeof(cmd), "AT+MTMETERID=1,0,\"\",\"%s\",\"\",1,,", sixtyFour);
  s.expect(cmd, "OK\r\n");
  check("a 64-byte serial number is accepted", dev.setSerialNumber(sixtyFour));
  check("script drained", s.scriptDrained());

  char sixtyFive[66];
  memset(sixtyFive, 'B', 65);
  sixtyFive[65] = '\0';
  Hearth.hearthSetError(0);
  check("a 65-byte serial number is refused host-side", !dev.setSerialNumber(sixtyFive));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic for the refusal", s.scriptDrained() && s.unexpected().empty());
}

static void test_protocol_version_nonprintable_byte_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));

  Hearth.hearthSetError(0);
  const char withControlChar[] = {'v', '1', '\x01', '\0'};
  check("a non-printable byte (0x01) is refused host-side", !dev.setProtocolVersion(withControlChar));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_protocol_version_embedded_quote_refused(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));

  Hearth.hearthSetError(0);
  check("an embedded raw '\"' is refused host-side", !dev.setProtocolVersion("v1\"beta"));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_wire_refusal_leaves_cache_untouched(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed a power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));

  s.expect("AT+MTMETERID=1,0,\"HOME\",\"\",\"\",1,,", "+MTERR:1\r\nERROR\r\n");
  check("a refused wire write fails the call", !dev.setPointOfDelivery("HOME"));

  /* Cache untouched: the very next push (e.g. from setMeterType()) must
   * still carry the OLD (unset, default "") PointOfDelivery, not the
   * refused "HOME". */
  s.expect("AT+MTMETERID=1,1,\"\",\"\",\"\",1,,", "OK\r\n");
  check("a later push still carries the default pod, proving the cache was never updated", dev.setMeterType(1));
}

/* ===== attributeChangeCB: Instance-served, documented no-op ===== */

static void test_attribute_change_cb_is_a_documented_noop(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  check("attributeChangeCB() returns true (started) and touches nothing observable", dev.attributeChangeCB(1, 0x0B06, 0, nullptr));
}

/* ===== the B229 reconcile pattern ===== */

static void test_reconcile_noop_when_nothing_configured(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);
  reconcile(dev);
  check("reconcile before any setter call issues no traffic", s.scriptDrained() && s.unexpected().empty());
}

/*
 * Build up the full identity across all five setters, THEN reconcile, and
 * confirm the reconcile line is the FULL identity, bypassing each setter's
 * own unchanged-value guard -- B229's whole point (this is defect B229 from
 * Round A, restated for a class where every field is configuration).
 */
static void test_reconcile_full_identity_and_post_reconcile_setter_still_noops(void) {
  MockStream s;
  MatterElectricalUtilityMeter dev;
  bringUp(s, dev);

  s.expect("AT+MTMETERID=1,0,\"\",\"\",\"\",1,,", "OK\r\n");
  check("seed the power threshold", dev.setPowerThreshold(true, 1, false, 0, false, 0));
  s.expect("AT+MTMETERID=1,1,\"\",\"\",\"\",1,,", "OK\r\n");
  check("setMeterType(1)", dev.setMeterType(1));
  s.expect("AT+MTMETERID=1,1,\"HOME\",\"\",\"\",1,,", "OK\r\n");
  check("setPointOfDelivery(HOME)", dev.setPointOfDelivery("HOME"));
  s.expect("AT+MTMETERID=1,1,\"HOME\",\"SN-1\",\"\",1,,", "OK\r\n");
  check("setSerialNumber(SN-1)", dev.setSerialNumber("SN-1"));
  s.expect("AT+MTMETERID=1,1,\"HOME\",\"SN-1\",\"v1.0\",1,,", "OK\r\n");
  check("setProtocolVersion(v1.0)", dev.setProtocolVersion("v1.0"));

  /* The reconcile: the FULL identity, unconditionally, exactly as it was
   * last committed to the cache. */
  s.expect("AT+MTMETERID=1,1,\"HOME\",\"SN-1\",\"v1.0\",1,,", "OK\r\n");
  reconcile(dev);
  check("reconcile re-pushed the full identity", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  /* Repeating any single setter with its already-cached value afterwards
   * is STILL a no-op: the reconcile push already put the fabric back in
   * sync, the same MatterEvse::hearthOnReconciled() CircuitCapacity shape. */
  check("setMeterType(1) after reconcile is still a no-op", dev.setMeterType(1));
  check("setPointOfDelivery(HOME) after reconcile is still a no-op", dev.setPointOfDelivery("HOME"));
  check("no traffic for either repeat", s.scriptDrained() && s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterElectricalUtilityMeter tests =====\n");
  g_yieldAdvanceMs = 1;

  test_begin_declares_0x0511_variant0();
  test_rebegin_after_reconcile_refused();
  test_setters_before_begin_and_before_reconcile();

  test_meter_type_before_threshold_refused();
  test_pod_serial_protocol_before_threshold_refused();

  test_both_power_values_absent_refused();
  test_power_threshold_pwr_only_pushes_exact_wire();
  test_power_threshold_apparent_only_pushes();
  test_power_threshold_src_nullable_absent_and_present();
  test_power_threshold_src_out_of_range_refused();

  test_meter_type_pushes_and_no_ops();
  test_meter_type_out_of_range_refused();

  test_point_of_delivery_comma_survives_unescaped();
  test_serial_number_64_bytes_accepted_65_refused();
  test_protocol_version_nonprintable_byte_refused();
  test_protocol_version_embedded_quote_refused();
  test_wire_refusal_leaves_cache_untouched();

  test_attribute_change_cb_is_a_documented_noop();

  test_reconcile_noop_when_nothing_configured();
  test_reconcile_full_identity_and_post_reconcile_setter_still_noops();

  g_yieldAdvanceMs = 0;

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
