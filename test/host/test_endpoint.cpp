#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* Concrete stand-in: the base class is abstract. */
class TestEndPoint : public MatterEndPoint {
public:
  int changes = 0;
  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    changes++;
    return true;
  }
};

/*
 * RE-REVIEW, IMPORTANT 2. The two modes differ on the wire in what comes
 * back, not just in what the firmware does with the value: mode 1 goes
 * through esp_matter::attribute::update(), which fires the firmware's own
 * POST_UPDATE callback and so echoes a +MTATTR URC before OK, while mode 0
 * goes through attribute::set_val() and echoes nothing (main.cpp:393,
 * AT_MT_SPEC.md S3.8). That asymmetry is the whole reason mode 0 exists.
 * Scripted here as the firmware behaves.
 *
 * This endpoint is not in the declaration registry, so the echo has no
 * target and hearthDispatchAttr() drops it. That is the documented policy
 * for an endpoint the sketch never declared, and it is asserted rather than
 * assumed.
 */
static void test_write_modes(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  s.expect("AT+MTATTR=1,6,0,1,0", "OK\r\n");
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  Hearth.begin(s);
  TestEndPoint ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v = esp_matter_bool(true);
  check("setAttributeVal writes mode 0", ep.setAttributeVal(0x0006, 0x0000, &v));
  check("updateAttributeVal writes mode 1", ep.updateAttributeVal(0x0006, 0x0000, &v));
  check("script drained", s.scriptDrained());
  check("the mode-1 echo reached no undeclared endpoint", ep.changes == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_read(void) {
  MockStream s;
  s.expect("AT+MTATTR=1,6,0", "+MTATTR:1,6,0,1\r\nOK\r\n");
  Hearth.begin(s);
  TestEndPoint ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v = esp_matter_bool(false);
  check("read succeeds", ep.getAttributeVal(0x0006, 0x0000, &v));
  check("read returns the reported value", v.val.b == true);
}

static void test_unknown_endpoint_reports_code(void) {
  MockStream s;
  s.expect("AT+MTATTR=9,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  Hearth.begin(s);
  TestEndPoint ep;
  ep.setEndPointId(9);
  esp_matter_attr_val_t v = esp_matter_bool(true);
  check("a failed write returns false", !ep.updateAttributeVal(0x0006, 0x0000, &v));
  check("the +MTERR code is retained", Hearth.lastError() == 2);
}

static void test_read_rejects_invalid_type(void) {
  /* attrVal->type is the target type for the rebuild (the wire only carries
   * a bare integer); ESP_MATTER_VAL_TYPE_INVALID must be refused before the
   * command is even sent, not handed to hearthAttrValFromLong to guess at. */
  MockStream s;  // no expect(): the command must never reach the wire
  Hearth.begin(s);
  TestEndPoint ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v;
  v.type = ESP_MATTER_VAL_TYPE_INVALID;
  v.val.u = 0;
  check("a read with an invalid target type fails", !ep.getAttributeVal(0x0006, 0x0000, &v));
  check("the type-not-carryable code is set", Hearth.lastError() == 5);
  check("no command was sent to the wire", s.unexpected().empty());
}

static void test_declaration_order(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a, b;
  check("first declaration accepted", MatterEndPoint::hearthDeclare(&a, 0x0100));
  check("second declaration accepted", MatterEndPoint::hearthDeclare(&b, 0x0302));
  check("count is 2", MatterEndPoint::hearthDeclaredCount() == 2);
  check("order preserved", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0100
                        && MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0302);
}

static void test_lookup_by_endpoint_id(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a;
  MatterEndPoint::hearthDeclare(&a, 0x0100);
  a.setEndPointId(4);
  check("lookup finds the endpoint", MatterEndPoint::hearthFindByEndPointId(4) == &a);
  check("lookup misses cleanly", MatterEndPoint::hearthFindByEndPointId(7) == nullptr);
}

static void test_identify_callback(void) {
  TestEndPoint ep;
  bool seen = false, state = false;
  ep.onIdentify([&](bool on) { seen = true; state = on; return true; });
  ep.endpointIdentifyCB(1, true);
  check("identify callback fires", seen && state);
}

/*
 * FINAL REVIEW, IMPORTANT 2. The registry holds raw pointers and nothing
 * ever removed one. A destroyed endpoint (a scope-local device object, or
 * one owned by something that goes away) therefore left a dangling pointer
 * behind that hearthFindByEndPointId() dereferences on the very next
 * +MTATTR URC. Destruction must deregister.
 */
static void test_destroyed_endpoint_is_deregistered(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint keep;
  MatterEndPoint::hearthDeclare(&keep, 0x0100);
  keep.setEndPointId(1);
  {
    TestEndPoint transient;
    MatterEndPoint::hearthDeclare(&transient, 0x0302);
    transient.setEndPointId(2);
    check("both are registered", MatterEndPoint::hearthDeclaredCount() == 2);
    check("the transient one is findable while alive", MatterEndPoint::hearthFindByEndPointId(2) == &transient);
  }
  check("destruction removed it from the registry", MatterEndPoint::hearthDeclaredCount() == 1);
  check("and it is no longer findable", MatterEndPoint::hearthFindByEndPointId(2) == nullptr);
  check("the surviving endpoint is untouched", MatterEndPoint::hearthDeclaredAt(0) == &keep
                                            && MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0100);
}

/* Removal must preserve declaration order for everything after it: endpoint
 * IDs are assigned from that order, so compacting the array the wrong way
 * would silently reshuffle the composition. */
static void test_deregistration_preserves_order(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a, c;
  {
    TestEndPoint b;
    MatterEndPoint::hearthDeclare(&a, 0x0100);
    MatterEndPoint::hearthDeclare(&b, 0x0101);
    MatterEndPoint::hearthDeclare(&c, 0x0302);
    check("three declared", MatterEndPoint::hearthDeclaredCount() == 3);
  }
  check("the middle one is gone", MatterEndPoint::hearthDeclaredCount() == 2);
  check("order of the survivors is preserved",
        MatterEndPoint::hearthDeclaredAt(0) == &a && MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0100
        && MatterEndPoint::hearthDeclaredAt(1) == &c && MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0302);
}

/*
 * FINAL REVIEW, IMPORTANT 2, second half. begin(); end(); begin(); on the
 * same device object used to append a second entry for it, silently
 * changing the composition from one endpoint to two and so triggering a
 * full clear/apply/reboot cycle on the next Matter.begin(). Re-declaring an
 * endpoint that is already registered is a no-op, not an addition.
 */
static void test_repeat_declaration_is_idempotent(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a;
  check("first declaration accepted", MatterEndPoint::hearthDeclare(&a, 0x0100));
  check("a repeat declaration is accepted too", MatterEndPoint::hearthDeclare(&a, 0x0100));
  check("but does not grow the registry", MatterEndPoint::hearthDeclaredCount() == 1);
  check("and keeps the one entry it had", MatterEndPoint::hearthDeclaredAt(0) == &a
                                       && MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0100);
}

/*
 * RE-REVIEW, MINOR 2, first branch. Re-declaring the same object with a
 * *different* device type before reconcile is a genuine composition change
 * and is taken: the sketch is still setting itself up, nothing has been
 * reconciled against the C6 yet, and the alternative (appending) is the bug
 * the idempotence rule exists to prevent. Untested until now.
 */
static void test_repeat_declaration_with_a_new_type_updates_in_place(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a, b;
  check("first declaration accepted", MatterEndPoint::hearthDeclare(&a, 0x0100));
  check("a second endpoint after it", MatterEndPoint::hearthDeclare(&b, 0x0302));
  check("re-declaring the first with a new type is accepted", MatterEndPoint::hearthDeclare(&a, 0x0101));
  check("the registry did not grow", MatterEndPoint::hearthDeclaredCount() == 2);
  check("the entry was updated in place", MatterEndPoint::hearthDeclaredAt(0) == &a
                                       && MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0101);
  check("and the endpoint after it kept its position", MatterEndPoint::hearthDeclaredAt(1) == &b
                                                    && MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0302);
}

/*
 * RE-REVIEW, MINOR 2 second branch, and MINOR 3. After reconcile, *any*
 * re-declaration of an already-registered endpoint is refused, whether or
 * not the device type changed.
 *
 * A changed type is a composition change arriving too late to be reconciled,
 * so it goes through the same +MTERR:10 refusal as a brand new declaration.
 *
 * An *exact repeat* used to be allowed through, on the reasoning that it
 * changes nothing. It changes nothing in the registry, but the caller is
 * MatterOnOffLight::begin(initialState) and friends, which take a true
 * return as licence to overwrite their cached state with the sketch's
 * initial value and never write it to the wire. A sketch calling
 * light.begin(true) after Matter.begin() therefore ended up believing the
 * light was on while the C6 still had it off, and the next setOnOff(true)
 * short-circuited on the equality check and never turned it on: a silent
 * divergence with no error anywhere.
 *
 * Refusing is also exact upstream parity. arduino-esp32 3.3.8's
 * MatterOnOffLight::begin() opens with `if (getEndPointId() != 0) { log_e(
 * "... has already been created."); return false; }`, and every other
 * endpoint class does the same.
 */
static void test_repeat_declaration_after_reconcile_is_refused(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a;
  check("first declaration accepted", MatterEndPoint::hearthDeclare(&a, 0x0100));
  MatterEndPoint::hearthMarkReconciled();
  a.setEndPointId(1);

  Hearth.hearthSetError(0);
  check("an exact repeat is refused after reconcile", !MatterEndPoint::hearthDeclare(&a, 0x0100));
  check("carrying the composition-rejected code", Hearth.lastError() == 10);

  Hearth.hearthSetError(0);
  check("a repeat with a different type is refused too", !MatterEndPoint::hearthDeclare(&a, 0x0101));
  check("carrying the same code", Hearth.lastError() == 10);

  check("neither attempt disturbed the registry", MatterEndPoint::hearthDeclaredCount() == 1
                                               && MatterEndPoint::hearthDeclaredAt(0) == &a
                                               && MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0100);
  check("and the adopted endpoint id is untouched", a.getEndPointId() == 1);
}

/*
 * FINAL REVIEW, IMPORTANT 3. Every declared endpoint's endpoint_id is 0
 * until reconcile adopts a real one, and 0 is the C6's Root Node. A lookup
 * for 0 therefore matched the first declared endpoint, so after a reconcile
 * that never ran or aborted, a stray +MTATTR:0,... would be delivered to a
 * device object it has nothing to do with. Endpoint 0 must never match.
 */
static void test_endpoint_zero_never_matches_a_declared_endpoint(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint a, b;
  MatterEndPoint::hearthDeclare(&a, 0x0100);
  MatterEndPoint::hearthDeclare(&b, 0x0302);
  check("both are still unreconciled", a.getEndPointId() == 0 && b.getEndPointId() == 0);
  check("a lookup for the Root Node finds nothing", MatterEndPoint::hearthFindByEndPointId(0) == nullptr);
  a.setEndPointId(1);
  check("a lookup for 0 still finds nothing once one is reconciled",
        MatterEndPoint::hearthFindByEndPointId(0) == nullptr);
  check("and the reconciled one is found normally", MatterEndPoint::hearthFindByEndPointId(1) == &a);
}

/*
 * FINAL REVIEW, IMPORTANT 3, second half. The same 0 on the write side:
 * hearthWriteAttr() formatted AT+MTATTR=0,... whenever reconcile had not
 * run, so a sketch carrying on after a failed Matter.begin() silently read
 * and wrote the co-processor's Root Node. An attribute access on an
 * unreconciled endpoint must fail before it reaches the wire, carrying the
 * protocol's own "unknown endpoint" code.
 */
static void test_attr_access_on_unreconciled_endpoint_fails_loudly(void) {
  MockStream s;  // no expect(): nothing may reach the wire
  Hearth.begin(s);
  TestEndPoint ep;  // never reconciled, so endpoint_id is still 0
  esp_matter_attr_val_t v = esp_matter_bool(true);

  /* If the endpoint-0 guard regresses, the command does reach the drained
   * mock, which never answers; keep the simulated clock moving so that is a
   * failed check rather than a hung suite (same hook, and same reason, as
   * test_timeout() in test_hearthlink.cpp). */
  g_yieldAdvanceMs = 1;
  check("setAttributeVal refuses", !ep.setAttributeVal(0x0006, 0x0000, &v));
  check("and reports the unknown-endpoint code", Hearth.lastError() == 2);
  Hearth.hearthSetError(0);
  check("updateAttributeVal refuses", !ep.updateAttributeVal(0x0006, 0x0000, &v));
  check("and reports the unknown-endpoint code", Hearth.lastError() == 2);
  Hearth.hearthSetError(0);
  check("getAttributeVal refuses", !ep.getAttributeVal(0x0006, 0x0000, &v));
  check("and reports the unknown-endpoint code", Hearth.lastError() == 2);
  g_yieldAdvanceMs = 0;
  check("nothing reached the wire", s.unexpected().empty());
}

static void test_declaration_registry_full(void) {
  /* hearthDeclare() must reject the (HEARTH_MAX_ENDPOINTS + 1)th endpoint
   * rather than truncating the registry silently: a regression here would
   * leave a sketch's later endpoints quietly missing from the data model. */
  MatterEndPoint::hearthClearDeclarations();
  static TestEndPoint eps[HEARTH_MAX_ENDPOINTS + 1];
  bool allFilled = true;
  for (int i = 0; i < HEARTH_MAX_ENDPOINTS; i++) {
    if (!MatterEndPoint::hearthDeclare(&eps[i], 0x0100)) {
      allFilled = false;
    }
  }
  check("all HEARTH_MAX_ENDPOINTS declarations are accepted", allFilled);
  check("count reaches the cap", MatterEndPoint::hearthDeclaredCount() == HEARTH_MAX_ENDPOINTS);
  check("the next declaration is rejected", !MatterEndPoint::hearthDeclare(&eps[HEARTH_MAX_ENDPOINTS], 0x0100));
  check("count does not grow past the cap", MatterEndPoint::hearthDeclaredCount() == HEARTH_MAX_ENDPOINTS);
}

int main(void) {
  printf("\n===== MatterEndPoint tests =====\n");
  test_write_modes();
  test_read();
  test_unknown_endpoint_reports_code();
  test_read_rejects_invalid_type();
  test_declaration_order();
  test_lookup_by_endpoint_id();
  test_identify_callback();
  test_destroyed_endpoint_is_deregistered();
  test_deregistration_preserves_order();
  test_repeat_declaration_is_idempotent();
  test_repeat_declaration_with_a_new_type_updates_in_place();
  test_repeat_declaration_after_reconcile_is_refused();
  test_endpoint_zero_never_matches_a_declared_endpoint();
  test_attr_access_on_unreconciled_endpoint_fails_loudly();
  test_declaration_registry_full();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
