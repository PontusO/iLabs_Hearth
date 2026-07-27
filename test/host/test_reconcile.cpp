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

int main(void) {
  printf("\n===== reconcile and ArduinoMatter tests =====\n");
  test_identical_composition_adopts_ids();
  test_empty_composition_applies();
  test_reordered_composition_applies();
  test_commissioned_change_warns();
  test_stray_ready_before_apply_is_still_reported();
  test_apply_timeout_reports_protocol_error_and_disarms();
  test_rejected_write_aborts_immediately();
  test_persistent_mismatch_bails_after_retry_cap();
  test_event_urc_maps_to_enum();
  test_pairing_code();
  test_commissioned_query();
  test_late_declaration_is_reported();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
