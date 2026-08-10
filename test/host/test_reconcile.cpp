#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterDoorLock.h"

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

/* Steady state: what the sketch declares is already on the C6. One query, no
 * writes, no reboot. This is every boot after the first. */
static void test_identical_composition_adopts_ids(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light, sensor;
  MatterEndPoint::hearthDeclare(&light, 0x0100);
  MatterEndPoint::hearthDeclare(&sensor, 0x0302);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\n+MTEP:1,2,0x0302\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("no further commands issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("first endpoint adopts ID 1", light.getEndPointId() == 1);
  check("second endpoint adopts ID 2", sensor.getEndPointId() == 2);
}

/* First boot: the C6 has nothing, so the sketch's declaration is applied.
 * +MTREADY is bundled into AT+MTEPAPPLY's own response, trailing the OK,
 * exactly as the real device sends it: OK first, then the reboot some time
 * later. HearthLink::command() returns on the OK and leaves the trailing
 * URC buffered for the wait loop's first poll() to pick up, so this
 * actually exercises that loop instead of short-circuiting it via a URC
 * that arrived before Matter.begin() was even called. */
static void test_empty_composition_applies(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("full apply sequence issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts the ID from the re-query", light.getEndPointId() == 1);
}

/* Order is part of the composition: same types, different sequence, must
 * re-apply. This is what makes endpoint IDs reproducible per spec 5.3. */
static void test_reordered_composition_applies(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint sensor, light;
  MatterEndPoint::hearthDeclare(&sensor, 0x0302);
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\n+MTEP:1,2,0x0302\r\nOK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0302", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0302\r\n+MTEP:1,2,0x0100\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("reorder triggers a re-apply", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("sensor is now endpoint 1", sensor.getEndPointId() == 1);
}

/* Spec 5.4: applying over a live fabric is allowed but never silent. */
static void test_commissioned_change_warns(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0101);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:1\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0101", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0101\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("it applies anyway, the sketch is the declaration of intent", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  /* The flag lives on Hearth, not on Matter: rule N2 forbids adding anything
   * to a Matter-named class, including test introspection. */
  check("and it warned first", Hearth.warnedAboutRecommission());
}

/* IMPORTANT 5: fail closed on a fabric query that returns an outright
 * ERROR. hearthQueryFabricCount() must not read that as "definitely zero
 * fabrics"; it already did not (a non-zero hearthCommand() return alone was
 * enough), but this pins the behaviour down. Does not disturb the apply
 * sequence: fabricsKnown/fabrics only gate the warning, not which commands
 * follow. */
static void test_fabric_query_error_still_warns(void) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.hearthSetWarnedAboutRecommission(false); /* the flag is a persistent Hearth member; start clean */
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "ERROR\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("a failed fabric query still warns (fail closed)", Hearth.warnedAboutRecommission());
  check("reconcile still completes", light.getEndPointId() == 1);
  check("no unexpected commands", s.unexpected().empty());
}

/* IMPORTANT 5, the specific hole the review found: an OK with no
 * +MTFABRICS: line (a truncated or malformed reply) is a *successful*
 * hearthCommand() return, so gating hearthQueryFabricCount() on the return
 * code alone reads this as "definitely zero fabrics" and skips the
 * warning. hearthQueryFabricCount() must instead confirm it actually
 * parsed a +MTFABRICS: line, the same way hearthQueryCodes()/
 * hearthQueryNet() gate on ctx.got. */
static void test_fabric_query_truncated_reply_still_warns(void) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.hearthSetWarnedAboutRecommission(false);
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "OK\r\n"); /* malformed: OK with no +MTFABRICS: line */
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("a truncated fabric reply still warns (fail closed)", Hearth.warnedAboutRecommission());
  check("reconcile still completes", light.getEndPointId() == 1);
  check("no unexpected commands", s.unexpected().empty());
}

/* CRITICAL 1 regression: the expected-reboot arm must be scoped to
 * AT+MTEPAPPLY only, not to the whole reconcile. A +MTREADY already sitting
 * on the wire before Matter.begin() is even called (an earlier, unrelated
 * notification, e.g. a prior spontaneous reboot the host has not yet
 * drained) must still be reported as an unexpected reboot rather than
 * silently absorbed. If the arm were taken before the query too, this
 * stray URC would satisfy hearthExpectedRebootSeen() before the apply's own
 * reboot has actually happened, and the wait loop below would exit without
 * ever waiting for the real completion -- the exact bug the review found. */
