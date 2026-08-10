/*
 * Task 7 (composed-appliance round): parent-aware endpoint declaration and
 * reconcile. The firmware's AT+MTEP grammar gained an optional third field,
 * the composition index of an earlier entry this endpoint is parented under
 * (AT_MT_SPEC.md S3.9): "AT+MTEP=<devtype>[,<variant>[,<parent_idx>]]" on
 * the way in, "+MTEP:<idx>,<ep_id>,<devtype>[,<variant>[,<parent_idx>]]" on
 * the way out. When a parent exists BOTH trailing fields appear (the variant
 * explicit even when 0, since the parent cannot be sent without it);
 * unparented lines keep the old 3- and 4-field shapes byte-identical, which
 * is what keeps every pre-parenting host working unmodified.
 *
 * The parent is part of what makes two compositions identical, exactly like
 * the variant before it: a changed parent on an otherwise-identical
 * declaration must trigger a recompose, and a live composition matching the
 * declared one including parents must adopt with one query and zero writes.
 */
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

class TestEndPoint : public MatterEndPoint {
public:
  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    return true;
  }
};

/* The registry round-trip: the four-arg hearthDeclare() stores the parent
 * index, hearthDeclaredParentAt() returns it, and every pre-existing declare
 * form reports HEARTH_NO_PARENT, so nothing about the 41 existing endpoint
 * classes' declarations changes. */
static void test_declare_with_parent_registry_roundtrip(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, cabinet, light;
  check("unparented declare (2-arg) still succeeds", MatterEndPoint::hearthDeclare(&fridge, 0x0070));
  check("parented declare (4-arg) succeeds", MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1, 0));
  check("unparented declare (3-arg) still succeeds", MatterEndPoint::hearthDeclare(&light, 0x0100, 0));

  check("count is 3", MatterEndPoint::hearthDeclaredCount() == 3);
  check("2-arg entry reports HEARTH_NO_PARENT", MatterEndPoint::hearthDeclaredParentAt(0) == MatterEndPoint::HEARTH_NO_PARENT);
  check("4-arg entry reports its parent index", MatterEndPoint::hearthDeclaredParentAt(1) == 0);
  check("3-arg entry reports HEARTH_NO_PARENT", MatterEndPoint::hearthDeclaredParentAt(2) == MatterEndPoint::HEARTH_NO_PARENT);
  check("type survives alongside the parent", MatterEndPoint::hearthDeclaredTypeAt(1) == 0x0071);
  check("variant survives alongside the parent", MatterEndPoint::hearthDeclaredVariantAt(1) == 1);
  check("out-of-range index reports HEARTH_NO_PARENT", MatterEndPoint::hearthDeclaredParentAt(7) == MatterEndPoint::HEARTH_NO_PARENT);

  /* Pre-reconcile re-declaration updates in place (the begin(); end();
   * begin(); case the registry already handles for type and variant): the
   * parent must update with it, not survive stale. */
  check("re-declare updates the parent in place", MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1));
  check("and the entry now reports HEARTH_NO_PARENT", MatterEndPoint::hearthDeclaredParentAt(1) == MatterEndPoint::HEARTH_NO_PARENT);
  check("without growing the registry", MatterEndPoint::hearthDeclaredCount() == 3);
}

/* The identical case with parents: a 5-field +MTEP? line (parent present, so
 * the variant is explicit even at 0) matching the declared composition
 * including the parent adopts with one query and zero writes. */
static void test_matching_5field_line_adopts_without_rebuild(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, cabinet;
  MatterEndPoint::hearthDeclare(&fridge, 0x0070);
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 0, 0);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  Hearth.begin(s);
  /* g_yieldAdvanceMs: if a regression makes the emission (or the compare
   * verdict) diverge from the script, the mismatched command gets no reply
   * from the mock and readLine() would spin on a clock nothing advances.
   * With it set, such a regression fails the checks below in milliseconds
   * of simulated time instead of hanging the suite; a correct run is
   * unaffected, since every scripted reply arrives immediately. Same
   * pattern as test_reconcile.cpp's latch test. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("no further commands issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("parent endpoint adopts ID 1", fridge.getEndPointId() == 1);
  check("child endpoint adopts ID 2", cabinet.getEndPointId() == 2);
}

/* Same, with a nonzero variant riding alongside the parent: all five fields
 * carry independent information and all five must round-trip. */
static void test_matching_5field_nonzero_variant_adopts(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, cabinet;
  MatterEndPoint::hearthDeclare(&fridge, 0x0070);
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1, 0);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,1,0\r\nOK\r\n");
  Hearth.begin(s);
  /* g_yieldAdvanceMs: if a regression makes the emission (or the compare
   * verdict) diverge from the script, the mismatched command gets no reply
   * from the mock and readLine() would spin on a clock nothing advances.
   * With it set, such a regression fails the checks below in milliseconds
   * of simulated time instead of hanging the suite; a correct run is
   * unaffected, since every scripted reply arrives immediately. Same
   * pattern as test_reconcile.cpp's latch test. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("no further commands issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("child endpoint adopts ID 2", cabinet.getEndPointId() == 2);
}

/* A parent-only change must trigger a recompose, exactly like a variant-only
 * change: same device types, same variants, but the live composition has no
 * parent link where the sketch declares one. Also pins the parented emission
 * byte-shape: "AT+MTEP=0x%04lX,%u,%u" with the variant explicit even when 0,
 * since the wire cannot carry a parent without it. */
