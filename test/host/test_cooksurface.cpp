/*
 * test_cooksurface.cpp - Task 10 (composed-appliance round): MatterCookSurface
 * owned by a composed MatterCooktop.
 *
 * The third consumer of Task 7's parent-aware declaration machinery and the
 * second TYPED owned child (MatterOvenCavity was the first): MatterCookSurface
 * subclasses MatterTemperatureControlledCabinet for the temperature machinery
 * and adds the OnOff OffOnly surface the wire contract gives it
 * (AT_MT_SPEC.md S3.9's 0x0077 note). The owner pattern pins mirror
 * test_oven.cpp (parent first, children carrying the parent's own registry
 * index; an owned child's begin() declares NOTHING; inert-reject on
 * capacity/post-begin), plus this class's own pins:
 *
 * - 0x0077 REQUIRES a parent on the wire (the first such device type), so
 *   the class is owned-only: a standalone surface refuses both begin()
 *   overloads without touching the registry.
 * - OnOff on the surface carries the OffOnly feature: a controller can only
 *   ever deliver OnOff=false (Off is the sole command in
 *   AcceptedCommandList), arriving as a normal +MTATTR URC on cluster 6.
 *   onOffChange() fires on those remote-delivered changes; the class does
 *   NOT artificially filter true (the cache mirrors whatever the wire
 *   reports).
 * - setOnOff(bool) is the host's own act, BOTH directions (S3.9: turning a
 *   surface on is always the host's AT+MTATTR write), so unlike the parent
 *   cooktop class there IS a wire path to OnOff=1 here, and it is exercised.
 * - The inherited fridge-cabinet modes API is structurally dead on a
 *   surface: no mode cluster exists on the endpoint, so setSupportedModes()
 *   refuses without wire traffic and a spurious cluster-82 +MTCMD forward is
 *   denied through MatterEndPoint's fail-closed base default.
 *
 * The zero-surface backward-compatibility test re-pins test_cooktop.cpp's
 * own captures: a MatterCooktop with no addSurface() call must produce the
 * exact declaration, adopt sequence and OnOff behaviour the 0.6.0 class did.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterCooktop.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* Cooktop plus one NUMBER surface, adopt path (live composition matches the
 * declaration). The surface's own begin() is deliberately NOT called here;
 * tests that need its temperature push call it themselves before
 * Matter.begin(). */
static void bringUpCooktopOneSurface(MockStream &s, MatterCooktop &cooktop, MatterCookSurface **surf) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  *surf = &cooktop.addSurface(MatterCookSurface::NUMBER);
  cooktop.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0078\r\n+MTEP:1,2,0x0077,0,0\r\nOK\r\n");
  Matter.begin();
}

/* ===== begin(): declaration order and parent indexes ===== */

/* First-boot rebuild of a cooktop with a NUMBER and a LEVELS surface. Parent
 * first, then the children, each carrying variant AND the parent's registry
 * index (the wire cannot carry a parent without an explicit variant, Task
 * 7's emission rule). */