static void test_stray_ready_before_apply_is_still_reported(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n"); /* the apply's real reboot */
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Hearth.begin(s);

  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) {
    got = e;
    seen++;
  });

  s.injectURC("+MTREADY"); /* stray: already on the wire before begin() runs */
  Matter.begin();

  check("the stray +MTREADY is reported, not absorbed by the apply's arm", seen == 1 && got == HEARTH_COPROCESSOR_REBOOTED);
  check("reconcile still completes correctly afterwards", light.getEndPointId() == 1);
  check("full sequence issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  Hearth.onLinkEvent(nullptr); /* the lambda above outlives this stack frame otherwise */
}

/* The 15 s AT+MTEPAPPLY wait times out: no +MTREADY ever arrives. Endpoint
 * IDs must stay at 0, HEARTH_PROTOCOL_ERROR must fire, and -- the specific
 * regression Task 4 already had to fix once -- the arm must not linger: a
 * later, unrelated spontaneous +MTREADY must still be reported as a reboot
 * rather than being swallowed by a stale arm. g_yieldAdvanceMs is the
 * harness's documented hook for driving a busy-wait loop's clock forward
 * without an actual sleep. */
static void test_apply_timeout_reports_protocol_error_and_disarms(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n"); /* no +MTREADY ever follows */
  Hearth.begin(s);

  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) {
    got = e;
    seen++;
  });

  g_yieldAdvanceMs = 2000; /* drive millis() forward across the wait loop's yield()s */
  Matter.begin();
  g_yieldAdvanceMs = 0;

  check("timeout leaves the endpoint ID at 0", light.getEndPointId() == 0);
  check("timeout is reported as a protocol error", seen == 1 && got == HEARTH_PROTOCOL_ERROR);
  check("full apply attempt issued before timing out", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  seen = 0;
  s.injectURC("+MTREADY");
  Hearth.poll();
  check("the arm did not linger: a later +MTREADY is still a reboot", seen == 1 && got == HEARTH_COPROCESSOR_REBOOTED);

  Hearth.onLinkEvent(nullptr);
}

/* CRITICAL 2: a rejected AT+MTEP= write (unknown device type, or past the
 * firmware's endpoint cap) must abort the reconcile immediately, rather
 * than proceeding to AT+MTEPCLEAR / AT+MTEPAPPLY with a composition already
 * known to be wrong on the wire. */
static void test_rejected_write_aborts_immediately(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "+MTERR:6\r\nERROR\r\n"); /* unknown device type */
  Hearth.begin(s);

  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) {
    got = e;
    seen++;
  });

  Matter.begin();

  check("the rejected write leaves the endpoint ID at 0", light.getEndPointId() == 0);
  check("aborts as a protocol error without ever sending AT+MTEPAPPLY", seen == 1 && got == HEARTH_PROTOCOL_ERROR);
  check("no unexpected commands", s.unexpected().empty());
  /* The +MTERR:6 the wire actually sent must survive, not get overwritten
   * with the generic -2 timeout sentinel: that is the whole point of the
   * +MTERR grammar (S5), letting a host tell "wrong form" apart from
   * "that device type does not exist". */
  check("the real +MTERR code is preserved, not the timeout sentinel", Hearth.lastError() == 6);

  Hearth.onLinkEvent(nullptr);
}

/* CRITICAL 2: a composition that still does not match after one full apply
 * cycle must not be retried forever -- bounded to one apply and one
 * confirm. A persistent mismatch bails with HEARTH_PROTOCOL_ERROR instead
 * of a third AT+MTEPCLEAR / AT+MTEPAPPLY cycle (unbounded flash writes on a
 * state that cannot resolve itself). */
static void test_persistent_mismatch_bails_after_retry_cap(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "OK\r\n"); /* still empty: the apply did not take */
  Hearth.begin(s);

  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) {
    got = e;
    seen++;
  });

  Matter.begin();

  check("gives up rather than applying a third time", light.getEndPointId() == 0);
  check("reports a protocol error", seen == 1 && got == HEARTH_PROTOCOL_ERROR);
  check("exactly the two-round sequence was issued", s.scriptDrained());
  check("no third round was attempted", s.unexpected().empty());

  Hearth.onLinkEvent(nullptr);
}

