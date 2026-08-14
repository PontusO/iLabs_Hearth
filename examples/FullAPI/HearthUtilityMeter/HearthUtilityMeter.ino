/*
 * FullAPI reference: MatterElectricalUtilityMeter
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * meter-identity class at all (see the class header), so this is this
 * port's own design against the firmware's AT+MTMETERID wire contract
 * (energy round C2, main/mt_meter.cpp / main/mt_at.c's cmd_mtmeterid;
 * docs/AT_MT_SPEC.md does not describe it yet, Task 14 owns that).
 *
 *   MatterElectricalUtilityMeter() global object below
 *   begin()                setup(), before Matter.begin(); declares only,
 *                          no wire traffic (device type 0x0511 has no
 *                          variant scheme, see the class header)
 *   setPowerThreshold(...) setup() after Matter.begin(), FIRST of the five
 *                          identity setters: every other setter refuses
 *                          host-side until a power threshold has been set
 *                          at least once (the choice-b rule, see the class
 *                          header)
 *   setMeterType(type)     setup()
 *   setPointOfDelivery(s)  setup()
 *   setSerialNumber(s)     setup()
 *   setProtocolVersion(s)  setup()
 *   attributeChangeCB(...) not called directly by a sketch: wired by the
 *                          library, a documented no-op for this class
 *                          (cluster 0x0B06 is Instance-served)
 *   end()                  not called: tearing the endpoint down mid-demo
 *                          is not a usable demo
 *
 * THERE IS NO READ-BACK: the class exposes no getters either (see the class
 * header). The sketch below keeps its own copy of what it pushed purely for
 * the status menu, the same discipline MatterEvse's FullAPI example uses
 * for its own Instance-served scalars.
 *
 * Configuration goes AFTER Matter.begin(): the endpoint id is adopted
 * there (the seven-type batch lesson, repeated in every FullAPI example
 * since).
 *
 * Every field pushed here is CONFIGURATION (an installation/product fact),
 * so a C6 reboot re-syncs it automatically: hearthOnReconciled() re-sends
 * the whole cached identity unconditionally on every reconcile, the B229
 * pattern (see the class header). Nothing in this sketch needs to notice a
 * reboot itself.
 *
 * Observe controller-side (device type 0x0511, electrical_utility_meter):
 *   chip-tool meteridentification read meter-type <node> <ep>
 *   chip-tool meteridentification read point-of-delivery <node> <ep>
 *   chip-tool meteridentification read meter-serial-number <node> <ep>
 *   chip-tool meteridentification read protocol-version <node> <ep>
 *   chip-tool meteridentification read power-threshold <node> <ep>
 */
#include <Matter.h>

MatterElectricalUtilityMeter UtilityMeter;

// Local tracking only: this class exposes no getters (there is no wire
// read-back, see the class header), so the sketch remembers what it last
// pushed, purely for the status menu.
uint8_t lastMeterType = 0;          // 0 = Utility
char lastPod[65] = "Main Panel, Unit 4B";
char lastSerial[65] = "SN-000123";
char lastProtocol[65] = "IEC62056-21 v1.0";
int64_t lastPwr = 7200000;  // 7.2 kW, milliwatts

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  UtilityMeter.begin();  // declares only; no wire traffic

  Matter.begin();

  // Endpoint-addressed configuration goes AFTER Matter.begin() (the
  // endpoint id is adopted there). setPowerThreshold() MUST run first:
  // every other setter refuses host-side (Hearth error 1, zero wire
  // traffic) until a power threshold has been set at least once.
  Serial.println(
    UtilityMeter.setPowerThreshold(true, lastPwr, false, 0, false, 0) ? "setPowerThreshold: OK" : "setPowerThreshold: failed"
  );
  Serial.println(UtilityMeter.setMeterType(lastMeterType) ? "setMeterType: OK" : "setMeterType: failed");
  // A comma inside the quoted string is legal content and survives
  // unescaped on the wire (mtmeterid_scan_string(), mt_at.c).
  Serial.println(UtilityMeter.setPointOfDelivery(lastPod) ? "setPointOfDelivery: OK" : "setPointOfDelivery: failed");
  Serial.println(UtilityMeter.setSerialNumber(lastSerial) ? "setSerialNumber: OK" : "setSerialNumber: failed");
  Serial.println(UtilityMeter.setProtocolVersion(lastProtocol) ? "setProtocolVersion: OK" : "setProtocolVersion: failed");

  Serial.println("FullAPI MatterElectricalUtilityMeter ready; '?' for menu");
}

void printHelp() {
  Serial.println("g=setMeterType(Generic)  p=setPowerThreshold(9000000mW)  s=status  ?=help");
}

void statusDump() {
  Serial.print("meterType ");
  Serial.print(lastMeterType);
  Serial.print(", pod \"");
  Serial.print(lastPod);
  Serial.print("\", serial \"");
  Serial.print(lastSerial);
  Serial.print("\", protocol \"");
  Serial.print(lastProtocol);
  Serial.print("\", powerThreshold(mW) ");
  Serial.println((long)lastPwr);
}

void loop() {
  // URC dispatch lives here even for a configuration-only class like this
  // one: poll unconditionally, exactly as every other FullAPI example
  // does.
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'g':
        lastMeterType = 2;  // Generic
        Serial.println(UtilityMeter.setMeterType(lastMeterType) ? "setMeterType(Generic): OK" : "setMeterType: failed");
        break;
      case 'p':
        lastPwr = 9000000;
        Serial.println(
          UtilityMeter.setPowerThreshold(true, lastPwr, false, 0, false, 0) ? "setPowerThreshold(9000000mW): OK"
                                                                             : "setPowerThreshold: failed"
        );
        break;
      case 's': statusDump(); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
