/*
 * test_roboticvacuum.cpp - Task 7 (RVC + Microwave batch): MatterRoboticVacuum.
 *
 * Full validation matrix (every category the wire grammar rejects, S3.20.1)
 * runs once, through setSupportedRunModes(); setSupportedCleanModes() gets
 * a wire-pin success case plus one reject (duplicate mode) to prove the
 * identical shared validator (hearthSetModeList()) is actually reached from
 * both public setters, not a full second copy of every category -- the
 * validator itself is the same code path for both, already proven exhaustively
 * against the run-mode setter.
 *
 * The CurrentMode caching correction (this batch's firmware Task 3, binding,
 * see the class header's own section) gets its own explicit regression test:
 * an injected +MTATTR for one of this class's clusters must NOT move the
 * cache, since attributeChangeCB() is a documented no-op here. Only an
 * ALLOWED hearthOnForwardedCommandFields() ChangeToMode verdict may.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterRoboticVacuum.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterRoboticVacuum &vac) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  vac.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0074\r\nOK\r\n");
  Matter.begin();
}

/* ===== begin() / declare ===== */

static void test_begin_declares_0x0074_variant0(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  check("declared as the robotic_vacuum_cleaner device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0074);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", vac.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached OperationalState is Stopped (0)", vac.getOperationalState() == 0);
  check("initial cached CurrentRunMode is 0", vac.getCurrentRunMode() == 0);
  check("initial cached CurrentCleanMode is 0", vac.getCurrentCleanMode() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  check("a second begin() after Matter.begin() is refused", !vac.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== setSupportedRunModes() / setSupportedCleanModes(): AT+MTMODES ===== */

/* AT_MT_SPEC.md S3.20.1's own worked example, transcribed onto endpoint 1. */
static void test_setsupportedrunmodes_explicit_tags_exact_wire_pin(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0x4000, 0x4001 };
  const char *labels[] = { "Idle", "Cleaning" };
  s.expect("AT+MTMODES=1,84,0,16384,\"Idle\",1,16385,\"Cleaning\"", "OK\r\n");
  check("setSupportedRunModes sends the exact wire pin with explicit tags", vac.setSupportedRunModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_tag_zero_rendering(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Idle", "Cleaning" };
  s.expect("AT+MTMODES=1,84,0,0,\"Idle\",1,0,\"Cleaning\"", "OK\r\n");
  check("tag 0 is rendered literally (firmware substitutes the default, not the host)", vac.setSupportedRunModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedcleanmodes_exact_wire_pin(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Vacuum", "Mop" };
  s.expect("AT+MTMODES=1,85,0,0,\"Vacuum\",1,0,\"Mop\"", "OK\r\n");
  check("setSupportedCleanModes sends cluster 85, not 84", vac.setSupportedCleanModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A comma inside a quoted label is legal (S3.20.1 references S3.20's own
 * rule verbatim). */
static void test_setsupportedrunmodes_comma_in_label(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Eco, quiet" };
  s.expect("AT+MTMODES=1,84,0,0,\"Eco, quiet\"", "OK\r\n");
  check("a comma inside a quoted label is sent verbatim, not split", vac.setSupportedRunModes(modes, tags, labels, 1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ----- grammar validation, run modes: every rejection category ----- */

static void test_setsupportedrunmodes_rejects_zero_triples(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  check("0 triples is refused host-side", !vac.setSupportedRunModes(nullptr, nullptr, nullptr, 0));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_rejects_too_many_triples(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
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
  check("9 triples exceeds the 8-triple cap and is refused", !vac.setSupportedRunModes(modes, tags, labels, 9));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_rejects_repeated_mode(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0, 0 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Idle", "Idle2" };
  check("a repeated mode value is refused host-side", !vac.setSupportedRunModes(modes, tags, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_rejects_empty_label(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "" };
  check("an empty label is refused host-side", !vac.setSupportedRunModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_rejects_oversized_label(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "123456789012345678901234567890123" }; /* 33 bytes, one over the 32-byte cap */
  check("label length 33 exceeds the 32-byte cap and is refused", strlen(labels[0]) == 33 && !vac.setSupportedRunModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_rejects_quote_in_label(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\"Label" };
  check("a label containing a double quote is refused", !vac.setSupportedRunModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_rejects_nonprintable_in_label(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\x01Label" };
  check("a label containing a non-printable byte is refused", !vac.setSupportedRunModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedrunmodes_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", vac.begin());
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Idle" };
  check("setSupportedRunModes() before reconcile fails", !vac.setSupportedRunModes(modes, tags, labels, 1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

static void test_setsupportedrunmodes_failed_write_leaves_cache_unresent(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Idle" };
  s.expect("AT+MTMODES=1,84,0,0,\"Idle\"", "ERROR\r\n");
  check("a rejected write returns false", !vac.setSupportedRunModes(modes, tags, labels, 1));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0074\r\nOK\r\n");
  Matter.begin();
  check("no run-mode list was resent (there is nothing cached)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ----- grammar validation, clean modes: proves the shared validator is
 * actually reached from this setter too (one representative category) ----- */

static void test_setsupportedcleanmodes_rejects_repeated_mode(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t modes[] = { 0, 0 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Vacuum", "Vacuum2" };
  check("a repeated mode value is refused host-side on the clean-mode setter too", !vac.setSupportedCleanModes(modes, tags, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== reconcile: both cached lists resent, independently ===== */

static void test_reconcile_with_nothing_set_sends_nothing(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac); /* bringUp() itself already exercised one reconcile with nothing set */
  check("no AT+MTMODES traffic with nothing set", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_both_modelists_resent_on_next_reconcile(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t runModes[] = { 0, 1 };
  uint16_t runTags[] = { 0, 0 };
  const char *runLabels[] = { "Idle", "Cleaning" };
  s.expect("AT+MTMODES=1,84,0,0,\"Idle\",1,0,\"Cleaning\"", "OK\r\n");
  check("setSupportedRunModes succeeds", vac.setSupportedRunModes(runModes, runTags, runLabels, 2));

  uint8_t cleanModes[] = { 0, 1 };
  uint16_t cleanTags[] = { 0, 0 };
  const char *cleanLabels[] = { "Vacuum", "Mop" };
  s.expect("AT+MTMODES=1,85,0,0,\"Vacuum\",1,0,\"Mop\"", "OK\r\n");
  check("setSupportedCleanModes succeeds", vac.setSupportedCleanModes(cleanModes, cleanTags, cleanLabels, 2));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0074\r\nOK\r\n");
  s.expect("AT+MTMODES=1,84,0,0,\"Idle\",1,0,\"Cleaning\"", "OK\r\n");   /* run modes resent */
  s.expect("AT+MTMODES=1,85,0,0,\"Vacuum\",1,0,\"Mop\"", "OK\r\n");     /* clean modes resent */
  Matter.begin(); /* a second reconcile, e.g. a sketch's repeated loop() call */

  check("both lists were resent verbatim, in run-then-clean order", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Only ONE list set: reconcile must resend that one and stay silent about
 * the other. */
static void test_only_set_list_is_resent_on_reconcile(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  uint8_t cleanModes[] = { 0 };
  uint16_t cleanTags[] = { 0 };
  const char *cleanLabels[] = { "Vacuum" };
  s.expect("AT+MTMODES=1,85,0,0,\"Vacuum\"", "OK\r\n");
  check("setSupportedCleanModes succeeds", vac.setSupportedCleanModes(cleanModes, cleanTags, cleanLabels, 1));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0074\r\nOK\r\n");
  s.expect("AT+MTMODES=1,85,0,0,\"Vacuum\"", "OK\r\n"); /* only the clean-mode list, nothing for run modes */
  Matter.begin();

  check("only the set list was resent", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== ChangeToMode forwards: cluster 84 (RvcRunMode) / 85 (RvcCleanMode) ===== */

static void test_changetorunmode_allow_caches_requested_mode(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  bool called = false;
  uint8_t seen = 255;
  vac.onChangeRunMode([&](uint8_t m) {
    called = true;
    seen = m;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,84,0,3"); /* ChangeToMode, requested mode 3 */
  Hearth.poll();
  check("onChangeRunMode fired with the requested mode", called && seen == 3);
  check("getCurrentRunMode() cache updated on allow", vac.getCurrentRunMode() == 3);
  check("an allowing verdict answers AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_changetorunmode_deny_leaves_cache_untouched(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onChangeRunMode([](uint8_t) { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,84,0,3");
  Hearth.poll();
  check("getCurrentRunMode() cache untouched on deny (still 0)", vac.getCurrentRunMode() == 0);
  check("a denying verdict answers AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_changetocleanmode_allow_caches_requested_mode(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  bool called = false;
  uint8_t seen = 255;
  vac.onChangeCleanMode([&](uint8_t m) {
    called = true;
    seen = m;
    return true;
  });
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,1,85,0,1"); /* ChangeToMode on RvcCleanMode, requested mode 1 */
  Hearth.poll();
  check("onChangeCleanMode fired with the requested mode", called && seen == 1);
  check("getCurrentCleanMode() cache updated on allow", vac.getCurrentCleanMode() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_changetocleanmode_deny_leaves_cache_untouched(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onChangeCleanMode([](uint8_t) { return false; });
  s.expect("AT+MTCMDRESP=10,0", "OK\r\n");
  s.injectURC("+MTCMD:10,1,85,0,1");
  Hearth.poll();
  check("getCurrentCleanMode() cache untouched on deny (still 0)", vac.getCurrentCleanMode() == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_changetomode_no_callback_denies_and_does_not_cache(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  /* no onChangeRunMode() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,84,0,5");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("cache untouched", vac.getCurrentRunMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== Pause / Resume / GoHome forwards: cluster 97 (RvcOperationalState) ===== */

static void test_forwarded_pause_allow(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,97,0"); /* Pause */
  Hearth.poll();
  check("an allowing onPause() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_pause_deny(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onPause([]() { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,97,0");
  Hearth.poll();
  check("a denying onPause() answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_resume_allow(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onResume([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,97,3"); /* Resume */
  Hearth.poll();
  check("an allowing onResume() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_resume_deny(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onResume([]() { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,97,3");
  Hearth.poll();
  check("a denying onResume() answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_gohome_allow(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onGoHome([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,97,128"); /* GoHome, 0x80 */
  Hearth.poll();
  check("an allowing onGoHome() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_gohome_deny(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onGoHome([]() { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,97,128");
  Hearth.poll();
  check("a denying onGoHome() answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Brief's own explicit case: GoHome with no callback registered fails
 * closed, the house norm every forwarded-command class follows. */
static void test_forwarded_gohome_no_callback_denies(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  /* no onGoHome() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,97,128");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== unknown cluster / command: defer to the base class default ===== */

static void test_forwarded_command_wrong_cluster_denies(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0"); /* DoorLock's cluster, none of this class's three */
  Hearth.poll();
  check("an unrelated cluster id denies via the base class default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_unrecognised_opstate_id_denies(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  vac.onPause([]() { return true; });
  vac.onResume([]() { return true; });
  vac.onGoHome([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,97,1"); /* Stop: RvcOperationalState does not support it (S3.21) */
  Hearth.poll();
  check("an unrecognised RvcOperationalState command id denies via the base default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== CurrentMode caching correction: no ember-level signal exists ===== */

/*
 * Binding correction (this batch's firmware Task 3): CurrentMode on
 * RvcRunMode/RvcCleanMode is AttributeAccessInterface-served, so no
 * +MTATTR URC is ever raised for it in the first place. This class's
 * attributeChangeCB() is a documented no-op precisely because of that; this
 * test proves an injected +MTATTR (something the real firmware never sends
 * for these clusters, but a defensive test all the same) cannot move the
 * cache either way, so a future accidental re-wiring of attributeChangeCB()
 * would fail this test immediately.
 */
static void test_injected_attr_urc_does_not_move_mode_caches(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  s.injectURC("+MTATTR:1,84,1,7"); /* would-be RvcRunMode CurrentMode change */
  Hearth.poll();
  check("getCurrentRunMode() unaffected by an injected +MTATTR", vac.getCurrentRunMode() == 0);
  check("no echo written back to the wire", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== setOperationalState() / getOperationalState(): AT+MTOPSTATE ===== */

static void test_setoperationalstate_base_states_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  s.expect("AT+MTOPSTATE=1,1", "OK\r\n"); /* Running */
  check("setOperationalState(Running) sends the exact wire pin", vac.setOperationalState(MatterRoboticVacuum::kStateRunning));
  check("cache updated to Running", vac.getOperationalState() == MatterRoboticVacuum::kStateRunning);
  s.expect("AT+MTOPSTATE=1,2", "OK\r\n"); /* Paused */
  check("setOperationalState(Paused) sends the exact wire pin", vac.setOperationalState(MatterRoboticVacuum::kStatePaused));
  check("cache updated to Paused", vac.getOperationalState() == MatterRoboticVacuum::kStatePaused);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.21's own worked example: the three RVC-only states,
 * decimal on the wire (this class's hearthSendOperationalState formats
 * "%u", the same convention the base OperationalState trio uses). */
static void test_setoperationalstate_rvc_only_states_exact_wire_pin(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  s.expect("AT+MTOPSTATE=1,64", "OK\r\n"); /* 0x40 SeekingCharger */
  check("setOperationalState(SeekingCharger) sends 64 decimal", vac.setOperationalState(MatterRoboticVacuum::kStateSeekingCharger));
  s.expect("AT+MTOPSTATE=1,65", "OK\r\n"); /* 0x41 Charging */
  check("setOperationalState(Charging) sends 65 decimal", vac.setOperationalState(MatterRoboticVacuum::kStateCharging));
  s.expect("AT+MTOPSTATE=1,66", "OK\r\n"); /* 0x42 Docked */
  check("setOperationalState(Docked) sends 66 decimal", vac.setOperationalState(MatterRoboticVacuum::kStateDocked));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_noop_on_unchanged_value(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac); /* cache starts 0 (Stopped) */
  check("setOperationalState(Stopped), already the cache, is a no-op", vac.setOperationalState(MatterRoboticVacuum::kStateStopped));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_failed_write_leaves_cache(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  bringUp(s, vac);
  s.expect("AT+MTOPSTATE=1,3", "+MTERR:1\r\nERROR\r\n"); /* 3/Error is reserved, S3.21 */
  check("a rejected write returns false", !vac.setOperationalState(3));
  check("cache untouched (still Stopped)", vac.getOperationalState() == MatterRoboticVacuum::kStateStopped);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterRoboticVacuum vac;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", vac.begin());
  check("setOperationalState() before reconcile fails", !vac.setOperationalState(MatterRoboticVacuum::kStateRunning));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== attributeChangeCB() / hearthAttrTypeFor(): documented no-ops ===== */

static void test_attributechangecb_is_noop_returning_started(void) {
  MatterRoboticVacuum vac;
  check("attributeChangeCB() returns false (started) before begin()", vac.attributeChangeCB(1, 84, 1, nullptr) == false);
}

static void test_attrtype_delegates_to_base(void) {
  MatterRoboticVacuum vac;
  check("hearthAttrTypeFor() falls through to the base default", vac.hearthAttrTypeFor(84, 1) == ESP_MATTER_VAL_TYPE_INTEGER);
}

int main(void) {
  printf("\n===== MatterRoboticVacuum tests =====\n");
  test_begin_declares_0x0074_variant0();
  test_rebegin_after_reconcile_refused();
  test_setsupportedrunmodes_explicit_tags_exact_wire_pin();
  test_setsupportedrunmodes_tag_zero_rendering();
  test_setsupportedcleanmodes_exact_wire_pin();
  test_setsupportedrunmodes_comma_in_label();
  test_setsupportedrunmodes_rejects_zero_triples();
  test_setsupportedrunmodes_rejects_too_many_triples();
  test_setsupportedrunmodes_rejects_repeated_mode();
  test_setsupportedrunmodes_rejects_empty_label();
  test_setsupportedrunmodes_rejects_oversized_label();
  test_setsupportedrunmodes_rejects_quote_in_label();
  test_setsupportedrunmodes_rejects_nonprintable_in_label();
  test_setsupportedrunmodes_before_reconcile_fails_without_traffic();
  test_setsupportedrunmodes_failed_write_leaves_cache_unresent();
  test_setsupportedcleanmodes_rejects_repeated_mode();
  test_reconcile_with_nothing_set_sends_nothing();
  test_both_modelists_resent_on_next_reconcile();
  test_only_set_list_is_resent_on_reconcile();
  test_changetorunmode_allow_caches_requested_mode();
  test_changetorunmode_deny_leaves_cache_untouched();
  test_changetocleanmode_allow_caches_requested_mode();
  test_changetocleanmode_deny_leaves_cache_untouched();
  test_changetomode_no_callback_denies_and_does_not_cache();
  test_forwarded_pause_allow();
  test_forwarded_pause_deny();
  test_forwarded_resume_allow();
  test_forwarded_resume_deny();
  test_forwarded_gohome_allow();
  test_forwarded_gohome_deny();
  test_forwarded_gohome_no_callback_denies();
  test_forwarded_command_wrong_cluster_denies();
  test_forwarded_command_unrecognised_opstate_id_denies();
  test_injected_attr_urc_does_not_move_mode_caches();
  test_setoperationalstate_base_states_exact_wire_pin_and_cache();
  test_setoperationalstate_rvc_only_states_exact_wire_pin();
  test_setoperationalstate_noop_on_unchanged_value();
  test_setoperationalstate_failed_write_leaves_cache();
  test_setoperationalstate_before_reconcile_fails_without_traffic();
  test_attributechangecb_is_noop_returning_started();
  test_attrtype_delegates_to_base();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
