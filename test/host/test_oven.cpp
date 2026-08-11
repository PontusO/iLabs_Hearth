/*
 * test_oven.cpp - Task 9 (composed-appliance round): MatterOven with owned
 * MatterOvenCavity children.
 *
 * The second consumer of Task 7's parent-aware declaration machinery, and
 * the first TYPED child class: MatterOvenCavity subclasses
 * MatterTemperatureControlledCabinet, so the owner pattern pins here mirror
 * test_refrigerator.cpp (parent first, children carrying the parent's own
 * registry index; an owned child's begin() declares NOTHING), plus the
 * cavity's own conditional cluster surface the oven parent derives:
 * OvenMode (73/0x49, AT+MTMODES cluster-aware triples) and
 * OvenCavityOperationalState (72/0x48, AT+MTOPSTATE and adjudicated
 * Stop/Start forwards).
 *
 * The typed-child pins that matter most:
 *
 * - The oven parent is BARE (Descriptor + Identify, AT_MT_SPEC.md S3.9's
 *   0x007B note): it has no modes, no alarm, no reconcile push of its own.
 *   Every functional test here runs against the CAVITY's endpoint.
 * - Pause (72,0) and Resume (72,3) DO NOT EXIST on this cluster
 *   (disallowConform, OperationalState_Oven.xml revision 2; S3.17): the
 *   firmware never forwards them, the class has no onPause/onResume members
 *   at all, and a spurious injected forward must be denied without reaching
 *   any callback.
 * - setOperationalState() enforces plain {0,1,2} membership HOST-side
 *   (hearthSetError(1), no wire traffic), the task brief's explicit
 *   requirement for this typed child: its legal state set is closed and
 *   known at compile time, unlike the union the firmware's own handler
 *   checks.
 * - A spurious cluster-82 forward on the cavity is denied through
 *   MatterEndPoint's base default, NOT the cabinet's owned-cabinet
 *   adjudication the cavity inherits from: the cavity's cluster is OvenMode,
 *   and its dispatch must skip the fridge-cabinet path entirely.
 *
 * ChangeToMode adjudication follows the 0.6.0 rule (S3.20.1): OvenMode's
 * CurrentMode is Instance-served, no +MTATTR URC ever fires for it, so
 * getCurrentMode() updates on allow verdicts only. B196 means a same-mode
 * ChangeToMode short-circuits firmware-side; no test injects one.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterOven.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* Oven plus one NUMBER cavity, adopt path (live composition matches the
 * declaration). The cavity's own begin() is deliberately NOT called here;
 * tests that need its temperature push call it themselves before
 * Matter.begin(). */
static void bringUpOvenOneCavity(MockStream &s, MatterOven &oven, MatterOvenCavity **cav) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  *cav = &oven.addCavity(MatterOvenCavity::NUMBER);
  oven.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x007B\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  Matter.begin();
}

/* ===== begin(): declaration order and parent indexes ===== */

/* The brief's verbatim AT+MTEP capture: first-boot rebuild of an oven with
 * a NUMBER and a LEVELS cavity. Parent first, then the children, each
 * carrying variant AND the parent's registry index (the wire cannot carry a
 * parent without an explicit variant, Task 7's emission rule). */
