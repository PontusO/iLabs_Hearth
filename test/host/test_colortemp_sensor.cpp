#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterColorTemperatureLight.h"
#include "MatterEndpoints/MatterTemperatureSensor.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* colorTemperature defaults to 200 here, not upstream's begin() default of
 * 370, so that a bare bringUpCT(s, light) leaves setColorTemperature(370)
 * (the brief's own write test) as a genuine change that actually issues
 * AT+MTATTR, rather than a same-value no-op that silently skips the wire. */
static void bringUpCT(MockStream &s, MatterColorTemperatureLight &light, bool initial = false, uint8_t brightness = 64, uint16_t colorTemperature = 200) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  light.begin(initial, brightness, colorTemperature);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x010C\r\nOK\r\n");
  Matter.begin();
}

static void bringUpSensor(MockStream &s, MatterTemperatureSensor &sensor, double initial = 0.00) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0302\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  check("declared as color_temperature_light", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x010C);
  check("adopted endpoint 1", light.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  check("setOnOff(true) succeeds", light.setOnOff(true));
  check("state cached", light.getOnOff() == true);
  check("operator bool agrees", (bool)light == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_toggle(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  check("toggle from off turns on", light.toggle() && light.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  light = true;
  check("operator= writes and caches", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_write(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,8,0,128,1", "OK\r\n");
  check("setBrightness writes CurrentLevel", light.setBrightness(128));
  check("brightness cached", light.getBrightness() == 128);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_max_brightness_constant(void) {
  check("MAX_BRIGHTNESS is 255", MatterColorTemperatureLight::MAX_BRIGHTNESS == 255);
}

static void test_color_temperature_write(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,768,7,370,1", "OK\r\n");
  check("setColorTemperature writes mireds", light.setColorTemperature(370));
  check("cached", light.getColorTemperature() == 370);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_temperature_bounds(void) {
  check("MIN is 100", MatterColorTemperatureLight::MIN_COLOR_TEMPERATURE == 100);
  check("MAX is 500", MatterColorTemperatureLight::MAX_COLOR_TEMPERATURE == 500);
}

static void test_onoff_failed_write_returns_false(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !light.setOnOff(true));
  check("and does not update the cache", light.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_failed_write_returns_false(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  s.expect("AT+MTATTR=1,8,0,128,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected brightness write returns false", !light.setBrightness(128));
  check("and does not update the cache", light.getBrightness() == 64);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_temperature_failed_write_returns_false(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);  // begins with bringUpCT's default colorTemperature of 200
  s.expect("AT+MTATTR=1,768,7,400,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected color temperature write returns false", !light.setColorTemperature(400));
  check("and does not update the cache", light.getColorTemperature() == 200);
  check("no unexpected commands", s.unexpected().empty());
}

/* onChange's three-argument form must fire with the current cached values
 * for whichever two parameters did not just change. */
static void test_controller_onoff_change_fires_callback(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false; uint8_t levelAtChange = 0; uint16_t tempAtChange = 0;
  light.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  light.onChange([&](bool on, uint8_t level, uint16_t temp) { changeSeen++; levelAtChange = level; tempAtChange = temp; (void)on; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange fired with all three values", changeSeen == 1 && levelAtChange == 64 && tempAtChange == 200);
  check("cached state updated", light.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_from_controller(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  int seen = 0; uint8_t level = 0; int bothSeen = 0;
  light.onChangeBrightness([&](uint8_t v) { seen++; level = v; return true; });
  light.onChange([&](bool, uint8_t, uint16_t) { bothSeen++; return true; });
  s.injectURC("+MTATTR:1,8,0,200");
  Hearth.poll();
  check("onChangeBrightness fired", seen == 1 && level == 200);
  check("onChange fired with all three values", bothSeen == 1);
  check("cached brightness updated", light.getBrightness() == 200);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_temperature_from_controller(void) {
  MockStream s; MatterColorTemperatureLight light;
  bringUpCT(s, light);
  int seen = 0; uint16_t temp = 0; int bothSeen = 0;
  light.onChangeColorTemperature([&](uint16_t v) { seen++; temp = v; return true; });
  light.onChange([&](bool, uint8_t, uint16_t) { bothSeen++; return true; });
  s.injectURC("+MTATTR:1,768,7,450");
  Hearth.poll();
  check("onChangeColorTemperature fired", seen == 1 && temp == 450);
  check("onChange fired", bothSeen == 1);
  check("cached color temperature updated", light.getColorTemperature() == 450);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * hearthDispatchAttr (Hearth.cpp) must rebuild each +MTATTR URC's value as
 * the attribute's real esp_matter_val_type_t: BOOLEAN for OnOff, UINT8 for
 * CurrentLevel, and UINT16 for ColorTemperatureMireds. Only the .type field
 * reliably distinguishes a correct rebuild from a wrong one (Task 6's
 * finding); a .val.x assertion alone can pass by memory-layout accident.
 */
class TypeCheckingCTLight : public MatterColorTemperatureLight {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenBrightnessType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenColorTempType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 6 && attribute_id == 0) {
      seenOnOffType = val->type;
    }
    if (cluster_id == 8 && attribute_id == 0) {
      seenBrightnessType = val->type;
    }
    if (cluster_id == 768 && attribute_id == 7) {
      seenColorTempType = val->type;
    }
    return MatterColorTemperatureLight::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_values(void) {
  MockStream s; TypeCheckingCTLight light;
  bringUpCT(s, light);
  s.injectURC("+MTATTR:1,6,0,1");
  s.injectURC("+MTATTR:1,8,0,200");
  s.injectURC("+MTATTR:1,768,7,450");
  Hearth.poll();
  check("on/off value type is boolean, not the generic integer", light.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("brightness value type is uint8, not the generic integer", light.seenBrightnessType == ESP_MATTER_VAL_TYPE_UINT8);
  check("color temperature value type is uint16, not the generic integer", light.seenColorTempType == ESP_MATTER_VAL_TYPE_UINT16);
  check("no unexpected commands", s.unexpected().empty());
}

/* The sensor is host-driven: the sketch pushes readings up to the fabric. */
static void test_sensor_begin_declares_and_adopts(void) {
  MockStream s; MatterTemperatureSensor sensor;
  bringUpSensor(s, sensor);
  check("declared as temperature_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0302);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_sensor_push(void) {
  MockStream s; MatterTemperatureSensor sensor;
  bringUpSensor(s, sensor);
  s.expect("AT+MTATTR=1,1026,0,2350,1", "OK\r\n");
  check("23.50 C becomes MeasuredValue 2350", sensor.setTemperature(23.50));
  check("and reads back as 23.5", sensor.getTemperature() > 23.49 && sensor.getTemperature() < 23.51);
  check("no unexpected commands", s.unexpected().empty());
}

/* Below freezing is the case an unsigned codec silently corrupts. */
static void test_sensor_negative(void) {
  MockStream s; MatterTemperatureSensor sensor;
  bringUpSensor(s, sensor);
  s.expect("AT+MTATTR=1,1026,0,-1234,1", "OK\r\n");
  check("-12.34 C is sent as -1234", sensor.setTemperature(-12.34));
  check("and reads back negative", sensor.getTemperature() < -12.33 && sensor.getTemperature() > -12.35);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_sensor_operators(void) {
  MockStream s; MatterTemperatureSensor sensor;
  bringUpSensor(s, sensor);
  s.expect("AT+MTATTR=1,1026,0,1000,1", "OK\r\n");
  sensor = 10.0;
  check("operator double reads back", (double)sensor > 9.99 && (double)sensor < 10.01);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_sensor_no_change_no_write(void) {
  MockStream s; MatterTemperatureSensor sensor;
  bringUpSensor(s, sensor, 23.50);
  check("setting the same value succeeds without a write", sensor.setTemperature(23.50));
  check("no AT traffic for a no-op write", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_sensor_failed_write_returns_false(void) {
  MockStream s; MatterTemperatureSensor sensor;
  bringUpSensor(s, sensor);
  s.expect("AT+MTATTR=1,1026,0,2350,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setTemperature(23.50));
  check("and does not update the cache", sensor.getTemperature() == 0.0);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingSensor : public MatterTemperatureSensor {
public:
  esp_matter_val_type_t seenType = ESP_MATTER_VAL_TYPE_INVALID;
  int32_t seenRaw = 0;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 1026 && attribute_id == 0) {
      seenType = val->type;
      seenRaw = val->val.i;
    }
    return MatterTemperatureSensor::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

/* The sensor is read-direction: nothing on the fabric writes to it in
 * practice, but the dispatcher must still hand it a correctly typed,
 * correctly signed value if something did. */
static void test_sensor_delivers_typed_signed_value(void) {
  MockStream s; TypeCheckingSensor sensor;
  bringUpSensor(s, sensor);
  s.injectURC("+MTATTR:1,1026,0,-500");
  Hearth.poll();
  check("value type is int16, not the generic integer", sensor.seenType == ESP_MATTER_VAL_TYPE_INT16);
  check("value keeps its sign", sensor.seenRaw == -500);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterColorTemperatureLight and MatterTemperatureSensor tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_toggle();
  test_assignment_operator();
  test_brightness_write();
  test_max_brightness_constant();
  test_color_temperature_write();
  test_color_temperature_bounds();
  test_onoff_failed_write_returns_false();
  test_brightness_failed_write_returns_false();
  test_color_temperature_failed_write_returns_false();
  test_controller_onoff_change_fires_callback();
  test_brightness_from_controller();
  test_color_temperature_from_controller();
  test_controller_change_delivers_typed_values();
  test_sensor_begin_declares_and_adopts();
  test_sensor_push();
  test_sensor_negative();
  test_sensor_operators();
  test_sensor_no_change_no_write();
  test_sensor_failed_write_returns_false();
  test_sensor_delivers_typed_signed_value();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