static void test_begin_rebuild_emission_order(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterCookSurface &surfA = cooktop.addSurface(MatterCookSurface::NUMBER);
  MatterCookSurface &surfB = cooktop.addSurface(MatterCookSurface::LEVELS);
  check("begin() declares cooktop and both surfaces", cooktop.begin());

  s.expect("AT+MTEP?", "OK\r\n"); /* empty: first boot */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0078", "OK\r\n");     /* parent first, unparented shape */
  s.expect("AT+MTEP=0x0077,0,0", "OK\r\n"); /* NUMBER surface under index 0 */
  s.expect("AT+MTEP=0x0077,1,0", "OK\r\n"); /* LEVELS surface under index 0 */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0078\r\n+MTEP:1,2,0x0077,0,0\r\n+MTEP:2,3,0x0077,1,0\r\nOK\r\n");
  /* g_yieldAdvanceMs: if a regression makes the emission diverge from the
   * script, the mismatched command gets no reply and readLine() would spin
   * on a clock nothing advances. Same pattern as test_oven. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("the full parented apply sequence is issued verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("cooktop adopts ID 1", cooktop.getEndPointId() == 1);
  check("NUMBER surface adopts ID 2", surfA.getEndPointId() == 2);
  check("LEVELS surface adopts ID 3", surfB.getEndPointId() == 3);
}

/* Registry shape without any reconcile: what begin() itself declared. */
static void test_begin_registry_shape(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterCooktop cooktop;
  cooktop.addSurface(MatterCookSurface::NUMBER);
  cooktop.addSurface(MatterCookSurface::LEVELS);
  check("begin() succeeds", cooktop.begin());
  check("three registry entries", MatterEndPoint::hearthDeclaredCount() == 3);
  check("entry 0 is the cooktop (0x0078)", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0078);
  check("the cooktop itself is unparented", MatterEndPoint::hearthDeclaredParentAt(0) == MatterEndPoint::HEARTH_NO_PARENT);
  check("entry 1 is a cook surface (0x0077)", MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0077);
  check("NUMBER surface declared variant 0", MatterEndPoint::hearthDeclaredVariantAt(1) == 0);
  check("NUMBER surface parented to the cooktop's index", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("entry 2 is a cook surface (0x0077)", MatterEndPoint::hearthDeclaredTypeAt(2) == 0x0077);
  check("LEVELS surface declared variant 1", MatterEndPoint::hearthDeclaredVariantAt(2) == 1);
  check("LEVELS surface parented to the cooktop's index", MatterEndPoint::hearthDeclaredParentAt(2) == 0);
}

/* ===== zero surfaces: byte-identical to the 0.6.0 cooktop ===== */

/* test_cooktop.cpp's own captures, re-pinned here against the composed
 * class: a cooktop with no addSurface() call declares exactly one bare
 * 0x0078 entry (no variant, no parent on the wire), adopts over the same
 * one-line reply, and keeps the OffOnly behaviour byte for byte. */
static void test_zero_surface_cooktop_unchanged(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() with no surfaces succeeds", cooktop.begin());
  check("one registry entry only", MatterEndPoint::hearthDeclaredCount() == 1);
  check("declared as cooktop", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0078);
  check("variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("unparented", MatterEndPoint::hearthDeclaredParentAt(0) == MatterEndPoint::HEARTH_NO_PARENT);

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0078\r\nOK\r\n");
  Matter.begin();
  check("adopts endpoint 1 with no recompose", cooktop.getEndPointId() == 1);
  check("no further commands issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  /* The OffOnly behaviour capture: a URC turns the cache on, off() writes 0. */
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("URC turned the cache on", cooktop.getOnOff() == true);
  s.expect("AT+MTATTR=1,6,0,0,1", "+MTATTR:1,6,0,0\r\nOK\r\n");
  check("off() writes OnOff=0 exactly as 0.6.0 did", cooktop.off());
  check("cache false again", cooktop.getOnOff() == false);
  check("nothing left over", s.scriptDrained() && s.unexpected().empty());
}

/* A zero-surface first-boot rebuild emits the bare unparented line, nothing
 * else: no variant field, no parent field. */
static void test_zero_surface_rebuild_emission_unchanged(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() succeeds", cooktop.begin());
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0078", "OK\r\n"); /* bare, exactly the 0.6.0 emission */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0078\r\nOK\r\n");
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;
  check("the 0.6.0 apply sequence is issued verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterCookSurface *surf = nullptr;
  bringUpCooktopOneSurface(s, cooktop, &surf);
  check("a second begin() after Matter.begin() is refused", !cooktop.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== addSurface(): capacity and pre-begin enforcement ===== */

static void test_addsurface_capacity(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterCooktop cooktop;
  for (uint8_t i = 0; i < MatterCooktop::kMaxSurfaces; i++) {
    cooktop.addSurface(MatterCookSurface::NUMBER);
  }
  MatterCookSurface &rejected = cooktop.addSurface(MatterCookSurface::NUMBER);
  check("begin() declares the cooktop and exactly kMaxSurfaces surfaces", cooktop.begin());
  check("registry holds 1 + kMaxSurfaces entries", MatterEndPoint::hearthDeclaredCount() == 1 + MatterCooktop::kMaxSurfaces);
  check("the over-capacity surface's begin() is refused", !rejected.begin(90.0, 30.0, 250.0, 5.0));
  check("and it declared nothing", MatterEndPoint::hearthDeclaredCount() == 1 + MatterCooktop::kMaxSurfaces);
}

static void test_addsurface_after_begin_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterCooktop cooktop;
  check("begin() with no surfaces succeeds", cooktop.begin());
  MatterCookSurface &rejected = cooktop.addSurface(MatterCookSurface::NUMBER);
  check("post-begin addSurface hands back an inert surface: begin() refused", !rejected.begin(90.0, 30.0, 250.0, 5.0));
  check("registry still holds only the cooktop", MatterEndPoint::hearthDeclaredCount() == 1);
}

/* ===== owned only: no public standalone begin path ===== */

/* 0x0077 REQUIRES a parent on the wire (AT_MT_SPEC.md S3.9: the unparented
 * form answers +MTERR:1), so unlike its cabinet base class the surface has
 * no standalone begin path at all: both overloads refuse without declaring
 * anything on a surface no cooktop owns. */
static void test_standalone_surface_begin_refused(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterCookSurface surf;
  check("standalone TemperatureNumber begin() is refused", !surf.begin(90.0, 30.0, 250.0, 5.0));
  uint8_t levels[] = { 0, 1, 2 };
  check("standalone TemperatureLevel begin() is refused too", !surf.begin(levels, 3, 1));
  check("nothing was declared", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no AT traffic at all", s.scriptDrained() && s.unexpected().empty());
}

/* ===== owned surface begin(): declares nothing, the parent declared it ===== */

static void test_owned_surface_begin_declares_nothing(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterCooktop cooktop;
  MatterCookSurface &surf = cooktop.addSurface(MatterCookSurface::NUMBER);
  check("cooktop.begin() declares", cooktop.begin());
  check("two registry entries after cooktop.begin()", MatterEndPoint::hearthDeclaredCount() == 2);
  check("owned surface begin() succeeds", surf.begin(90.0, 30.0, 250.0, 5.0));
  check("and declared nothing: still two entries", MatterEndPoint::hearthDeclaredCount() == 2);
  /* THE trap this pin exists for: a hearthDeclare from the surface's own
   * begin() would have updated its entry in place and wiped the parent. */
  check("the surface's parent index survived its begin()", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("and its declared variant survived too", MatterEndPoint::hearthDeclaredVariantAt(1) == 0);
  check("begin() cached the setpoint", surf.getTemperatureSetpoint() == 90.0);
  check("begin() cached the min", surf.getMinTemperature() == 30.0);
  check("begin() cached the max", surf.getMaxTemperature() == 250.0);
  check("the OnOff cache boots false", surf.getOnOff() == false);
  check("no AT traffic at all", s.scriptDrained() && s.unexpected().empty());
}

static void test_owned_surface_begin_flavour_mismatch_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterCooktop cooktop;
  MatterCookSurface &num = cooktop.addSurface(MatterCookSurface::NUMBER);
  MatterCookSurface &lev = cooktop.addSurface(MatterCookSurface::LEVELS);
  check("cooktop.begin() declares", cooktop.begin());
  uint8_t levels[] = { 0, 1, 2 };
  check("TemperatureNumber begin() on a LEVELS surface is refused", !lev.begin(90.0, 30.0, 250.0, 5.0));
  check("TemperatureLevel begin() on a NUMBER surface is refused", !num.begin(levels, 3, 1));
  check("the matching flavours still work: NUMBER", num.begin(90.0, 30.0, 250.0, 5.0));
  check("the matching flavours still work: LEVELS", lev.begin(levels, 3, 1));
  check("an owned re-begin while started is refused", !num.begin(100.0, 30.0, 250.0, 5.0));
  check("nothing extra was ever declared", MatterEndPoint::hearthDeclaredCount() == 3);
}

/* The owned begin()'s cache feeds the same reconcile push the cabinet base
 * class always had: 90.0/30.0/250.0/5.0 lands as 9000/3000/25000/500
 * hundredths on the surface's own endpoint, min/max/step/setpoint order. */
static void test_owned_surface_reconcile_pushes_temperatures(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  MatterCooktop cooktop;
  MatterCookSurface &surf = cooktop.addSurface(MatterCookSurface::NUMBER);
  cooktop.begin();
  check("owned surface begin() before Matter.begin()", surf.begin(90.0, 30.0, 250.0, 5.0));
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0078\r\n+MTEP:1,2,0x0077,0,0\r\nOK\r\n");
  s.expect("AT+MTATTR=2,86,1,3000,1", "+MTATTR:2,86,1,3000\r\nOK\r\n");     /* MinTemperature */
  s.expect("AT+MTATTR=2,86,2,25000,1", "+MTATTR:2,86,2,25000\r\nOK\r\n");   /* MaxTemperature */
  s.expect("AT+MTATTR=2,86,3,500,1", "+MTATTR:2,86,3,500\r\nOK\r\n");       /* Step */
  s.expect("AT+MTATTR=2,86,0,9000,1", "+MTATTR:2,86,0,9000\r\nOK\r\n");     /* TemperatureSetpoint */
  Matter.begin();
  check("the owned surface's temperature push ran on its own endpoint", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("surface adopted endpoint 2", surf.getEndPointId() == 2);
}

/* ===== OnOff: the OffOnly surface ===== */

/* The one thing a controller can deliver: OnOff=false, as a normal +MTATTR
 * URC on cluster 6 (the firmware handles the Off command and the ember
 * attribute change is reported). onOffChange() fires, the cache follows, and
 * nothing is echoed back to the fabric. */
static void test_remote_off_reaches_callback(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterCookSurface *surf = nullptr;
  bringUpCooktopOneSurface(s, cooktop, &surf);
  check("owned surface begin()", surf->begin(90.0, 30.0, 250.0, 5.0));

  /* Host turns the burner on first: the only way it CAN turn on. */
  s.expect("AT+MTATTR=2,6,0,1,1", "+MTATTR:2,6,0,1\r\nOK\r\n");
  check("setOnOff(true) writes OnOff=1 on the surface endpoint", surf->setOnOff(true));
  check("cache reads on", surf->getOnOff() == true);

  int seen = 0;
  bool last = true;
  surf->onOffChange([&](bool v) {
    seen++;
    last = v;
  });

  /* The remote Off: a plain URC, no command adjudication involved. */
  s.injectURC("+MTATTR:2,6,0,0");
  Hearth.poll();
  check("onOffChange fired on the remote-delivered change", seen == 1);
  check("and delivered false, the only value OffOnly can carry", last == false);
  check("cache followed the URC", surf->getOnOff() == false);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* setOnOff() is local and works BOTH directions; skip-if-equal is sound
 * because cache and device both boot Off. A failed write leaves the cache
 * untouched. */
static void test_setonoff_both_directions(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterCookSurface *surf = nullptr;
  bringUpCooktopOneSurface(s, cooktop, &surf);
  check("owned surface begin()", surf->begin(90.0, 30.0, 250.0, 5.0));

  check("setOnOff(false) on an already-off cache is a no-op", surf->setOnOff(false));
  check("and issued no wire traffic", s.scriptDrained());

  s.expect("AT+MTATTR=2,6,0,1,1", "+MTATTR:2,6,0,1\r\nOK\r\n");
  check("setOnOff(true) performs the write", surf->setOnOff(true));
  check("cache on", surf->getOnOff() == true);

  check("setOnOff(true) again is a no-op", surf->setOnOff(true));
  check("still nothing further on the wire", s.scriptDrained());

  s.expect("AT+MTATTR=2,6,0,0,1", "+MTATTR:2,6,0,0\r\nOK\r\n");
  check("setOnOff(false) performs the write back down", surf->setOnOff(false));
  check("cache off", surf->getOnOff() == false);

  s.expect("AT+MTATTR=2,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !surf->setOnOff(true));
  check("and does not update the cache", surf->getOnOff() == false);
  check("nothing left over", s.scriptDrained() && s.unexpected().empty());
}

static void test_unstarted_surface_setonoff_refused(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterCookSurface *surf = nullptr;
  bringUpCooktopOneSurface(s, cooktop, &surf);
  /* surface begin() deliberately NOT called */
  check("setOnOff() on an unstarted surface is refused", !surf->setOnOff(true));
  check("no wire traffic", s.scriptDrained() && s.unexpected().empty());
}

/* The URC dispatcher asks the endpoint for the attribute's type; OnOff must
 * come back boolean (val.b), and the inherited temperature attributes keep
 * the cabinet's own int16 answer. */
static void test_attr_types(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterCooktop cooktop;
  MatterCookSurface &surf = cooktop.addSurface(MatterCookSurface::NUMBER);
  check("OnOff is typed boolean", surf.hearthAttrTypeFor(6, 0) == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("TemperatureSetpoint keeps the cabinet's int16", surf.hearthAttrTypeFor(86, 0) == ESP_MATTER_VAL_TYPE_INT16);
}

/* ===== inherited temperature machinery on the surface endpoint ===== */

static void test_temperature_roundtrip(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterCookSurface *surf = nullptr;
  bringUpCooktopOneSurface(s, cooktop, &surf);
  check("owned surface begin()", surf->begin(90.0, 30.0, 250.0, 5.0));

  s.expect("AT+MTATTR=2,86,0,12000,1", "+MTATTR:2,86,0,12000\r\nOK\r\n");
  check("setTemperatureSetpoint(120.0) writes on the surface endpoint", surf->setTemperatureSetpoint(120.0));
  check("getter reads it back", surf->getTemperatureSetpoint() == 120.0);

  /* A controller-driven setpoint change arrives as a URC and lands in the
   * inherited cabinet cache, no echo. */
  s.injectURC("+MTATTR:2,86,0,15000");
  Hearth.poll();
  check("a remote setpoint URC updates the inherited cache", surf->getTemperatureSetpoint() == 150.0);
  check("no echo written back", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== no mode cluster on a surface ===== */

/* The inherited fridge-cabinet modes API is structurally dead here: the
 * endpoint carries no mode cluster of any kind, so the hidden
 * setSupportedModes() refuses without wire traffic even though the surface
 * IS owned, and a spurious cluster-82 forward is denied through the base
 * default without consulting the (never-consulted) inherited callback. */
static void test_modes_api_dead_on_a_surface(void) {
  MockStream s;
  MatterCooktop cooktop;
  MatterCookSurface *surf = nullptr;
  bringUpCooktopOneSurface(s, cooktop, &surf);
  check("owned surface begin()", surf->begin(90.0, 30.0, 250.0, 5.0));

  uint8_t modes[] = { 0 };
  uint16_t tags[] = { 0 };
  const char *labels[] = { "Boost" };
  check("setSupportedModes() refuses on a surface", !surf->setSupportedModes(modes, tags, labels, 1));
  check("and issued no wire traffic", s.scriptDrained());

  bool consulted = false;
  surf->onChangeMode([&](uint8_t) {
    consulted = true;
    return true;
  });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,2,82,0,1"); /* spurious fridge-cabinet ChangeToMode */
  Hearth.poll();
  check("a spurious cluster-82 forward is denied", s.scriptDrained());
  check("without consulting the inherited callback", !consulted);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterCookSurface / composed MatterCooktop tests =====\n");
  test_begin_rebuild_emission_order();
  test_begin_registry_shape();
  test_zero_surface_cooktop_unchanged();
  test_zero_surface_rebuild_emission_unchanged();
  test_rebegin_after_reconcile_refused();
  test_addsurface_capacity();
  test_addsurface_after_begin_refused();
  test_standalone_surface_begin_refused();
  test_owned_surface_begin_declares_nothing();
  test_owned_surface_begin_flavour_mismatch_refused();
  test_owned_surface_reconcile_pushes_temperatures();
  test_remote_off_reaches_callback();
  test_setonoff_both_directions();
  test_unstarted_surface_setonoff_refused();
  test_attr_types();
  test_temperature_roundtrip();
  test_modes_api_dead_on_a_surface();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
