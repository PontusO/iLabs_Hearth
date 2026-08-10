/*
 * test_refrigerator.cpp - Task 8 (composed-appliance round):
 * MatterRefrigerator with owned Temperature Controlled Cabinets.
 *
 * The first class to CONSUME Task 7's parent-aware declaration machinery
 * (4-arg hearthDeclare, hearthDeclaredParentAt, the parented reconcile):
 * test_composition_parent.cpp proved that machinery against a generic
 * TestEndPoint; this file proves the fridge actually drives it. The
 * declaration-order pin (parent first, then children carrying the parent's
 * own registry index) is the verbatim AT+MTEP capture the brief demands.
 *
 * The owned-cabinet pins matter most: an owned cabinet's begin() must
 * declare NOTHING (the parent declared it, with the parent index; a
 * hearthDeclare from the cabinet's own begin() would update the registry
 * entry in place and silently wipe that parent index back to
 * HEARTH_NO_PARENT, exactly the in-place-update semantics
 * test_composition_parent's registry-roundtrip test pins).
 *
 * ChangeToMode adjudication follows the 0.6.0 rule (MatterRoboticVacuum's
 * "CurrentMode caching" section): ModeBase's CurrentMode is
 * AttributeAccessInterface-served, no +MTATTR URC ever fires for it, so
 * getCurrentMode() updates on allow verdicts only. B196 additionally means
 * a same-mode ChangeToMode short-circuits firmware-side and never reaches
 * this host at all, so no test here injects one: there is no wire line to
 * inject.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterRefrigerator.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* Fridge alone, adopt path (live composition matches the declaration). */
static void bringUpFridgeOnly(MockStream &s, MatterRefrigerator &fridge) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  fridge.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\nOK\r\n");
  Matter.begin();
}

/* Fridge plus one NUMBER cabinet, adopt path. The cabinet's own begin() is
 * deliberately NOT called here; tests that need its temperature push call
 * it themselves before Matter.begin(). */
static void bringUpFridgeOneCabinet(MockStream &s, MatterRefrigerator &fridge, MatterTemperatureControlledCabinet **cab) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  *cab = &fridge.addCabinet(MatterRefrigerator::NUMBER);
  fridge.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  Matter.begin();
}

/* ===== begin(): declaration order and parent indexes ===== */

/* The brief's verbatim AT+MTEP capture: first-boot rebuild of a fridge with
 * a NUMBER and a LEVELS cabinet. Parent first, then the children, each
 * carrying variant AND the parent's registry index (the wire cannot carry a
 * parent without an explicit variant, Task 7's emission rule). */
