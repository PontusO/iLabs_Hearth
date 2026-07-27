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

static void test_write_modes(void) {
  MockStream s;
  s.expect("AT+MTATTR=1,6,0,1,0", "OK\r\n");
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  Hearth.begin(s);
  TestEndPoint ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v = esp_matter_bool(true);
  check("setAttributeVal writes mode 0", ep.setAttributeVal(0x0006, 0x0000, &v));
  check("updateAttributeVal writes mode 1", ep.updateAttributeVal(0x0006, 0x0000, &v));
  check("script drained", s.scriptDrained());
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
  test_declaration_registry_full();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