/*
 * FINAL REVIEW, CRITICAL 2. kHearthMaxReconcileAttempts bounds one call, not
 * the boot. The README documents calling Matter.begin() from loop(), and
 * upstream's examples call isDeviceCommissioned() there, so a sketch whose
 * composition the C6 rejects (unknown device type, or past the 16-endpoint
 * cap) would run clear + writes + apply + reboot on every single loop
 * iteration, forever: unbounded NVS wear on the C6 and a co-processor that
 * never finishes rebooting. A reconcile that failed once must stay failed
 * for the rest of the boot; nothing about the inputs changes between
 * iterations, so retrying can only repeat the damage.
 */
static void test_failed_reconcile_is_latched_for_the_boot(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "+MTERR:6\r\nERROR\r\n"); /* the C6 rejects the type */
  Hearth.begin(s);

  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t) { seen++; });

  Matter.begin();
  check("the first attempt runs and fails", s.scriptDrained() && seen == 1);
  check("the failure is latched", Hearth.hearthReconcileFailed());

  /* Three more loop() iterations. The script is empty, so anything sent now
   * lands in unexpected(). g_yieldAdvanceMs is set for the same reason
   * test_timeout() in test_hearthlink.cpp sets it: if the latch ever
   * regresses, the command that should not have been sent gets no reply
   * from the drained mock and readLine() would otherwise spin on a clock
   * nothing advances. With it set, a regression fails the checks below in
   * a few milliseconds of simulated time instead of hanging the suite. */
  g_yieldAdvanceMs = 1;
  Matter.begin();
  Matter.begin();
  Matter.begin();
  g_yieldAdvanceMs = 0;
  check("later Matter.begin() calls issue nothing at all", s.unexpected().empty());
  check("and do not re-raise the protocol error", seen == 1);

  Hearth.onLinkEvent(nullptr);
}

/* The latch must not outlive the thing it protects: Hearth.begin() (a fresh
 * link) and hearthClearDeclarations() (a fresh composition) both clear it,
 * so a genuinely new set of inputs gets a genuine attempt. Without this the
 * latch would be indistinguishable from a permanently dead library. */
static void test_reconcile_latch_clears_with_a_fresh_composition(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint bad;
  MatterEndPoint::hearthDeclare(&bad, 0x0100);

  MockStream s1;
  s1.expect("AT+MTEP?", "ERROR\r\n");
  Hearth.begin(s1);
  Matter.begin();
  check("the first composition failed and latched", Hearth.hearthReconcileFailed());

  MatterEndPoint::hearthClearDeclarations();
  check("clearing the declarations clears the latch", !Hearth.hearthReconcileFailed());

  TestEndPoint good;
  MatterEndPoint::hearthDeclare(&good, 0x0100);
  MockStream s2;
  s2.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Hearth.begin(s2);
  Matter.begin();
  check("and the next reconcile runs normally", good.getEndPointId() == 1);
  check("no unexpected commands", s2.unexpected().empty());
}

static void test_event_urc_maps_to_enum(void) {
  MockStream s;
  Hearth.begin(s);
  matterEvent_t got = MATTER_SERVER_READY;
  int seen = 0;
  Matter.onEvent([&](matterEvent_t e, const chip::DeviceLayer::ChipDeviceEvent *) {
    got = e; seen++;
  });
  s.injectURC("+MTEVT:3");
  Hearth.poll();
  check("bit 3 is commissioning complete", seen == 1 && got == MATTER_COMMISSIONING_COMPLETE);
  check("no unexpected commands", s.unexpected().empty());
  /* ArduinoMatter::_matterEventCB is a static: it outlives this function's
   * stack frame, and the lambda above captures got/seen from that frame by
   * reference. Left registered, any later URC dispatch anywhere in this
   * binary would write through dangling references. Clear it before the
   * locals it closes over go out of scope. */
  Matter.onEvent(nullptr);
}

static void test_pairing_code(void) {
  MockStream s;
  s.expect("AT+MTCODES?", "+MTCODES:MT:Y.K9042C00KA0648G00,34970112332\r\nOK\r\n");
  Hearth.begin(s);
  check("manual code parsed", Matter.getManualPairingCode() == String("34970112332"));
  check("no unexpected commands", s.unexpected().empty());
}

static void test_commissioned_query(void) {
  MockStream s;
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:2\r\nOK\r\n");
  Hearth.begin(s);
  check("zero fabrics is uncommissioned", !Matter.isDeviceCommissioned());
  check("two fabrics is commissioned", Matter.isDeviceCommissioned());
  check("no unexpected commands", s.unexpected().empty());
}