static void test_begin_rebuild_emission_order(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterTemperatureControlledCabinet &cabA = fridge.addCabinet(MatterRefrigerator::NUMBER);
  MatterTemperatureControlledCabinet &cabB = fridge.addCabinet(MatterRefrigerator::LEVELS);
  check("begin() declares fridge and both cabinets", fridge.begin());

  s.expect("AT+MTEP?", "OK\r\n"); /* empty: first boot */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0070", "OK\r\n");     /* parent first, unparented shape */
  s.expect("AT+MTEP=0x0071,0,0", "OK\r\n"); /* NUMBER cabinet under index 0 */
  s.expect("AT+MTEP=0x0071,1,0", "OK\r\n"); /* LEVELS cabinet under index 0 */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\n+MTEP:2,3,0x0071,1,0\r\nOK\r\n");
  /* g_yieldAdvanceMs: if a regression makes the emission diverge from the
   * script, the mismatched command gets no reply and readLine() would spin
   * on a clock nothing advances. Same pattern as test_composition_parent. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("the full parented apply sequence is issued verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("fridge adopts ID 1", fridge.getEndPointId() == 1);
  check("NUMBER cabinet adopts ID 2", cabA.getEndPointId() == 2);
  check("LEVELS cabinet adopts ID 3", cabB.getEndPointId() == 3);
}

/* Registry shape without any reconcile: what begin() itself declared. */
static void test_begin_registry_shape(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterRefrigerator fridge;
  fridge.addCabinet(MatterRefrigerator::NUMBER);
  fridge.addCabinet(MatterRefrigerator::LEVELS);
  check("begin() succeeds", fridge.begin());
  check("three registry entries", MatterEndPoint::hearthDeclaredCount() == 3);
  check("entry 0 is the fridge (0x0070)", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0070);
  check("the fridge itself is unparented", MatterEndPoint::hearthDeclaredParentAt(0) == MatterEndPoint::HEARTH_NO_PARENT);
  check("entry 1 is a cabinet (0x0071)", MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0071);
  check("NUMBER cabinet declared variant 0", MatterEndPoint::hearthDeclaredVariantAt(1) == 0);
  check("NUMBER cabinet parented to the fridge's index", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("entry 2 is a cabinet (0x0071)", MatterEndPoint::hearthDeclaredTypeAt(2) == 0x0071);
  check("LEVELS cabinet declared variant 1", MatterEndPoint::hearthDeclaredVariantAt(2) == 1);
  check("LEVELS cabinet parented to the fridge's index", MatterEndPoint::hearthDeclaredParentAt(2) == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  check("a second begin() after Matter.begin() is refused", !fridge.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== addCabinet(): capacity and pre-begin enforcement ===== */

static void test_addcabinet_capacity(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterRefrigerator fridge;
  for (uint8_t i = 0; i < MatterRefrigerator::kMaxCabinets; i++) {
    fridge.addCabinet(MatterRefrigerator::NUMBER);
  }
  MatterTemperatureControlledCabinet &rejected = fridge.addCabinet(MatterRefrigerator::NUMBER);
  check("begin() declares the fridge and exactly kMaxCabinets cabinets", fridge.begin());
  check("registry holds 1 + kMaxCabinets entries, not 6", MatterEndPoint::hearthDeclaredCount() == 1 + MatterRefrigerator::kMaxCabinets);
  check("the over-capacity cabinet's begin() is refused", !rejected.begin(4.0, 0.0, 10.0, 0.5));
  check("and it declared nothing", MatterEndPoint::hearthDeclaredCount() == 1 + MatterRefrigerator::kMaxCabinets);
}

static void test_addcabinet_after_begin_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterRefrigerator fridge;
  check("begin() with no cabinets succeeds", fridge.begin());
  MatterTemperatureControlledCabinet &rejected = fridge.addCabinet(MatterRefrigerator::NUMBER);
  check("post-begin addCabinet hands back an inert cabinet: begin() refused", !rejected.begin(4.0, 0.0, 10.0, 0.5));
  check("registry still holds only the fridge", MatterEndPoint::hearthDeclaredCount() == 1);
}

/* ===== owned cabinet begin(): declares nothing, the parent declared it ===== */

static void test_owned_cabinet_begin_declares_nothing(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet &cab = fridge.addCabinet(MatterRefrigerator::NUMBER);
  check("fridge.begin() declares", fridge.begin());
  check("two registry entries after fridge.begin()", MatterEndPoint::hearthDeclaredCount() == 2);
  check("owned cabinet begin() succeeds", cab.begin(4.0, 0.0, 10.0, 0.5));
  check("and declared nothing: still two entries", MatterEndPoint::hearthDeclaredCount() == 2);
  /* THE trap this pin exists for: a hearthDeclare from the cabinet's own
   * begin() would have updated its entry in place and wiped the parent. */
  check("the cabinet's parent index survived its begin()", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("and its declared variant survived too", MatterEndPoint::hearthDeclaredVariantAt(1) == 0);
  check("begin() cached the setpoint", cab.getTemperatureSetpoint() == 4.0);
  check("begin() cached the min", cab.getMinTemperature() == 0.0);
  check("begin() cached the max", cab.getMaxTemperature() == 10.0);
  check("no AT traffic at all", s.scriptDrained() && s.unexpected().empty());
}

static void test_owned_cabinet_begin_flavour_mismatch_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet &num = fridge.addCabinet(MatterRefrigerator::NUMBER);
  MatterTemperatureControlledCabinet &lev = fridge.addCabinet(MatterRefrigerator::LEVELS);
  check("fridge.begin() declares", fridge.begin());
  uint8_t levels[] = { 0, 1, 2 };
  check("TemperatureNumber begin() on a LEVELS cabinet is refused", !lev.begin(4.0, 0.0, 10.0, 0.5));
  check("TemperatureLevel begin() on a NUMBER cabinet is refused", !num.begin(levels, 3, 1));
  check("the matching flavours still work: NUMBER", num.begin(4.0, 0.0, 10.0, 0.5));
  check("the matching flavours still work: LEVELS", lev.begin(levels, 3, 1));
  check("an owned re-begin while started is refused", !num.begin(5.0, 0.0, 10.0, 0.5));
  check("nothing extra was ever declared", MatterEndPoint::hearthDeclaredCount() == 3);
}

/* The owned begin()'s cache feeds the same reconcile push the standalone
 * cabinet always had: 4.0/0.0/10.0/0.5 lands as 400/0/1000/50 hundredths on
 * the cabinet's own endpoint, min/max/step/setpoint in that order. */
static void test_owned_cabinet_reconcile_pushes_temperatures(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet &cab = fridge.addCabinet(MatterRefrigerator::NUMBER);
  fridge.begin();
  check("owned cabinet begin() before Matter.begin()", cab.begin(4.0, 0.0, 10.0, 0.5));
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,1,0,1", "+MTATTR:2,86,1,0\r\nOK\r\n");       /* MinTemperature */
  s.expect("AT+MTATTR=2,86,2,1000,1", "+MTATTR:2,86,2,1000\r\nOK\r\n"); /* MaxTemperature */
  s.expect("AT+MTATTR=2,86,3,50,1", "+MTATTR:2,86,3,50\r\nOK\r\n");     /* Step */
  s.expect("AT+MTATTR=2,86,0,400,1", "+MTATTR:2,86,0,400\r\nOK\r\n");   /* TemperatureSetpoint */
  Matter.begin();
  check("the owned cabinet's temperature push ran on its own endpoint", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("cabinet adopted endpoint 2", cab.getEndPointId() == 2);
}

/* ===== parent modes: RefrigeratorAndTCCMode (0x52, 82 decimal) ===== */

static void test_parent_setsupportedmodes_exact_wire_pin(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Auto", "Energy Saver" };
  s.expect("AT+MTMODES=1,82,0,0,\"Auto\",1,0,\"Energy Saver\"", "OK\r\n");
  check("setSupportedModes sends cluster 82 on the fridge endpoint", fridge.setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_parent_setsupportedmodes_explicit_tag_rendering(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0, 4 };
  uint16_t tags[] = { 0x0000, 0x4001 }; /* 0 = firmware substitutes kAuto; 0x4001 passes through */
  const char *labels[] = { "Auto", "Rapid Cool" };
  s.expect("AT+MTMODES=1,82,0,0,\"Auto\",4,16385,\"Rapid Cool\"", "OK\r\n");
  check("explicit tags render decimal, tag 0 renders literally", fridge.setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ----- grammar validation: every rejection category, S3.20.1 ----- */

static void test_parent_modes_rejects_zero_triples(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  check("0 triples is refused host-side", !fridge.setSupportedModes(nullptr, nullptr, nullptr, 0));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_rejects_too_many_triples(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
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
  check("9 triples exceeds the 8-triple cap and is refused", !fridge.setSupportedModes(modes, tags, labels, 9));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_rejects_repeated_mode(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 2, 2 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "A", "B" };
  check("a repeated mode value is refused host-side", !fridge.setSupportedModes(modes, tags, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_rejects_empty_label(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "" };
  check("an empty label is refused host-side", !fridge.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_rejects_oversized_label(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "123456789012345678901234567890123" }; /* 33 bytes, one over the cap */
  check("label length 33 exceeds the 32-byte cap and is refused", strlen(labels[0]) == 33 && !fridge.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_rejects_quote_in_label(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\"Label" };
  check("a label containing a double quote is refused", !fridge.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_rejects_nonprintable_in_label(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\x01Label" };
  check("a label containing a non-printable byte is refused", !fridge.setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", fridge.begin());
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Auto" };
  check("setSupportedModes() before reconcile fails", !fridge.setSupportedModes(modes, tags, labels, 1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_parent_modes_failed_write_leaves_cache_unresent(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Auto" };
  s.expect("AT+MTMODES=1,82,0,0,\"Auto\"", "ERROR\r\n");
  check("a rejected write returns false", !fridge.setSupportedModes(modes, tags, labels, 1));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\nOK\r\n");
  Matter.begin();
  check("no mode list was resent (there is nothing cached)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== parent ChangeToMode adjudication: the 0.6.0 caching rule ===== */

static void test_parent_changetomode_allow_caches_requested_mode(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  bool called = false;
  uint8_t seen = 255;
  fridge.onChangeMode([&](uint8_t m) {
    called = true;
    seen = m;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,82,0,2"); /* ChangeToMode, requested mode 2 */
  Hearth.poll();
  check("onChangeMode fired with the requested mode", called && seen == 2);
  check("getCurrentMode() cache updated on allow", fridge.getCurrentMode() == 2);
  check("an allowing verdict answers AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_parent_changetomode_deny_leaves_cache_untouched(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  fridge.onChangeMode([](uint8_t) { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,82,0,2");
  Hearth.poll();
  check("getCurrentMode() cache untouched on deny (still 0)", fridge.getCurrentMode() == 0);
  check("a denying verdict answers AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_parent_changetomode_no_callback_denies(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  /* no onChangeMode() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,82,0,3");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("cache untouched", fridge.getCurrentMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* The 0.6.0 rule's defensive half (same shape as MatterRoboticVacuum's own
 * test): CurrentMode is Instance-served, the firmware never raises +MTATTR
 * for it, and an injected one must not move the cache either. */
static void test_injected_attr_urc_does_not_move_mode_cache(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  s.injectURC("+MTATTR:1,82,1,5"); /* would-be CurrentMode change */
  Hearth.poll();
  check("getCurrentMode() unaffected by an injected +MTATTR", fridge.getCurrentMode() == 0);
  check("no echo written back to the wire", s.scriptDrained() && s.unexpected().empty());
}

/* ===== owned cabinet modes: cluster 82 on the CABINET's endpoint ===== */

static void test_owned_cabinet_setsupportedmodes_exact_wire_pin(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet *cab = nullptr;
  bringUpFridgeOneCabinet(s, fridge, &cab);
  check("owned cabinet begin() (post-reconcile, cache only)", cab->begin(4.0, 0.0, 10.0, 0.5));
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Auto", "Deep Freeze" };
  s.expect("AT+MTMODES=2,82,0,0,\"Auto\",1,0,\"Deep Freeze\"", "OK\r\n");
  check("cabinet setSupportedModes sends cluster 82 on ITS endpoint (2)", cab->setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_owned_cabinet_modes_rejects_repeated_mode(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet *cab = nullptr;
  bringUpFridgeOneCabinet(s, fridge, &cab);
  check("owned cabinet begin()", cab->begin(4.0, 0.0, 10.0, 0.5));
  uint8_t modes[] = { 1, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "A", "B" };
  check("a repeated mode value is refused host-side on the cabinet too", !cab->setSupportedModes(modes, tags, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

/* Valid only when owned by a refrigerator: a standalone cabinet has no
 * ModeBase cluster at all (S3.9: the conditional cluster set is derived
 * from the parent), so the API refuses without touching the wire. */
static void test_unowned_cabinet_modes_api_refused(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterTemperatureControlledCabinet cab;
  cab.begin(4.0, 0.0, 10.0, 0.5);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,1,0,1", "+MTATTR:1,86,1,0\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,2,1000,1", "+MTATTR:1,86,2,1000\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,3,50,1", "+MTATTR:1,86,3,50\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,0,400,1", "+MTATTR:1,86,0,400\r\nOK\r\n");
  Matter.begin();
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Auto" };
  check("setSupportedModes on an unowned cabinet is refused", !cab.setSupportedModes(modes, tags, labels, 1));
  check("getCurrentMode stays 0", cab.getCurrentMode() == 0);
  check("no modes traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_owned_cabinet_changetomode_allow_and_deny(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet *cab = nullptr;
  bringUpFridgeOneCabinet(s, fridge, &cab);
  check("owned cabinet begin()", cab->begin(4.0, 0.0, 10.0, 0.5));
  uint8_t seen = 255;
  bool verdict = true;
  cab->onChangeMode([&](uint8_t m) {
    seen = m;
    return verdict;
  });
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,2,82,0,1"); /* ChangeToMode on the CABINET's endpoint */
  Hearth.poll();
  check("cabinet onChangeMode fired with the requested mode", seen == 1);
  check("cabinet getCurrentMode() updated on allow", cab->getCurrentMode() == 1);
  check("allowing verdict answered", s.scriptDrained());

  verdict = false;
  s.expect("AT+MTCMDRESP=10,0", "OK\r\n");
  s.injectURC("+MTCMD:10,2,82,0,0");
  Hearth.poll();
  check("cabinet cache untouched on deny (still 1)", cab->getCurrentMode() == 1);
  check("denying verdict answered", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* An unowned cabinet must not adjudicate a (spurious) cluster-82 forward:
 * base class default denies. */
static void test_unowned_cabinet_changetomode_denies(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterTemperatureControlledCabinet cab;
  cab.begin(4.0, 0.0, 10.0, 0.5);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,1,0,1", "+MTATTR:1,86,1,0\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,2,1000,1", "+MTATTR:1,86,2,1000\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,3,50,1", "+MTATTR:1,86,3,50\r\nOK\r\n");
  s.expect("AT+MTATTR=1,86,0,400,1", "+MTATTR:1,86,0,400\r\nOK\r\n");
  Matter.begin();
  cab.onChangeMode([](uint8_t) { return true; }); /* registered, but not owned: never consulted */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,82,0,1");
  Hearth.poll();
  check("an unowned cabinet denies a cluster-82 forward via the base default", s.scriptDrained());
  check("cache untouched", cab.getCurrentMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== door alarm: AT+MTALARM on the fridge endpoint ===== */

static void test_setdooropenalarm_exact_wire_pin(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  s.expect("AT+MTALARM=1,0,1", "OK\r\n");
  check("setDoorOpenAlarm(true) emits AT+MTALARM=<ep>,0,1", fridge.setDoorOpenAlarm(true));
  s.expect("AT+MTALARM=1,0,0", "OK\r\n");
  check("setDoorOpenAlarm(false) emits AT+MTALARM=<ep>,0,0", fridge.setDoorOpenAlarm(false));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setalarmstate_generic_bit_and_firmware_reject(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge);
  /* bit 3 is in the wire's defensive 0-7 union bound but unsupported by the
   * Matter spec's own bitmap; the firmware checks it against the endpoint's
   * Supported bitmap and answers +MTERR:1 (S3.22). The host passes it
   * through rather than duplicating that check: the firmware never
   * transcribes bitmaps into this library. */
  s.expect("AT+MTALARM=1,3,1", "+MTERR:1\r\nERROR\r\n");
  check("an unsupported bit is sent and the firmware's reject is honoured", !fridge.setAlarmState(3, true));
  check("and the +MTERR code is readable", Hearth.lastError() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_alarm_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", fridge.begin());
  check("setDoorOpenAlarm() before reconcile fails", !fridge.setDoorOpenAlarm(true));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== reconcile: both mode lists resent, parent then cabinet ===== */

static void test_reconcile_resends_parent_and_cabinet_modes(void) {
  MockStream s;
  MatterRefrigerator fridge;
  MatterTemperatureControlledCabinet *cab = nullptr;
  bringUpFridgeOneCabinet(s, fridge, &cab);
  check("owned cabinet begin()", cab->begin(4.0, 0.0, 10.0, 0.5));

  uint8_t fModes[] = { 0, 1 };
  uint16_t fTags[] = { 0, 0 };
  const char *fLabels[] = { "Auto", "Energy Saver" };
  s.expect("AT+MTMODES=1,82,0,0,\"Auto\",1,0,\"Energy Saver\"", "OK\r\n");
  check("fridge setSupportedModes succeeds", fridge.setSupportedModes(fModes, fTags, fLabels, 2));

  uint8_t cModes[] = { 0 };
  uint16_t cTags[] = { 0 };
  const char *cLabels[] = { "Auto" };
  s.expect("AT+MTMODES=2,82,0,0,\"Auto\"", "OK\r\n");
  check("cabinet setSupportedModes succeeds", cab->setSupportedModes(cModes, cTags, cLabels, 1));

  /* A second reconcile: the fridge's hook resends its list, then the
   * cabinet's hook (registry order) pushes its four cached temperature
   * values and resends ITS list. */
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  s.expect("AT+MTMODES=1,82,0,0,\"Auto\",1,0,\"Energy Saver\"", "OK\r\n");
  s.expect("AT+MTATTR=2,86,1,0,1", "+MTATTR:2,86,1,0\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,2,1000,1", "+MTATTR:2,86,2,1000\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,3,50,1", "+MTATTR:2,86,3,50\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,0,400,1", "+MTATTR:2,86,0,400\r\nOK\r\n");
  s.expect("AT+MTMODES=2,82,0,0,\"Auto\"", "OK\r\n");
  Matter.begin();

  check("both lists were resent verbatim, parent first", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_reconcile_with_nothing_set_sends_no_modes(void) {
  MockStream s;
  MatterRefrigerator fridge;
  bringUpFridgeOnly(s, fridge); /* bringUp itself already exercised one reconcile */
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\nOK\r\n");
  Matter.begin();
  check("no AT+MTMODES traffic with nothing set", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterRefrigerator tests =====\n");
  test_begin_rebuild_emission_order();
  test_begin_registry_shape();
  test_rebegin_after_reconcile_refused();
  test_addcabinet_capacity();
  test_addcabinet_after_begin_refused();
  test_owned_cabinet_begin_declares_nothing();
  test_owned_cabinet_begin_flavour_mismatch_refused();
  test_owned_cabinet_reconcile_pushes_temperatures();
  test_parent_setsupportedmodes_exact_wire_pin();
  test_parent_setsupportedmodes_explicit_tag_rendering();
  test_parent_modes_rejects_zero_triples();
  test_parent_modes_rejects_too_many_triples();
  test_parent_modes_rejects_repeated_mode();
  test_parent_modes_rejects_empty_label();
  test_parent_modes_rejects_oversized_label();
  test_parent_modes_rejects_quote_in_label();
  test_parent_modes_rejects_nonprintable_in_label();
  test_parent_modes_before_reconcile_fails_without_traffic();
  test_parent_modes_failed_write_leaves_cache_unresent();
  test_parent_changetomode_allow_caches_requested_mode();
  test_parent_changetomode_deny_leaves_cache_untouched();
  test_parent_changetomode_no_callback_denies();
  test_injected_attr_urc_does_not_move_mode_cache();
  test_owned_cabinet_setsupportedmodes_exact_wire_pin();
  test_owned_cabinet_modes_rejects_repeated_mode();
  test_unowned_cabinet_modes_api_refused();
  test_owned_cabinet_changetomode_allow_and_deny();
  test_unowned_cabinet_changetomode_denies();
  test_setdooropenalarm_exact_wire_pin();
  test_setalarmstate_generic_bit_and_firmware_reject();
  test_alarm_before_reconcile_fails_without_traffic();
  test_reconcile_resends_parent_and_cabinet_modes();
  test_reconcile_with_nothing_set_sends_no_modes();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
