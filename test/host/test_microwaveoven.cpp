/*
 * test_microwaveoven.cpp - Task 8 (RVC + Microwave batch, last library
 * task): MatterMicrowaveOven.
 *
 * Full validation matrix for the one mode list (MicrowaveOvenMode, S3.20.1's
 * grammar), the SetCookingParameters/AddMoreTime multi-field dispatch (Task
 * 6's HearthCmdFields, both the all-present and the sparse shape), and the
 * inherited OperationalState (cluster 0x0060) deferral chain: this class's
 * hearthOnForwardedCommandFields() must reach
 * MatterOperationalStateEndpoint::hearthOnForwardedCommand()'s
 * Pause/Stop/Start/Resume dispatch, proving the chain the header comment
 * documents (MatterOperationalStateEndpoint does not itself override
 * hearthOnForwardedCommandFields(), so the qualified call binds to
 * MatterEndPoint's own default body, which virtually calls
 * hearthOnForwardedCommand() and lands in MatterOperationalStateEndpoint's
 * override) actually works end to end, not just compiles.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterMicrowaveOven.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterMicrowaveOven &oven) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  oven.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0079\r\nOK\r\n");
  Matter.begin();
}

/* ===== begin() / declare ===== */

static void test_begin_declares_0x0079_variant0(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  check("declared as the microwave_oven device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0079);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", oven.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached OperationalState is Stopped (0)", oven.getOperationalState() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  check("a second begin() after Matter.begin() is refused", !oven.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== setSupportedModes(): AT+MTMODES, cluster 94 (MicrowaveOvenMode) ===== */

/* AT_MT_SPEC.md S3.20.1's own worked example ("AT+MTMODES=13,94,0,0,"Normal""),
 * transcribed onto endpoint 1. */
static void test_setsupportedmodes_exact_wire_pin(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Normal" };
  s.expect("AT+MTMODES=1,94,0,0,\"Normal\"", "OK\r\n");
  check("setSupportedModes sends the exact wire pin", oven.setSupportedModes(modes, tags, labels, 1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_multi_triple_explicit_tags(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0x4000, 0x4000 }; /* kNormal on every mode, S3.20.1's own table */
  const char *labels[] = { "Normal", "Reheat" };
  s.expect("AT+MTMODES=1,94,0,16384,\"Normal\",1,16384,\"Reheat\"", "OK\r\n");
  check("multi-triple call sends both, cluster 94", oven.setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_comma_in_label(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Eco, quiet" };
  s.expect("AT+MTMODES=1,94,0,0,\"Eco, quiet\"", "OK\r\n");
  check("a comma inside a quoted label is sent verbatim, not split", oven.setSupportedModes(modes, tags, labels, 1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ----- grammar validation: every rejection category ----- */

static void test_setsupportedmodes_rejects_zero_triples(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  check("0 triples is refused host-side", !oven.setSupportedModes(nullptr, nullptr, nullptr, 0));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_too_many_triples(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[9];
  uint16_t tags[9];
  const char *labels[9];
  char buf[9][8];
  for (int i = 0; i < 9; i++) {
    modes[i] = (uint8_t)i;
    tags[i] = 0;
    snprintf(buf[i], sizeof(buf[i]), "M%d", i);
    labels[i] = buf[i];
  }
  check("9 triples exceeds the 8-triple cap and is refused", !oven.setSupportedModes(modes, tags, labels, 9));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_repeated_mode(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0, 0 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Normal", "Normal2" };
  check("a repeated mode value is refused host-side", !oven.setSupportedModes(modes, tags, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_empty_label(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "" };
  check("an empty label is refused host-side", !oven.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_oversized_label(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "123456789012345678901234567890123" }; /* 33 bytes, one over the 32-byte cap */
  check("label length 33 exceeds the 32-byte cap and is refused", strlen(labels[0]) == 33 && !oven.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_quote_in_label(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\"Label" };
  check("a label containing a double quote is refused", !oven.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_nonprintable_in_label(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\x01Label" };
  check("a label containing a non-printable byte is refused", !oven.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", oven.begin());
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Normal" };
  check("setSupportedModes() before reconcile fails", !oven.setSupportedModes(modes, tags, labels, 1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

static void test_setsupportedmodes_failed_write_leaves_cache_unresent(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Normal" };
  s.expect("AT+MTMODES=1,94,0,0,\"Normal\"", "ERROR\r\n");
  check("a rejected write returns false", !oven.setSupportedModes(modes, tags, labels, 1));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0079\r\nOK\r\n");
  Matter.begin();
  check("no mode list was resent (there is nothing cached)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== reconcile: cached list resent ===== */

static void test_reconcile_with_nothing_set_sends_nothing(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven); /* bringUp() itself already exercised one reconcile with nothing set */
  check("no AT+MTMODES traffic with nothing set", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_modelist_resent_on_next_reconcile(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Normal", "Reheat" };
  s.expect("AT+MTMODES=1,94,0,0,\"Normal\",1,0,\"Reheat\"", "OK\r\n");
  check("setSupportedModes succeeds", oven.setSupportedModes(modes, tags, labels, 2));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0079\r\nOK\r\n");
  s.expect("AT+MTMODES=1,94,0,0,\"Normal\",1,0,\"Reheat\"", "OK\r\n"); /* resent */
  Matter.begin(); /* a second reconcile, e.g. a sketch's repeated loop() call */

  check("the list was resent verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== SetCookingParameters forwards: cluster 95 (MicrowaveOvenControl) ===== */

/* All four fields present, the shape S3.17 says is the only one this
 * firmware's actual wire traffic ever sends. */
static void test_cookingparameters_all_present_allow(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  bool called = false;
  HearthCookingParams seen = {};
  oven.onCookingParameters([&](const HearthCookingParams &p) {
    called = true;
    seen = p;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,95,0,2,90,80,1"); /* cookMode=2, cookTime=90, power=80, startAfter=1 */
  Hearth.poll();
  check("onCookingParameters fired", called);
  check("hasCookMode true, cookMode 2", seen.hasCookMode && seen.cookMode == 2);
  check("hasCookTime true, cookTimeSec 90", seen.hasCookTime && seen.cookTimeSec == 90);
  check("hasPower true, powerPercent 80", seen.hasPower && seen.powerPercent == 80);
  check("startAfterSetting true", seen.startAfterSetting == true);
  check("an allowing verdict answers AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The wire-grammar-legal sparse shape (Task 6's ",,30,,1" example): only
 * cookTime and startAfter present, cookMode and power absent. */
static void test_cookingparameters_sparse_fields(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  bool called = false;
  HearthCookingParams seen = {};
  oven.onCookingParameters([&](const HearthCookingParams &p) {
    called = true;
    seen = p;
    return true;
  });
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,1,95,0,,30,,1"); /* cookMode and power absent */
  Hearth.poll();
  check("onCookingParameters fired", called);
  check("hasCookMode false, cookMode defaults 0", !seen.hasCookMode && seen.cookMode == 0);
  check("hasCookTime true, cookTimeSec 30", seen.hasCookTime && seen.cookTimeSec == 30);
  check("hasPower false, powerPercent defaults 0", !seen.hasPower && seen.powerPercent == 0);
  check("startAfterSetting true (the present field's value)", seen.startAfterSetting == true);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Only cookMode present; startAfter absent entirely (line ends early) must
 * default startAfterSetting to false, not fabricate a true. */
static void test_cookingparameters_startafter_absent_defaults_false(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  HearthCookingParams seen = {};
  seen.startAfterSetting = true; /* poison, must be overwritten to false */
  oven.onCookingParameters([&](const HearthCookingParams &p) {
    seen = p;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,95,0,3"); /* only cookMode present, no cookTime/power/startAfter positions at all */
  Hearth.poll();
  check("hasCookMode true, cookMode 3", seen.hasCookMode && seen.cookMode == 3);
  check("hasCookTime false", !seen.hasCookTime);
  check("hasPower false", !seen.hasPower);
  check("startAfterSetting defaults false when the field is absent", seen.startAfterSetting == false);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cookingparameters_deny(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onCookingParameters([](const HearthCookingParams &) { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,95,0,2,90,80,1");
  Hearth.poll();
  check("a denying verdict answers AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cookingparameters_no_callback_denies(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  /* no onCookingParameters() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,95,0,2,90,80,1");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== AddMoreTime forwards: cluster 95, command 1 ===== */

static void test_addmoretime_allow_carries_absolute_value(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  bool called = false;
  uint32_t seen = 0;
  oven.onAddMoreTime([&](uint32_t finalCookTimeSec) {
    called = true;
    seen = finalCookTimeSec;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,95,1,150"); /* AddMoreTime, absolute finalCookTimeSec 150 */
  Hearth.poll();
  check("onAddMoreTime fired with the absolute value, not a delta", called && seen == 150);
  check("an allowing verdict answers AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_addmoretime_deny(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onAddMoreTime([](uint32_t) { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,95,1,150");
  Hearth.poll();
  check("a denying verdict answers AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_addmoretime_no_callback_denies(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  /* no onAddMoreTime() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,95,1,150");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== unknown command on cluster 95: no fallback to the deferral chain ===== */

static void test_unrecognised_control_command_denies(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onCookingParameters([](const HearthCookingParams &) { return true; });
  oven.onAddMoreTime([](uint32_t) { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,95,9"); /* command id 9: neither SetCookingParameters nor AddMoreTime */
  Hearth.poll();
  check("an unrecognised MicrowaveOvenControl command id denies via the base default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * ===== Inherited OperationalState (cluster 96) deferral chain regression =====
 *
 * The whole point of this class's hearthOnForwardedCommandFields(): every
 * command NOT on cluster 95 must still reach
 * MatterOperationalStateEndpoint::hearthOnForwardedCommand()'s
 * Pause/Stop/Start/Resume dispatch. These four mirror
 * test_laundrywasher.cpp's own forwarded-command matrix exactly, proving the
 * chain works from THIS class's entry point, not merely that the base class
 * works in isolation.
 */

static void test_forwarded_pause_allow_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,0"); /* Pause */
  Hearth.poll();
  check("an allowing onPause() (inherited) answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_pause_deny_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onPause([]() { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,96,0");
  Hearth.poll();
  check("a denying onPause() (inherited) answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_stop_allow_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onStop([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,1"); /* Stop */
  Hearth.poll();
  check("an allowing onStop() (inherited) answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_start_allow_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onStart([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,2"); /* Start */
  Hearth.poll();
  check("an allowing onStart() (inherited) answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_resume_allow_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onResume([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,3"); /* Resume */
  Hearth.poll();
  check("an allowing onResume() (inherited) answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_opstate_no_callback_denies_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  /* no onPause() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,0");
  Hearth.poll();
  check("no callback registered denies (fail closed), via the deferral chain", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_wrong_cluster_denies(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0"); /* DoorLock's cluster, none of this class's three */
  Hearth.poll();
  check("an unrelated cluster id denies via the deferral chain's own base default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_unrecognised_opstate_id_denies_via_deferral(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  oven.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,9"); /* command id 9: not Pause/Stop/Start/Resume */
  Hearth.poll();
  check("an unrecognised OperationalState command id denies via the deferral chain", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* setOperationalState()/getOperationalState() (inherited, AT+MTOPSTATE): one
 * exact-pin regression, proving the inherited setter/getter still work
 * unmodified through this subclass too. */
static void test_setoperationalstate_inherited_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterMicrowaveOven oven;
  bringUp(s, oven);
  s.expect("AT+MTOPSTATE=1,1", "OK\r\n"); /* Running */
  check("setOperationalState(Running) sends the exact wire pin", oven.setOperationalState(MatterMicrowaveOven::kStateRunning));
  check("cache updated to Running", oven.getOperationalState() == MatterMicrowaveOven::kStateRunning);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterMicrowaveOven tests =====\n");
  test_begin_declares_0x0079_variant0();
  test_rebegin_after_reconcile_refused();
  test_setsupportedmodes_exact_wire_pin();
  test_setsupportedmodes_multi_triple_explicit_tags();
  test_setsupportedmodes_comma_in_label();
  test_setsupportedmodes_rejects_zero_triples();
  test_setsupportedmodes_rejects_too_many_triples();
  test_setsupportedmodes_rejects_repeated_mode();
  test_setsupportedmodes_rejects_empty_label();
  test_setsupportedmodes_rejects_oversized_label();
  test_setsupportedmodes_rejects_quote_in_label();
  test_setsupportedmodes_rejects_nonprintable_in_label();
  test_setsupportedmodes_before_reconcile_fails_without_traffic();
  test_setsupportedmodes_failed_write_leaves_cache_unresent();
  test_reconcile_with_nothing_set_sends_nothing();
  test_modelist_resent_on_next_reconcile();
  test_cookingparameters_all_present_allow();
  test_cookingparameters_sparse_fields();
  test_cookingparameters_startafter_absent_defaults_false();
  test_cookingparameters_deny();
  test_cookingparameters_no_callback_denies();
  test_addmoretime_allow_carries_absolute_value();
  test_addmoretime_deny();
  test_addmoretime_no_callback_denies();
  test_unrecognised_control_command_denies();
  test_forwarded_pause_allow_via_deferral();
  test_forwarded_pause_deny_via_deferral();
  test_forwarded_stop_allow_via_deferral();
  test_forwarded_start_allow_via_deferral();
  test_forwarded_resume_allow_via_deferral();
  test_forwarded_opstate_no_callback_denies_via_deferral();
  test_forwarded_command_wrong_cluster_denies();
  test_forwarded_command_unrecognised_opstate_id_denies_via_deferral();
  test_setoperationalstate_inherited_exact_wire_pin_and_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