static void test_parent_only_change_triggers_recompose(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, cabinet;
  MatterEndPoint::hearthDeclare(&fridge, 0x0070);
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 0, 0);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071\r\nOK\r\n"); /* live: unparented */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0070", "OK\r\n");
  s.expect("AT+MTEP=0x0071,0,0", "OK\r\n"); /* variant explicit even at 0 */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\nOK\r\n");
  Hearth.begin(s);
  /* g_yieldAdvanceMs: if a regression makes the emission (or the compare
   * verdict) diverge from the script, the mismatched command gets no reply
   * from the mock and readLine() would spin on a clock nothing advances.
   * With it set, such a regression fails the checks below in milliseconds
   * of simulated time instead of hanging the suite; a correct run is
   * unaffected, since every scripted reply arrives immediately. Same
   * pattern as test_reconcile.cpp's latch test. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("a parent-only mismatch triggers a recompose", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("child endpoint adopts the ID from the re-query", cabinet.getEndPointId() == 2);
}

/* The reverse direction: the live composition carries a parent link the
 * sketch does not declare. The compare treats the missing 5th field as
 * HEARTH_NO_PARENT, so this is a mismatch too, and the unparented rebuild
 * emission stays byte-identical to 0.6.0 (no trailing fields at variant 0,
 * variant only when nonzero). */
static void test_declared_unparented_vs_live_parent_triggers_recompose(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, cabinet;
  MatterEndPoint::hearthDeclare(&fridge, 0x0070);
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,1,0\r\nOK\r\n"); /* live: parented */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0070", "OK\r\n"); /* unparented, variant 0: the 0.6.0 shape */
  s.expect("AT+MTEP=0x0071,1", "OK\r\n"); /* unparented, variant 1: the 0.6.0 shape */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,1\r\nOK\r\n");
  Hearth.begin(s);
  /* g_yieldAdvanceMs: if a regression makes the emission (or the compare
   * verdict) diverge from the script, the mismatched command gets no reply
   * from the mock and readLine() would spin on a clock nothing advances.
   * With it set, such a regression fails the checks below in milliseconds
   * of simulated time instead of hanging the suite; a correct run is
   * unaffected, since every scripted reply arrives immediately. Same
   * pattern as test_reconcile.cpp's latch test. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("a live-parent vs declared-unparented mismatch also triggers a recompose", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("child endpoint adopts the ID from the re-query", cabinet.getEndPointId() == 2);
}

/* A changed parent VALUE on an otherwise-identical declaration (both sides
 * parented, different target) is a recompose too: reparenting a cabinet from
 * the fridge to the oven changes the composed data model even though every
 * per-endpoint field but the parent is the same. */
static void test_changed_parent_value_triggers_recompose(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, oven, cabinet;
  MatterEndPoint::hearthDeclare(&fridge, 0x0070);
  MatterEndPoint::hearthDeclare(&oven, 0x007B);
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 0, 1);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x007B\r\n+MTEP:2,3,0x0071,0,0\r\nOK\r\n"); /* live parent: 0 */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0070", "OK\r\n");
  s.expect("AT+MTEP=0x007B", "OK\r\n");
  s.expect("AT+MTEP=0x0071,0,1", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x007B\r\n+MTEP:2,3,0x0071,0,1\r\nOK\r\n");
  Hearth.begin(s);
  /* g_yieldAdvanceMs: if a regression makes the emission (or the compare
   * verdict) diverge from the script, the mismatched command gets no reply
   * from the mock and readLine() would spin on a clock nothing advances.
   * With it set, such a regression fails the checks below in milliseconds
   * of simulated time instead of hanging the suite; a correct run is
   * unaffected, since every scripted reply arrives immediately. Same
   * pattern as test_reconcile.cpp's latch test. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("a changed parent value triggers a recompose", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("child endpoint adopts the ID from the re-query", cabinet.getEndPointId() == 3);
}

/* First-boot rebuild of a parented composition from an empty C6: pins the
 * full emission sequence verbatim, parented and unparented entries side by
 * side, nonzero variant riding with the parent. */
static void test_parented_rebuild_emission_shapes(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint fridge, cabinetA, cabinetB;
  MatterEndPoint::hearthDeclare(&fridge, 0x0070);
  MatterEndPoint::hearthDeclare(&cabinetA, 0x0071, 0, 0);
  MatterEndPoint::hearthDeclare(&cabinetB, 0x0071, 1, 0);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n"); /* empty: first boot */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0070", "OK\r\n");
  s.expect("AT+MTEP=0x0071,0,0", "OK\r\n");
  s.expect("AT+MTEP=0x0071,1,0", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0070\r\n+MTEP:1,2,0x0071,0,0\r\n+MTEP:2,3,0x0071,1,0\r\nOK\r\n");
  Hearth.begin(s);
  /* g_yieldAdvanceMs: if a regression makes the emission (or the compare
   * verdict) diverge from the script, the mismatched command gets no reply
   * from the mock and readLine() would spin on a clock nothing advances.
   * With it set, such a regression fails the checks below in milliseconds
   * of simulated time instead of hanging the suite; a correct run is
   * unaffected, since every scripted reply arrives immediately. Same
   * pattern as test_reconcile.cpp's latch test. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("the full parented apply sequence is issued verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("parent adopts ID 1", fridge.getEndPointId() == 1);
  check("first child adopts ID 2", cabinetA.getEndPointId() == 2);
  check("second child adopts ID 3", cabinetB.getEndPointId() == 3);
}

int main(void) {
  printf("\n===== composition parent tests =====\n");
  test_declare_with_parent_registry_roundtrip();
  test_matching_5field_line_adopts_without_rebuild();
  test_matching_5field_nonzero_variant_adopts();
  test_parent_only_change_triggers_recompose();
  test_declared_unparented_vs_live_parent_triggers_recompose();
  test_changed_parent_value_triggers_recompose();
  test_parented_rebuild_emission_shapes();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