/* Endpoints registering after Matter.begin() are outside the reconciled
 * composition. Upstream's own examples call Matter.begin() last; this makes
 * getting it wrong loud rather than silent. */
static void test_late_declaration_is_reported(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  Hearth.begin(s);
  Matter.begin();
  TestEndPoint late;
  check("a late declaration is refused", !MatterEndPoint::hearthDeclare(&late, 0x0100));
  check("and is reported as a protocol error", Hearth.lastError() != 0);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * C4: composition variants (AT_MT_SPEC.md S3.9). hearthDeclare()'s three-arg
 * form threads a per-device-type variant through the same identical-vs-
 * rebuild comparison that already covers device type, so a variant change
 * with no device-type change still forces a rebuild (the C6 must run the
 * fifth abort trap's TemperatureNumber/TemperatureLevel branch that matches
 * what the sketch actually declared).
 */
static void test_declared_variant_emits_variant_during_rebuild(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint cabinet;
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0071,1", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071,1\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("the variant is emitted alongside the device type", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts the ID from the re-query", cabinet.getEndPointId() == 1);
}

/* Adopt matches only when the reported variant equals the declared one: a
 * 4-field +MTEP? line reporting the same variant the sketch declared is the
 * identical case, one query, zero writes. */
static void test_matching_4field_variant_adopts_without_rebuild(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint cabinet;
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071,1\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("no further commands issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts ID 1 without a rebuild", cabinet.getEndPointId() == 1);
}

/* The 3-field form (no 4th field at all) is variant 0, per AT_MT_SPEC.md
 * S3.9's byte-identical-output guarantee: a declared variant 0 must adopt
 * against it exactly as it always did, before this task existed. */
static void test_3field_line_matches_declared_variant_zero(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint cabinet;
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071); /* two-arg form: variant 0 */

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("no further commands issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts ID 1 without a rebuild", cabinet.getEndPointId() == 1);
}

/* A live variant 0 against a declared variant 1 (or vice versa) is a
 * mismatch even though the device type is identical: it must trigger a
 * rebuild, not be silently adopted as if the C6 already had what the sketch
 * wants. */
static void test_variant_mismatch_triggers_rebuild(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint cabinet;
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071, 1);

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n"); /* live: variant 0 */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0071,1", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071,1\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("a variant-only mismatch still triggers a rebuild", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts the ID from the re-query", cabinet.getEndPointId() == 1);
}

/* Review fix round 1, Minor 3: the reverse direction of the mismatch above.
 * A declared variant 0 (the two-arg hearthDeclare() form) against a live
 * 4-field line reporting variant 1 must also trigger a rebuild; only the
 * "declared 1, live 3-field (0)" direction had a dedicated test before this. */
static void test_declared_variant_zero_vs_stored_4field_variant_triggers_rebuild(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint cabinet;
  MatterEndPoint::hearthDeclare(&cabinet, 0x0071); /* two-arg form: variant 0 */

  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071,1\r\nOK\r\n"); /* live: variant 1 */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0071", "OK\r\n"); /* declared variant 0: no variant field emitted */
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0071\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();

  check("a declared-zero vs stored-4field-nonzero mismatch also triggers a rebuild", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts the ID from the re-query", cabinet.getEndPointId() == 1);
}

/*
 * C3: MatterDoorLock's reconcile push (B120 norm, see its header comment).
 * ArduinoMatter::begin() calls hearthOnReconciled() exactly once per
 * declared endpoint on BOTH the "identical" (adopt, one query, zero writes)
 * and the rebuild (clear/apply/re-query) branches -- see the "identical"
 * check and its own comment above -- so the door lock's AT+MTLOCK push must
 * fire on both, not only the first boot's rebuild. Test both explicitly:
 * the class's own test/host/test_doorlock.cpp only ever exercises the adopt
 * path (via its bringUp() helper), so the rebuild path has no coverage
 * without a dedicated test here.
 */
