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
  check("first endpoint adopts ID 1", light.getEndPointId() == 1);
  check("second endpoint adopts ID 2", sensor.getEndPointId() == 2);
}

/* First boot: the C6 has nothing, so the sketch's declaration is applied. */
static void test_empty_composition_applies(void) {
  MatterEndPoint::hearthClearDeclarations();
  TestEndPoint light;
  MatterEndPoint::hearthDeclare(&light, 0x0100);

  MockStream s;
  s.expect("AT+MTEP?", "OK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0100", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Hearth.begin(s);
  s.injectURC("+MTREADY");        /* the reboot the apply triggers */
  Matter.begin();

  check("full apply sequence issued", s.scriptDrained());
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
  s.expect("AT+MTEPAPPLY", "OK\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0302\r\n+MTEP:1,2,0x0100\r\nOK\r\n");
  Hearth.begin(s);
  s.injectURC("+MTREADY");
  Matter.begin();

  check("reorder triggers a re-apply", s.scriptDrained());
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
  s.expect("AT+MTEPAPPLY", "OK\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0101\r\nOK\r\n");
  Hearth.begin(s);
  s.injectURC("+MTREADY");
  Matter.begin();

  check("it applies anyway, the sketch is the declaration of intent", s.scriptDrained());
  /* The flag lives on Hearth, not on Matter: rule N2 forbids adding anything
   * to a Matter-named class, including test introspection. */
  check("and it warned first", Hearth.warnedAboutRecommission());
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
}

static void test_pairing_code(void) {
  MockStream s;
  s.expect("AT+MTCODES?", "+MTCODES:MT:Y.K9042C00KA0648G00,34970112332\r\nOK\r\n");
  Hearth.begin(s);
  check("manual code parsed", Matter.getManualPairingCode() == String("34970112332"));
}

static void test_commissioned_query(void) {
  MockStream s;
  s.expect("AT+MTFABRICS?", "+MTFABRICS:0\r\nOK\r\n");
  s.expect("AT+MTFABRICS?", "+MTFABRICS:2\r\nOK\r\n");
  Hearth.begin(s);
  check("zero fabrics is uncommissioned", !Matter.isDeviceCommissioned());
  check("two fabrics is commissioned", Matter.isDeviceCommissioned());
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
}

int main(void) {
  printf("\n===== reconcile and ArduinoMatter tests =====\n");
  test_identical_composition_adopts_ids();
  test_empty_composition_applies();
  test_reordered_composition_applies();
  test_commissioned_change_warns();
  test_event_urc_maps_to_enum();
  test_pairing_code();
  test_commissioned_query();
  test_late_declaration_is_reported();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
