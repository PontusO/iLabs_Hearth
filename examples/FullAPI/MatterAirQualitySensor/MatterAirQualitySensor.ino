/*
 * FullAPI reference: MatterAirQualitySensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. A Hearth-original class (no arduino-esp32
 * counterpart); its one attribute, AirQuality::AirQuality, is an enum8
 * with seven defined values (transcribed in the class header from
 * upstream CHIP's AirQualityEnum):
 *
 *   AirQuality_t   kUnknown=0 kGood=1 kFair=2 kModerate=3 kPoor=4
 *                  kVeryPoor=5 kExtremelyPoor=6
 *
 * NO onChange: this attribute is kView-only per the C2 adjudication
 * (see the library README's "The ten-type swoop"), so a controller can
 * never write it and no genuine controller-driven URC for it can ever
 * arrive. An onChange callback here would have nothing honest to fire
 * on but the sketch's own writes.
 *
 * NO-ECHO NOTE: writes to this attribute never echo a +MTATTR URC in
 * any mode. AirQuality is AAI-served in the firmware (design spec
 * section 3), not a plain esp_matter attribute store, so
 * setAirQuality()'s wire write gets a bare OK back with no echo line
 * at all; that OK is the real, and only, confirmation.
 *
 *   MatterAirQualitySensor()       global object below
 *   begin(AirQuality_t)            setup()
 *   setAirQuality(AirQuality_t)    menu 'q', cycles all seven values
 *   getAirQuality()                menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool airquality read air-quality <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!AQ.setAirQuality(MatterAirQualitySensor::kGood)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterAirQualitySensor AQ;

const char *kAirQualityName[7] = {
  "kUnknown", "kGood", "kFair", "kModerate", "kPoor", "kVeryPoor", "kExtremelyPoor"
};

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  AQ.begin(MatterAirQualitySensor::kUnknown);
  Matter.begin();
  Serial.println("FullAPI MatterAirQualitySensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("q=setAirQuality(cycle all 7)  s=getAirQuality  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch. This class has no
   * onChange to dispatch, but poll() still drains the link for every
   * other endpoint and command in the sketch. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'q':
        {
          uint8_t next = (static_cast<uint8_t>(AQ.getAirQuality()) + 1) % 7;
          MatterAirQualitySensor::AirQuality_t q = static_cast<MatterAirQualitySensor::AirQuality_t>(next);
          Serial.print("setAirQuality(");
          Serial.print(kAirQualityName[next]);
          Serial.print("): ");
          /* Bare OK is the whole confirmation; see the no-echo note above. */
          Serial.println(AQ.setAirQuality(q) ? "OK" : "failed");
        }
        break;
      case 's':
        Serial.print("airQuality: ");
        Serial.println(kAirQualityName[static_cast<uint8_t>(AQ.getAirQuality())]);
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