static void test_doorlock_reconcile_adopt_pushes_lock_state(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterDoorLock lock;
  MockStream s;
  Hearth.begin(s);
  check("begin(true) declares", lock.begin(true));
  s.expect("AT+MTEP?", "+MTEP:0,1,0x000A\r\nOK\r\n"); /* already matches: one query, no rebuild */
  s.expect("AT+MTLOCK=1,1,1", "OK\r\n"); /* Locked, kSourceManual */
  Matter.begin();

  check("the adopt path still pushes the begun lock state exactly once", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts ID 1", lock.getEndPointId() == 1);
  check("cache reflects the begun state", lock.getLockState() == MatterDoorLock::kStateLocked);
}

static void test_doorlock_reconcile_rebuild_pushes_lock_state(void) {
  MatterEndPoint::hearthClearDeclarations();
  MatterDoorLock lock;
  MockStream s;
  Hearth.begin(s);
  check("begin(false) declares", lock.begin(false));
  s.expect("AT+MTEP?", "OK\r\n"); /* empty: first boot, must rebuild */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x000A", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x000A\r\nOK\r\n");
  s.expect("AT+MTLOCK=1,2,1", "OK\r\n"); /* Unlocked, kSourceManual */
  Matter.begin();

  check("the rebuild path pushes the begun lock state exactly once, after the re-query", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint adopts the ID from the re-query", lock.getEndPointId() == 1);
  check("cache reflects the begun state", lock.getLockState() == MatterDoorLock::kStateUnlocked);
}

/*
 * Task 10 (composed-appliance round, routed from Task 7's review): the
 * firmware's MT_COMP_MAX_ENDPOINTS is 24 now, so a live composition can
 * legally answer AT+MTEP? with more than 16 lines. HEARTH_MAX_ENDPOINTS
 * must match: at the old 16, hearthOnEpLine() silently dropped every line
 * past the sixteenth, so a 17-endpoint declared registry could never
 * compare equal to its own live composition and every boot recomposed (and
 * rebooted the C6) for no reason. Pin the whole path: 17 declared
 * endpoints, a 17-line reply, one query, zero writes, every endpoint
 * adopting its ID, including the seventeenth.
 */
static void test_seventeen_endpoint_composition_adopts_without_recompose(void) {
  MatterEndPoint::hearthClearDeclarations();
  static const int kCount = 17;
  static TestEndPoint eps[kCount];
  bool allDeclared = true;
  for (int i = 0; i < kCount; i++) {
    allDeclared &= MatterEndPoint::hearthDeclare(&eps[i], 0x0100);
  }
  check("all 17 declarations are accepted (cap is 24 now)", allDeclared);

  std::string reply;
  for (int i = 0; i < kCount; i++) {
    char line[48];
    snprintf(line, sizeof(line), "+MTEP:%d,%d,0x0100\r\n", i, i + 1);
    reply += line;
  }
  reply += "OK\r\n";

  MockStream s;
  s.expect("AT+MTEP?", reply);
  Hearth.begin(s);
  Matter.begin();

  check("a 17-line reply parses completely: one query, no recompose", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("endpoint 1 adopts ID 1", eps[0].getEndPointId() == 1);
  check("endpoint 16 adopts ID 16", eps[15].getEndPointId() == 16);
  check("endpoint 17 adopts ID 17, past the old 16-line parse cap", eps[16].getEndPointId() == 17);
}

int main(void) {
  printf("\n===== reconcile and ArduinoMatter tests =====\n");
  test_identical_composition_adopts_ids();
  test_empty_composition_applies();
  test_reordered_composition_applies();
  test_commissioned_change_warns();
  test_fabric_query_error_still_warns();
  test_fabric_query_truncated_reply_still_warns();
  test_stray_ready_before_apply_is_still_reported();
  test_apply_timeout_reports_protocol_error_and_disarms();
  test_rejected_write_aborts_immediately();
  test_persistent_mismatch_bails_after_retry_cap();
  test_failed_reconcile_is_latched_for_the_boot();
  test_reconcile_latch_clears_with_a_fresh_composition();
  test_event_urc_maps_to_enum();
  test_pairing_code();
  test_commissioned_query();
  test_late_declaration_is_reported();
  test_declared_variant_emits_variant_during_rebuild();
  test_matching_4field_variant_adopts_without_rebuild();
  test_3field_line_matches_declared_variant_zero();
  test_variant_mismatch_triggers_rebuild();
  test_declared_variant_zero_vs_stored_4field_variant_triggers_rebuild();
  test_doorlock_reconcile_adopt_pushes_lock_state();
  test_doorlock_reconcile_rebuild_pushes_lock_state();
  test_seventeen_endpoint_composition_adopts_without_recompose();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
