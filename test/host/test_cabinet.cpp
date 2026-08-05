#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterTemperatureControlledCabinet.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* ===== TemperatureNumber mode (variant 0) ===== */

/*
 * Fix round 2 (bench bug): Matter.begin()'s reconcile now pushes the four
 * cached TN values to the wire (min, max, step, setpoint order; see
 * hearthOnReconciled()'s header comment for why). bringUpTN() therefore
 * queues those four exchanges after the AT+MTEP? query, computed from the
 * same values passed to begin(), so every existing TN test's MockStream
 * script stays exact without hand-maintaining four raw numbers per call
 * site.
 */
static void bringUpTN(
  MockStream &s, MatterTemperatureControlledCabinet &c, double setpoint = 0.00, double minTemp = -10.0, double maxTemp = 32.0,
  double step = 0.50
) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  c.begin(setpoint, minTemp, maxTemp, step);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n"); /* variant 0: 3-field line */

  int16_t rawMin = static_cast<int16_t>(minTemp * 100.0);
  int16_t rawMax = static_cast<int16_t>(maxTemp * 100.0);
  int16_t rawStep = static_cast<int16_t>(step * 100.0);
  int16_t rawSetpoint = static_cast<int16_t>(setpoint * 100.0);
  char cmd[48], reply[48];
  snprintf(cmd, sizeof(cmd), "AT+MTATTR=1,86,1,%d,1", (int)rawMin);
  snprintf(reply, sizeof(reply), "+MTATTR:1,86,1,%d\r\nOK\r\n", (int)rawMin);
  s.expect(cmd, reply);
  snprintf(cmd, sizeof(cmd), "AT+MTATTR=1,86,2,%d,1", (int)rawMax);
  snprintf(reply, sizeof(reply), "+MTATTR:1,86,2,%d\r\nOK\r\n", (int)rawMax);
  s.expect(cmd, reply);
  snprintf(cmd, sizeof(cmd), "AT+MTATTR=1,86,3,%d,1", (int)rawStep);
  snprintf(reply, sizeof(reply), "+MTATTR:1,86,3,%d\r\nOK\r\n", (int)rawStep);
  s.expect(cmd, reply);
  snprintf(cmd, sizeof(cmd), "AT+MTATTR=1,86,0,%d,1", (int)rawSetpoint);
  snprintf(reply, sizeof(reply), "+MTATTR:1,86,0,%d\r\nOK\r\n", (int)rawSetpoint);
  s.expect(cmd, reply);

  Matter.begin();
}

static void test_tn_begin_declares_variant0(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c);
  check("declared as the cabinet device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0071);
  check("declared variant 0 (TemperatureNumber)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", c.getEndPointId() == 1);
  /* c.begin() itself still issues nothing; the reconcile push happens
   * inside Matter.begin(), which bringUpTN() already called. */
  check("exactly the declaration query plus the four-value reconcile push happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("default setpoint is 0.00C", c.getTemperatureSetpoint() > -0.01 && c.getTemperatureSetpoint() < 0.01);
  check("default min is -10.0C", c.getMinTemperature() > -10.01 && c.getMinTemperature() < -9.99);
  check("default max is 32.0C", c.getMaxTemperature() > 31.99 && c.getMaxTemperature() < 32.01);
  check("default step is 0.50C", c.getStep() > 0.49 && c.getStep() < 0.51);
}

/* Pins upstream's exact double->raw conversion: static_cast<int16_t>(v * 100.0). */
static void test_tn_setpoint_write_uses_upstream_conversion(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c);
  s.expect("AT+MTATTR=1,86,0,2150,1", "+MTATTR:1,86,0,2150\r\nOK\r\n");
  check("setTemperatureSetpoint(21.50) succeeds", c.setTemperatureSetpoint(21.50));
  check("cached as 21.50C", c.getTemperatureSetpoint() > 21.49 && c.getTemperatureSetpoint() < 21.51);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tn_min_max_step_write_their_own_attributes(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c);
  s.expect("AT+MTATTR=1,86,1,-500,1", "+MTATTR:1,86,1,-500\r\nOK\r\n");
  check("setMinTemperature(-5.00) succeeds", c.setMinTemperature(-5.00));
  s.expect("AT+MTATTR=1,86,2,3000,1", "+MTATTR:1,86,2,3000\r\nOK\r\n");
  check("setMaxTemperature(30.00) succeeds", c.setMaxTemperature(30.00));
  s.expect("AT+MTATTR=1,86,3,100,1", "+MTATTR:1,86,3,100\r\nOK\r\n");
  check("setStep(1.00) succeeds", c.setStep(1.00));
  check("min cached", c.getMinTemperature() > -5.01 && c.getMinTemperature() < -4.99);
  check("max cached", c.getMaxTemperature() > 29.99 && c.getMaxTemperature() < 30.01);
  check("step cached", c.getStep() > 0.99 && c.getStep() < 1.01);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tn_setpoint_out_of_range_is_refused(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c); /* default range [-10.0, 32.0] */
  check("40.0C exceeds the 32.0C max and is refused", !c.setTemperatureSetpoint(40.0));
  check("setpoint unchanged", c.getTemperatureSetpoint() > -0.01 && c.getTemperatureSetpoint() < 0.01);
  check("no AT traffic was issued", s.scriptDrained());
}