static void test_begin_rebuild_emission_order(void) {
  MockStream s;
  MatterOven oven;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterOvenCavity &cavA = oven.addCavity(MatterOvenCavity::NUMBER);
  MatterOvenCavity &cavB = oven.addCavity(MatterOvenCavity::LEVELS);
  check("begin() declares oven and both cavities", oven.begin());

  s.expect("AT+MTEP?", "OK\r\n"); /* empty: first boot */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x007B", "OK\r\n");     /* parent first, unparented shape */
  s.expect("AT+MTEP=0x0071,0,0", "OK\r\n"); /* NUMBER cavity under index 0 */
  s.expect("AT+MTEP=0x0071,1,0", "OK\r\n"); /* LEVELS cavity under index 0 */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x007B\r\n+MTEP:1,2,0x0071,0,0\r\n+MTEP:2,3,0x0071,1,0\r\nOK\r\n");
  /* g_yieldAdvanceMs: if a regression makes the emission diverge from the
   * script, the mismatched command gets no reply and readLine() would spin
   * on a clock nothing advances. Same pattern as test_refrigerator. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("the full parented apply sequence is issued verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("oven adopts ID 1", oven.getEndPointId() == 1);
  check("NUMBER cavity adopts ID 2", cavA.getEndPointId() == 2);
  check("LEVELS cavity adopts ID 3", cavB.getEndPointId() == 3);
}

/* Registry shape without any reconcile: what begin() itself declared. */
static void test_begin_registry_shape(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterOven oven;
  oven.addCavity(MatterOvenCavity::NUMBER);
  oven.addCavity(MatterOvenCavity::LEVELS);
  check("begin() succeeds", oven.begin());
  check("three registry entries", MatterEndPoint::hearthDeclaredCount() == 3);
  check("entry 0 is the oven (0x007B)", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x007B);
  check("the oven itself is unparented", MatterEndPoint::hearthDeclaredParentAt(0) == MatterEndPoint::HEARTH_NO_PARENT);
  check("entry 1 is a cabinet (0x0071)", MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0071);
  check("NUMBER cavity declared variant 0", MatterEndPoint::hearthDeclaredVariantAt(1) == 0);
  check("NUMBER cavity parented to the oven's index", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("entry 2 is a cabinet (0x0071)", MatterEndPoint::hearthDeclaredTypeAt(2) == 0x0071);
  check("LEVELS cavity declared variant 1", MatterEndPoint::hearthDeclaredVariantAt(2) == 1);
  check("LEVELS cavity parented to the oven's index", MatterEndPoint::hearthDeclaredParentAt(2) == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("a second begin() after Matter.begin() is refused", !oven.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== addCavity(): capacity and pre-begin enforcement ===== */

static void test_addcavity_capacity(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterOven oven;
  for (uint8_t i = 0; i < MatterOven::kMaxCavities; i++) {
    oven.addCavity(MatterOvenCavity::NUMBER);
  }
  MatterOvenCavity &rejected = oven.addCavity(MatterOvenCavity::NUMBER);
  check("begin() declares the oven and exactly kMaxCavities cavities", oven.begin());
  check("registry holds 1 + kMaxCavities entries, not 5", MatterEndPoint::hearthDeclaredCount() == 1 + MatterOven::kMaxCavities);
  check("the over-capacity cavity's begin() is refused", !rejected.begin(180.0, 30.0, 300.0, 5.0));
  check("and it declared nothing", MatterEndPoint::hearthDeclaredCount() == 1 + MatterOven::kMaxCavities);
}

static void test_addcavity_after_begin_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterOven oven;
  check("begin() with no cavities succeeds", oven.begin());
  MatterOvenCavity &rejected = oven.addCavity(MatterOvenCavity::NUMBER);
  check("post-begin addCavity hands back an inert cavity: begin() refused", !rejected.begin(180.0, 30.0, 300.0, 5.0));
  check("registry still holds only the oven", MatterEndPoint::hearthDeclaredCount() == 1);
}

/* ===== owned only: no public standalone begin path ===== */

/* A sketch-constructed cavity never owned by an oven must refuse both
 * begin() overloads without declaring anything: unlike its cabinet base
 * class (standalone-legal by design), the cavity's conditional clusters
 * exist ONLY when the firmware derives them from an Oven parent, so a
 * standalone cavity endpoint would answer nothing this class promises. */
static void test_standalone_cavity_begin_refused(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterOvenCavity cav;
  check("standalone TemperatureNumber begin() is refused", !cav.begin(180.0, 30.0, 300.0, 5.0));
  uint8_t levels[] = { 0, 1, 2 };
  check("standalone TemperatureLevel begin() is refused too", !cav.begin(levels, 3, 1));
  check("nothing was declared", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no AT traffic at all", s.scriptDrained() && s.unexpected().empty());
}

/* ===== owned cavity begin(): declares nothing, the parent declared it ===== */

static void test_owned_cavity_begin_declares_nothing(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterOven oven;
  MatterOvenCavity &cav = oven.addCavity(MatterOvenCavity::NUMBER);
  check("oven.begin() declares", oven.begin());
  check("two registry entries after oven.begin()", MatterEndPoint::hearthDeclaredCount() == 2);
  check("owned cavity begin() succeeds", cav.begin(180.0, 30.0, 300.0, 5.0));
  check("and declared nothing: still two entries", MatterEndPoint::hearthDeclaredCount() == 2);
  /* THE trap this pin exists for: a hearthDeclare from the cavity's own
   * begin() would have updated its entry in place and wiped the parent. */
  check("the cavity's parent index survived its begin()", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("and its declared variant survived too", MatterEndPoint::hearthDeclaredVariantAt(1) == 0);
  check("begin() cached the setpoint", cav.getTemperatureSetpoint() == 180.0);
  check("begin() cached the min", cav.getMinTemperature() == 30.0);
  check("begin() cached the max", cav.getMaxTemperature() == 300.0);
  check("no AT traffic at all", s.scriptDrained() && s.unexpected().empty());
}

static void test_owned_cavity_begin_flavour_mismatch_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterOven oven;
  MatterOvenCavity &num = oven.addCavity(MatterOvenCavity::NUMBER);
  MatterOvenCavity &lev = oven.addCavity(MatterOvenCavity::LEVELS);
  check("oven.begin() declares", oven.begin());
  uint8_t levels[] = { 0, 1, 2 };
  check("TemperatureNumber begin() on a LEVELS cavity is refused", !lev.begin(180.0, 30.0, 300.0, 5.0));
  check("TemperatureLevel begin() on a NUMBER cavity is refused", !num.begin(levels, 3, 1));
  check("the matching flavours still work: NUMBER", num.begin(180.0, 30.0, 300.0, 5.0));
  check("the matching flavours still work: LEVELS", lev.begin(levels, 3, 1));
  check("an owned re-begin while started is refused", !num.begin(200.0, 30.0, 300.0, 5.0));
  check("nothing extra was ever declared", MatterEndPoint::hearthDeclaredCount() == 3);
}

/* The owned begin()'s cache feeds the same reconcile push the cabinet base
 * class always had: 180.0/30.0/300.0/5.0 lands as 18000/3000/30000/500
 * hundredths on the cavity's own endpoint, min/max/step/setpoint order. */
static void test_owned_cavity_reconcile_pushes_temperatures(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterOven oven;
  MatterOvenCavity &cav = oven.addCavity(MatterOvenCavity::NUMBER);
  oven.begin();
  check("owned cavity begin() before Matter.begin()", cav.begin(180.0, 30.0, 300.0, 5.0));
  s.expect("AT+MTEP?", "+MTEP:0,1,0x007B\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,1,3000,1", "+MTATTR:2,86,1,3000\r\nOK\r\n");     /* MinTemperature */
  s.expect("AT+MTATTR=2,86,2,30000,1", "+MTATTR:2,86,2,30000\r\nOK\r\n");   /* MaxTemperature */
  s.expect("AT+MTATTR=2,86,3,500,1", "+MTATTR:2,86,3,500\r\nOK\r\n");       /* Step */
  s.expect("AT+MTATTR=2,86,0,18000,1", "+MTATTR:2,86,0,18000\r\nOK\r\n");   /* TemperatureSetpoint */
  Matter.begin();
  check("the owned cavity's temperature push ran on its own endpoint", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("cavity adopted endpoint 2", cav.getEndPointId() == 2);
}

/* ===== cavity modes: OvenMode (0x49, 73 decimal) on the cavity ===== */

static void test_cavity_setsupportedmodes_exact_wire_pin(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin() (post-reconcile, cache only)", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Bake", "Roast" };
  s.expect("AT+MTMODES=2,73,0,0,\"Bake\",1,0,\"Roast\"", "OK\r\n");
  check("setSupportedModes sends cluster 73 on the CAVITY's endpoint (2)", cav->setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cavity_setsupportedmodes_explicit_tag_rendering(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0, 4 };
  uint16_t tags[] = { 0x0000, 0x4002 }; /* 0 = firmware substitutes kBake; 0x4002 kGrill passes through */
  const char *labels[] = { "Bake", "Grill" };
  s.expect("AT+MTMODES=2,73,0,0,\"Bake\",4,16386,\"Grill\"", "OK\r\n");
  check("explicit tags render decimal, tag 0 renders literally", cav->setSupportedModes(modes, tags, labels, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ----- grammar validation: every rejection category, S3.20.1 ----- */

static void test_cavity_modes_rejects_zero_triples(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  check("0 triples is refused host-side", !cav->setSupportedModes(nullptr, nullptr, nullptr, 0));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_rejects_too_many_triples(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
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
  check("9 triples exceeds the 8-triple cap and is refused", !cav->setSupportedModes(modes, tags, labels, 9));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_rejects_repeated_mode(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 2, 2 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "A", "B" };
  check("a repeated mode value is refused host-side", !cav->setSupportedModes(modes, tags, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_rejects_empty_label(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "" };
  check("an empty label is refused host-side", !cav->setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_rejects_oversized_label(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "123456789012345678901234567890123" }; /* 33 bytes, one over the cap */
  check("label length 33 exceeds the 32-byte cap and is refused", strlen(labels[0]) == 33 && !cav->setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_rejects_quote_in_label(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\"Label" };
  check("a label containing a double quote is refused", !cav->setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_rejects_nonprintable_in_label(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bad\x01Label" };
  check("a label containing a non-printable byte is refused", !cav->setSupportedModes(modes, tags, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterOven oven;
  MatterOvenCavity &cav = oven.addCavity(MatterOvenCavity::NUMBER);
  check("oven.begin() declares", oven.begin());
  check("owned cavity begin() (cache only)", cav.begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bake" };
  check("setSupportedModes() before reconcile fails", !cav.setSupportedModes(modes, tags, labels, 1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_cavity_modes_failed_write_leaves_cache_unresent(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Bake" };
  s.expect("AT+MTMODES=2,73,0,0,\"Bake\"", "ERROR\r\n");
  check("a rejected write returns false", !cav->setSupportedModes(modes, tags, labels, 1));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x007B\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,1,3000,1", "+MTATTR:2,86,1,3000\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,2,30000,1", "+MTATTR:2,86,2,30000\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,3,500,1", "+MTATTR:2,86,3,500\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,0,18000,1", "+MTATTR:2,86,0,18000\r\nOK\r\n");
  Matter.begin();
  check("no mode list was resent (there is nothing cached)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== ChangeToMode adjudication on (73,0): the 0.6.0 caching rule ===== */

static void test_changetomode_allow_caches_requested_mode(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  bool called = false;
  uint8_t seen = 255;
  cav->onChangeMode([&](uint8_t m) {
    called = true;
    seen = m;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,2,73,0,1"); /* ChangeToMode, requested mode 1 */
  Hearth.poll();
  check("onChangeMode fired with the requested mode", called && seen == 1);
  check("getCurrentMode() cache updated on allow", cav->getCurrentMode() == 1);
  check("an allowing verdict answers AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_changetomode_deny_leaves_cache_untouched(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  cav->onChangeMode([](uint8_t) { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,2,73,0,1");
  Hearth.poll();
  check("getCurrentMode() cache untouched on deny (still 0)", cav->getCurrentMode() == 0);
  check("a denying verdict answers AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_changetomode_no_callback_denies(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  /* no onChangeMode() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,2,73,0,2");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("cache untouched", cav->getCurrentMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* A spurious cluster-82 (fridge-cabinet mode) forward on the cavity must be
 * denied through MatterEndPoint's base default, and must NOT consult the
 * cavity's onChangeMode callback: the cavity's dispatch skips the inherited
 * fridge-cabinet adjudication entirely (its cluster is OvenMode, 73). */
static void test_spurious_cluster82_forward_denied(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  bool called = false;
  cav->onChangeMode([&](uint8_t) {
    called = true;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,2,82,0,1");
  Hearth.poll();
  check("a cluster-82 forward on the cavity is denied", s.scriptDrained());
  check("the cavity's onChangeMode was never consulted", !called);
  check("cache untouched", cav->getCurrentMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* The 0.6.0 rule's defensive half: CurrentMode is Instance-served, the
 * firmware never raises +MTATTR for it, and an injected one must not move
 * the cache either. */
static void test_injected_attr_urc_does_not_move_mode_cache(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  s.injectURC("+MTATTR:2,73,1,5"); /* would-be CurrentMode change */
  Hearth.poll();
  check("getCurrentMode() unaffected by an injected +MTATTR", cav->getCurrentMode() == 0);
  check("no echo written back to the wire", s.scriptDrained() && s.unexpected().empty());
}

/* ===== Stop/Start forwards on (72,1)/(72,2), adjudicated both ways ===== */

static void test_onstop_allow_and_deny(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  bool called = false;
  bool verdict = true;
  cav->onStop([&]() {
    called = true;
    return verdict;
  });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,2,72,1"); /* Stop, no payload */
  Hearth.poll();
  check("onStop fired", called);
  check("an allowing verdict answers AT+MTCMDRESP=7,1", s.scriptDrained());

  called = false;
  verdict = false;
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,2,72,1");
  Hearth.poll();
  check("onStop fired again", called);
  check("a denying verdict answers AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_onstart_allow_and_deny(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  bool called = false;
  bool verdict = true;
  cav->onStart([&]() {
    called = true;
    return verdict;
  });
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,2,72,2"); /* Start, no payload */
  Hearth.poll();
  check("onStart fired", called);
  check("an allowing verdict answers AT+MTCMDRESP=9,1", s.scriptDrained());

  called = false;
  verdict = false;
  s.expect("AT+MTCMDRESP=10,0", "OK\r\n");
  s.injectURC("+MTCMD:10,2,72,2");
  Hearth.poll();
  check("onStart fired again", called);
  check("a denying verdict answers AT+MTCMDRESP=10,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_opstate_no_callback_denies(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  /* neither onStop nor onStart registered */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,2,72,1");
  Hearth.poll();
  check("Stop with no callback denies (fail closed)", s.scriptDrained());
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,2,72,2");
  Hearth.poll();
  check("Start with no callback denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Pause (72,0) and Resume (72,3) do not exist on this cluster
 * (disallowConform): the firmware never forwards them, and a spurious
 * injected forward must be denied without reaching ANY callback. The class
 * has no onPause/onResume members at all, so this pin registers both legal
 * callbacks and asserts neither fires. */
static void test_pause_resume_never_reach_a_callback(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  bool stopCalled = false, startCalled = false, modeCalled = false;
  cav->onStop([&]() {
    stopCalled = true;
    return true;
  });
  cav->onStart([&]() {
    startCalled = true;
    return true;
  });
  cav->onChangeMode([&](uint8_t) {
    modeCalled = true;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,2,72,0"); /* would-be Pause */
  Hearth.poll();
  check("(72,0) is denied", s.scriptDrained());
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,2,72,3"); /* would-be Resume */
  Hearth.poll();
  check("(72,3) is denied", s.scriptDrained());
  check("no callback of any kind fired", !stopCalled && !startCalled && !modeCalled);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== setOperationalState: plain {0,1,2}, host-side bound ===== */

static void test_setoperationalstate_exact_wire_pin(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  check("initial cached state is Stopped (0)", cav->getOperationalState() == 0);
  s.expect("AT+MTOPSTATE=2,1", "OK\r\n");
  check("setOperationalState(1) emits AT+MTOPSTATE=<ep>,1", cav->setOperationalState(1));
  check("cache updated on success", cav->getOperationalState() == 1);
  s.expect("AT+MTOPSTATE=2,2", "OK\r\n");
  check("setOperationalState(2) emits AT+MTOPSTATE=<ep>,2", cav->setOperationalState(2));
  s.expect("AT+MTOPSTATE=2,0", "OK\r\n");
  check("setOperationalState(0) emits AT+MTOPSTATE=<ep>,0", cav->setOperationalState(0));
  check("cache back at 0", cav->getOperationalState() == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_rejects_out_of_range_host_side(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  check("state 3 (Error) is refused host-side", !cav->setOperationalState(3));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("state 0x40 (an RVC-only value) is refused too", !cav->setOperationalState(0x40));
  check("reports the grammar-violation code again", Hearth.lastError() == 1);
  check("cache untouched", cav->getOperationalState() == 0);
  check("no AT traffic was issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_setoperationalstate_same_state_skips_wire(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  check("setOperationalState(0) with cache already 0 succeeds", cav->setOperationalState(0));
  check("and issued no traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setoperationalstate_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterOven oven;
  MatterOvenCavity &cav = oven.addCavity(MatterOvenCavity::NUMBER);
  check("oven.begin() declares", oven.begin());
  check("owned cavity begin() (cache only)", cav.begin(180.0, 30.0, 300.0, 5.0));
  check("setOperationalState(1) before reconcile fails", !cav.setOperationalState(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("cache untouched", cav.getOperationalState() == 0);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

static void test_setoperationalstate_failed_write_leaves_cache(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));
  s.expect("AT+MTOPSTATE=2,1", "+MTERR:3\r\nERROR\r\n");
  check("a firmware-rejected write returns false", !cav->setOperationalState(1));
  check("and the +MTERR code is readable", Hearth.lastError() == 3);
  check("cache untouched on a failed write", cav->getOperationalState() == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== reconcile: the cavity resends its mode list, the oven nothing ===== */

static void test_reconcile_resends_cavity_modes(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  check("owned cavity begin()", cav->begin(180.0, 30.0, 300.0, 5.0));

  uint8_t modes[] = { 0, 1 };
  uint16_t tags[] = { 0, 0 };
  const char *labels[] = { "Bake", "Roast" };
  s.expect("AT+MTMODES=2,73,0,0,\"Bake\",1,0,\"Roast\"", "OK\r\n");
  check("cavity setSupportedModes succeeds", cav->setSupportedModes(modes, tags, labels, 2));

  /* A second reconcile: the oven's endpoint is bare and sends NOTHING; the
   * cavity's hook pushes its four cached temperature values and resends its
   * OvenMode list. */
  s.expect("AT+MTEP?", "+MTEP:0,1,0x007B\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,1,3000,1", "+MTATTR:2,86,1,3000\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,2,30000,1", "+MTATTR:2,86,2,30000\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,3,500,1", "+MTATTR:2,86,3,500\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,0,18000,1", "+MTATTR:2,86,0,18000\r\nOK\r\n");
  s.expect("AT+MTMODES=2,73,0,0,\"Bake\",1,0,\"Roast\"", "OK\r\n");
  Matter.begin();

  check("the cavity's list was resent verbatim, the oven sent nothing", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_reconcile_with_nothing_set_sends_no_modes(void) {
  MockStream s;
  MatterOven oven;
  MatterOvenCavity *cav = nullptr;
  bringUpOvenOneCavity(s, oven, &cav);
  /* the cavity's begin() was never called: no temperature push either */
  s.expect("AT+MTEP?", "+MTEP:0,1,0x007B\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  Matter.begin();
  check("no AT+MTMODES or AT+MTATTR traffic with nothing set", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterOven / MatterOvenCavity tests =====\n");
  test_begin_rebuild_emission_order();
  test_begin_registry_shape();
  test_rebegin_after_reconcile_refused();
  test_addcavity_capacity();
  test_addcavity_after_begin_refused();
  test_standalone_cavity_begin_refused();
  test_owned_cavity_begin_declares_nothing();
  test_owned_cavity_begin_flavour_mismatch_refused();
  test_owned_cavity_reconcile_pushes_temperatures();
  test_cavity_setsupportedmodes_exact_wire_pin();
  test_cavity_setsupportedmodes_explicit_tag_rendering();
  test_cavity_modes_rejects_zero_triples();
  test_cavity_modes_rejects_too_many_triples();
  test_cavity_modes_rejects_repeated_mode();
  test_cavity_modes_rejects_empty_label();
  test_cavity_modes_rejects_oversized_label();
  test_cavity_modes_rejects_quote_in_label();
  test_cavity_modes_rejects_nonprintable_in_label();
  test_cavity_modes_before_reconcile_fails_without_traffic();
  test_cavity_modes_failed_write_leaves_cache_unresent();
  test_changetomode_allow_caches_requested_mode();
  test_changetomode_deny_leaves_cache_untouched();
  test_changetomode_no_callback_denies();
  test_spurious_cluster82_forward_denied();
  test_injected_attr_urc_does_not_move_mode_cache();
  test_onstop_allow_and_deny();
  test_onstart_allow_and_deny();
  test_opstate_no_callback_denies();
  test_pause_resume_never_reach_a_callback();
  test_setoperationalstate_exact_wire_pin();
  test_setoperationalstate_rejects_out_of_range_host_side();
  test_setoperationalstate_same_state_skips_wire();
  test_setoperationalstate_before_reconcile_fails_without_traffic();
  test_setoperationalstate_failed_write_leaves_cache();
  test_reconcile_resends_cavity_modes();
  test_reconcile_with_nothing_set_sends_no_modes();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
