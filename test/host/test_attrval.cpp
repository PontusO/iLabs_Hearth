#include <stdio.h>
#include "ArduinoShim.h"
#include "HearthCompat.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void test_bool_roundtrip(void) {
  long out = -1;
  check("bool true flattens to 1", hearthAttrValToLong(esp_matter_bool(true), &out) && out == 1);
  check("bool false flattens to 0", hearthAttrValToLong(esp_matter_bool(false), &out) && out == 0);
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_BOOLEAN, 1);
  check("1 rebuilds as bool true", v.type == ESP_MATTER_VAL_TYPE_BOOLEAN && v.val.b);
}

static void test_unsigned_roundtrip(void) {
  long out = 0;
  check("uint8 255 flattens", hearthAttrValToLong(esp_matter_uint8(255), &out) && out == 255);
  check("uint16 65535 flattens", hearthAttrValToLong(esp_matter_uint16(65535), &out) && out == 65535);
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_UINT8, 64);
  check("64 rebuilds as uint8", v.type == ESP_MATTER_VAL_TYPE_UINT8 && v.val.u == 64);
}

static void test_signed_negative(void) {
  long out = 0;
  /* -12.34 C is the TemperatureMeasurement MeasuredValue -1234. Signedness is
   * the one thing a naive unsigned-only codec gets wrong, so it is asserted. */
  check("int16 -1234 keeps its sign",
        hearthAttrValToLong(esp_matter_int16((int16_t)-1234), &out) && out == -1234);
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_INT16, -1234);
  check("-1234 rebuilds as int16", v.type == ESP_MATTER_VAL_TYPE_INT16 && v.val.i == -1234);
}

static void test_uncarryable(void) {
  long out = 0;
  esp_matter_attr_val_t v; v.type = ESP_MATTER_VAL_TYPE_INVALID; v.val.i = 0;
  check("an uncarryable type is refused", !hearthAttrValToLong(v, &out));
}

static void test_rebuild_rejects_unknown_type(void) {
  /* A type outside the enum (reachable via an unchecked cast, e.g. a
   * corrupted or never-initialised attrVal->type) must not be handed back
   * as though it were valid: hearthAttrValFromLong's default branch forces
   * .type to ESP_MATTER_VAL_TYPE_INVALID so a caller inspecting the result
   * can tell the rebuild did not know the type. */
  esp_matter_attr_val_t v = hearthAttrValFromLong((esp_matter_val_type_t)99, 42);
  check("an out-of-enum type rebuilds as INVALID", v.type == ESP_MATTER_VAL_TYPE_INVALID);
}

int main(void) {
  printf("\n===== attribute value codec tests =====\n");
  test_bool_roundtrip();
  test_unsigned_roundtrip();
  test_signed_negative();
  test_uncarryable();
  test_rebuild_rejects_unknown_type();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