static void test_tn_failed_write_does_not_update_cache(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c);
  s.expect("AT+MTATTR=1,86,0,2150,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected setpoint write returns false", !c.setTemperatureSetpoint(21.50));
  check("and does not update the cache", c.getTemperatureSetpoint() > -0.01 && c.getTemperatureSetpoint() < 0.01);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tn_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c);
  check("a second begin() after Matter.begin() is refused", !c.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached setpoint was not overwritten", c.getTemperatureSetpoint() > -0.01 && c.getTemperatureSetpoint() < 0.01);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/*
 * Fix round 2 (bench bug, C6): begin(21.5, -5.0, 30.0, 0.5) followed by
 * Matter.begin() left MinTemperature/MaxTemperature/Step at the C6's
 * esp-matter defaults (0/10/1) forever, because setRawMinTemperature() et
 * al.'s skip-if-equal short-circuited on a cache that was seeded from the
 * sketch's own arguments, never from the device (which boots at esp-matter's
 * defaults, not the sketch's). A controller's SetTemperature command then
 * answered CONSTRAINT_ERROR against bounds the sketch believed it had set.
 * Reproduced on the bench; raw AT writes of the same values propagated fine,
 * exonerating the firmware. Fixed by pushing the four cached values directly
 * (bypassing the setters' skip-if-equal) from hearthOnReconciled(), the same
 * hook TL mode already uses for its own state. Pins the bug's own exact
 * reported scenario.
 */
static void test_tn_reconcile_pushes_the_four_cached_values_in_order(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s; MatterTemperatureControlledCabinet c;
  Hearth.begin(s);
  check("TN begin(21.5, -5.0, 30.0, 0.5) succeeds", c.begin(21.5, -5.0, 30.0, 0.5));
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,1,-500,1", "+MTATTR:1,86,1,-500\r\nOK\r\n");  /* MinTemperature */
  s.expect("AT+MTATTR=1,86,2,3000,1", "+MTATTR:1,86,2,3000\r\nOK\r\n");  /* MaxTemperature */
  s.expect("AT+MTATTR=1,86,3,50,1", "+MTATTR:1,86,3,50\r\nOK\r\n");      /* Step */
  s.expect("AT+MTATTR=1,86,0,2150,1", "+MTATTR:1,86,0,2150\r\nOK\r\n");  /* TemperatureSetpoint */
  Matter.begin();

  check("all four writes reached the wire during reconcile, in min/max/step/setpoint order", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("setpoint cached at 21.5C", c.getTemperatureSetpoint() > 21.49 && c.getTemperatureSetpoint() < 21.51);
  check("min cached at -5.0C", c.getMinTemperature() > -5.01 && c.getMinTemperature() < -4.99);
  check("max cached at 30.0C", c.getMaxTemperature() > 29.99 && c.getMaxTemperature() < 30.01);
  check("step cached at 0.5C", c.getStep() > 0.49 && c.getStep() < 0.51);
}

/* Once the reconcile push has synced the device to the cache, cache==device
 * holds, so the setters' pre-existing skip-if-equal is now correct: calling
 * a setter with exactly the value begin() already pushed is a genuine no-op
 * on the wire, not a silently-skipped divergent write. */
static void test_tn_setter_with_begun_value_is_noop_after_reconcile_push(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c, 21.5, -5.0, 30.0, 0.5); /* reconcile push already put these on the wire */
  check("setMinTemperature(-5.0), the same value begin() used, succeeds", c.setMinTemperature(-5.0));
  check("and is a genuine no-op: the push already set this on the wire", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A setter value that genuinely differs from what the reconcile push sent
 * still reaches the wire: the fix must not turn every TN setter into a
 * permanent no-op, only skip writes that are truly redundant. */
static void test_tn_setter_with_new_value_still_writes_after_reconcile_push(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  bringUpTN(s, c, 21.5, -5.0, 30.0, 0.5);
  s.expect("AT+MTATTR=1,86,1,-800,1", "+MTATTR:1,86,1,-800\r\nOK\r\n");
  check("setMinTemperature(-8.0), a genuinely new value, still writes", c.setMinTemperature(-8.0));
  check("min cached at -8.0C", c.getMinTemperature() > -8.01 && c.getMinTemperature() < -7.99);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== TemperatureLevel mode (variant 1) ===== */

static void bringUpTL(
  MockStream &s, MatterTemperatureControlledCabinet &c, uint8_t *levels, uint16_t count, uint8_t selected,
  const char *labelsWire
) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("TL begin() succeeds", c.begin(levels, count, selected));
  s.expect("AT+MTEP?", "+MTEP:0,4,0x0071,1\r\nOK\r\n"); /* variant 1: 4-field line */
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+MTTEMPLEVELS=4,%s", labelsWire);
  s.expect(cmd, "OK\r\n");
  snprintf(cmd, sizeof(cmd), "AT+MTATTR=4,86,4,%u,1", (unsigned)selected);
  char reply[64];
  snprintf(reply, sizeof(reply), "+MTATTR:4,86,4,%u\r\nOK\r\n", (unsigned)selected);
  s.expect(cmd, reply);
  Matter.begin();
}

static void test_tl_begin_declares_variant1_sends_generated_labels_and_selected_level(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 1, "\"Level 0\",\"Level 1\",\"Level 2\"");
  check("declared as the cabinet device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0071);
  check("declared variant 1 (TemperatureLevel)", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
  check("adopted endpoint 4", c.getEndPointId() == 4);
  check("labels and selected level reached the wire at reconcile", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("selected level cached", c.getSelectedTemperatureLevel() == 1);
  check("supported level count cached", c.getSupportedTemperatureLevelsCount() == 3);
}

/* Fix round 2: TL mode is unchanged by the TN reconcile-push fix. Its own
 * bringUpTL() script is exact (labels, then SelectedTemperatureLevel, and
 * nothing else), so this is confirmatory: a regression that pushed TN
 * attributes for a TL-mode cabinet too would show up as an unexpected
 * command here, since TL mode has no MinTemperature/MaxTemperature/Step/
 * TemperatureSetpoint attributes on the live cluster at all (the fifth
 * abort trap makes the two feature sets mutually exclusive). */
static void test_tl_reconcile_pushes_only_labels_and_level_no_tn_attributes(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 1, "\"Level 0\",\"Level 1\",\"Level 2\"");
  check("TL reconcile pushed exactly labels + selected level, nothing else", s.scriptDrained());
  check("no unexpected commands (no TN attribute writes)", s.unexpected().empty());
}

/* Generated labels use the level IDENTIFIER, not its array index. */
static void test_tl_generated_labels_use_level_identifier_not_index(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 10, 20, 30 };
  bringUpTL(s, c, levels, 3, 20, "\"Level 10\",\"Level 20\",\"Level 30\"");
  check("selected level 20 cached", c.getSelectedTemperatureLevel() == 20);
  check("no unexpected commands", s.unexpected().empty());
}

/* Pins the exact quoted wire string, including a comma INSIDE a label, which
 * AT_MT_SPEC.md S3.16 states is legal and part of the label's own text. */
static void test_set_labels_sends_exact_quoted_wire_with_comma_in_a_label(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  const char *labels[] = { "Cold", "Warm, ish", "Hot" };
  s.expect("AT+MTTEMPLEVELS=4,\"Cold\",\"Warm, ish\",\"Hot\"", "OK\r\n");
  check("setSupportedTemperatureLevelLabels succeeds", c.setSupportedTemperatureLevelLabels(labels, 3));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Labels are not persisted by the firmware (S3.16): the custom set must be
 * re-sent, verbatim, on every later reconcile, not just the one that set it. */
static void test_custom_labels_are_resent_on_next_reconcile(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  const char *labels[] = { "Cold", "Warm", "Hot" };
  s.expect("AT+MTTEMPLEVELS=4,\"Cold\",\"Warm\",\"Hot\"", "OK\r\n");
  check("setSupportedTemperatureLevelLabels succeeds", c.setSupportedTemperatureLevelLabels(labels, 3));

  s.expect("AT+MTEP?", "+MTEP:0,4,0x0071,1\r\nOK\r\n");
  s.expect("AT+MTTEMPLEVELS=4,\"Cold\",\"Warm\",\"Hot\"", "OK\r\n"); /* custom, not "Level N" */
  s.expect("AT+MTATTR=4,86,4,0,1", "+MTATTR:4,86,4,0\r\nOK\r\n");
  Matter.begin(); /* a second reconcile, e.g. a sketch's repeated loop() call */

  check("the custom labels were resent verbatim, not the generated defaults", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_selected_temperature_level_controller_urc_dispatches(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  s.injectURC("+MTATTR:4,86,4,2");
  Hearth.poll();
  check("selected level updated from the controller URC", c.getSelectedTemperatureLevel() == 2);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tl_failed_selected_level_write_does_not_update_cache(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  s.expect("AT+MTATTR=4,86,4,2,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected selected-level write returns false", !c.setSelectedTemperatureLevel(2));
  check("and does not update the cache", c.getSelectedTemperatureLevel() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_labels_write_does_not_update_cache(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  const char *labels[] = { "Cold", "Warm", "Hot" };
  s.expect("AT+MTTEMPLEVELS=4,\"Cold\",\"Warm\",\"Hot\"", "ERROR\r\n");
  check("a rejected labels write returns false", !c.setSupportedTemperatureLevelLabels(labels, 3));

  s.expect("AT+MTEP?", "+MTEP:0,4,0x0071,1\r\nOK\r\n");
  s.expect("AT+MTTEMPLEVELS=4,\"Level 0\",\"Level 1\",\"Level 2\"", "OK\r\n"); /* the OLD (generated) labels, not "Cold"/"Warm"/"Hot" */
  s.expect("AT+MTATTR=4,86,4,0,1", "+MTATTR:4,86,4,0\r\nOK\r\n");
  Matter.begin();
  check("the failed write did not overwrite the cache", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tl_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 1, "\"Level 0\",\"Level 1\",\"Level 2\"");

  uint8_t moreLevels[] = { 5, 6 };
  check("a second begin() after Matter.begin() is refused", !c.begin(moreLevels, 2, 5));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached selected level was not overwritten", c.getSelectedTemperatureLevel() == 1);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* Validate-before-declare (MatterThermostat.h's deviation 1 precedent):
 * a selectedLevel absent from supportedLevels is refused before any
 * registry slot is consumed. */
static void test_tl_begin_rejects_selected_level_not_in_array(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s; MatterTemperatureControlledCabinet c;
  Hearth.begin(s);
  uint8_t levels[] = { 0, 1, 2 };
  check("selectedLevel 9 is not in the array and is refused", !c.begin(levels, 3, 9));
  check("no registry slot was consumed", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no AT traffic was issued", s.scriptDrained());
}

static void test_tl_begin_rejects_too_many_levels(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s; MatterTemperatureControlledCabinet c;
  Hearth.begin(s);
  uint8_t levels[17];
  for (int i = 0; i < 17; i++) {
    levels[i] = (uint8_t)i;
  }
  check("17 levels exceeds the 16-entry cap and is refused", !c.begin(levels, 17, 0));
  check("no registry slot was consumed", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no AT traffic was issued", s.scriptDrained());
}

static void test_16_entry_label_cap_enforced_host_side(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  const char *labels[17];
  for (int i = 0; i < 17; i++) {
    labels[i] = "X";
  }
  check("17 labels exceeds the 16-entry cap and is refused", !c.setSupportedTemperatureLevelLabels(labels, 17));
  check("reports the grammar/label-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_16_char_label_cap_enforced_host_side(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  const char *labels[] = { "12345678901234567" }; /* 17 bytes, one over the 16-byte cap */
  check("label length 17 exceeds the 16-byte cap and is refused", strlen(labels[0]) == 17 && !c.setSupportedTemperatureLevelLabels(labels, 1));
  check("reports the grammar/label-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Review fix round 1, Important finding: a label containing a '"' must be
 * rejected host-side, not sent through unescaped -- AT_MT_SPEC.md S3.16
 * forbids a double-quote character inside a label, and an unescaped one
 * would corrupt the AT+MTTEMPLEVELS line's own field boundary at the
 * firmware parser rather than coming back as a clean +MTERR:1. Also pins a
 * non-printable byte (the same grammar clause), and that a rejected call
 * leaves the cache untouched: a later reconcile still resends the OLD
 * (generated) labels, not the rejected content.
 */
static void test_label_content_grammar_enforced_host_side(void) {
  MockStream s; MatterTemperatureControlledCabinet c;
  uint8_t levels[] = { 0, 1, 2 };
  bringUpTL(s, c, levels, 3, 0, "\"Level 0\",\"Level 1\",\"Level 2\"");

  const char *quoteLabels[] = { "Bad\"Label" };
  check("a label containing a double quote is refused", !c.setSupportedTemperatureLevelLabels(quoteLabels, 1));
  check("reports the grammar/label-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued for the quote violation", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  const char *nonPrintableLabels[] = { "Bad\x01Label" };
  check("a label containing a non-printable byte is refused", !c.setSupportedTemperatureLevelLabels(nonPrintableLabels, 1));
  check("reports the grammar/label-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued for the non-printable violation", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  s.expect("AT+MTEP?", "+MTEP:0,4,0x0071,1\r\nOK\r\n");
  s.expect("AT+MTTEMPLEVELS=4,\"Level 0\",\"Level 1\",\"Level 2\"", "OK\r\n"); /* still the OLD generated labels */
  s.expect("AT+MTATTR=4,86,4,0,1", "+MTATTR:4,86,4,0\r\nOK\r\n");
  Matter.begin();
  check("both rejected calls left the cache untouched", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tn_type_and_tl_type_report_the_right_attr_types(void) {
  MatterTemperatureControlledCabinet c;
  check(
    "TemperatureSetpoint is int16", c.hearthAttrTypeFor(86, 0) == ESP_MATTER_VAL_TYPE_INT16
  );
  check("MinTemperature is int16", c.hearthAttrTypeFor(86, 1) == ESP_MATTER_VAL_TYPE_INT16);
  check("MaxTemperature is int16", c.hearthAttrTypeFor(86, 2) == ESP_MATTER_VAL_TYPE_INT16);
  check("Step is int16", c.hearthAttrTypeFor(86, 3) == ESP_MATTER_VAL_TYPE_INT16);
  check("SelectedTemperatureLevel is uint8", c.hearthAttrTypeFor(86, 4) == ESP_MATTER_VAL_TYPE_UINT8);
  check("an unrelated cluster falls through to the base default", c.hearthAttrTypeFor(6, 0) == ESP_MATTER_VAL_TYPE_INTEGER);
}

int main(void) {
  printf("\n===== MatterTemperatureControlledCabinet tests =====\n");
  test_tn_begin_declares_variant0();
  test_tn_setpoint_write_uses_upstream_conversion();
  test_tn_min_max_step_write_their_own_attributes();
  test_tn_setpoint_out_of_range_is_refused();
  test_tn_failed_write_does_not_update_cache();
  test_tn_rebegin_after_reconcile_refused();
  test_tn_reconcile_pushes_the_four_cached_values_in_order();
  test_tn_setter_with_begun_value_is_noop_after_reconcile_push();
  test_tn_setter_with_new_value_still_writes_after_reconcile_push();
  test_tl_begin_declares_variant1_sends_generated_labels_and_selected_level();
  test_tl_reconcile_pushes_only_labels_and_level_no_tn_attributes();
  test_tl_generated_labels_use_level_identifier_not_index();
  test_set_labels_sends_exact_quoted_wire_with_comma_in_a_label();
  test_custom_labels_are_resent_on_next_reconcile();
  test_selected_temperature_level_controller_urc_dispatches();
  test_tl_failed_selected_level_write_does_not_update_cache();
  test_failed_labels_write_does_not_update_cache();
  test_tl_rebegin_after_reconcile_refused();
  test_tl_begin_rejects_selected_level_not_in_array();
  test_tl_begin_rejects_too_many_levels();
  test_16_entry_label_cap_enforced_host_side();
  test_16_char_label_cap_enforced_host_side();
  test_label_content_grammar_enforced_host_side();
  test_tn_type_and_tl_type_report_the_right_attr_types();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
