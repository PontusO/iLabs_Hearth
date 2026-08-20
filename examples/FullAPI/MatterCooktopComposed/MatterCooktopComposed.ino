/*
 * FullAPI reference: composed MatterCooktop + MatterCookSurface
 *
 * Demonstrates the composed cooktop (Task 10, composed-appliance round):
 * a 0x0078 parent owning two Cook Surface children (0x0077), one
 * TemperatureNumber and one TemperatureLevel, the whole surface-facing
 * public API on a serial menu. The zero-surface MatterCooktop API is
 * demonstrated by examples/FullAPI/MatterCooktop, unchanged; this sketch
 * covers what composition adds. Hearth-original classes: arduino-esp32's
 * Matter library ships neither (see the class headers), so this is this
 * port's own design against the firmware's wire contract (AT_MT_SPEC.md
 * S3.9's 0x0077 note).
 *
 *   CabinetFlavour_t:  NUMBER=0 (TemperatureNumber)  LEVELS=1 (TemperatureLevel)
 *
 *   MatterCooktop()                          global object below
 *   addSurface(flavour)                      setup(), BEFORE Cooktop.begin();
 *                                             one NUMBER and one LEVELS
 *   begin()                                  setup(); declares the cooktop,
 *                                             then both surfaces parented
 *                                             under it
 *   off(), getOnOff(), onChange...           the cooktop's own OffOnly API,
 *                                             see examples/FullAPI/
 *                                             MatterCooktop; menu 'O'/'S'
 *                                             here for the parent endpoint
 *
 *   MatterCookSurface (the owned references addSurface() hands back):
 *   front->begin(90.0, 30.0, 250.0, 5.0)     setup(), after Cooktop.begin();
 *                                             cache-only, the parent already
 *                                             declared the endpoint
 *   rear->begin(levels, 4, 0)                setup(), the LEVELS shape
 *   front->onOffChange(cb)                   setup(), prints the new state
 *   front->setOnOff(true/false)              menu '1'/'0' (front),
 *   rear->setOnOff(true/false)               menu '4'/'3' (rear)
 *   front->getOnOff()                        menu 's'
 *   front->setTemperatureSetpoint(...)       menu 't' (TemperatureNumber)
 *   front->getTemperatureSetpoint()          menu 's'
 *   rear->setSelectedTemperatureLevel(...)   menu 'l' (TemperatureLevel)
 *   rear->getSelectedTemperatureLevel()      menu 's'
 *   rear->setSupportedTemperatureLevelLabels setup(), AFTER Matter.begin()
 *   (the whole inherited cabinet surface:    see examples/FullAPI/
 *    min/max/step setters and getters,        MatterTemperatureControlled-
 *    setSupportedTemperatureLevels)           Cabinet, not repeated here
 *
 * -------------------------------------------------------------------------
 * OffOnly cuts the other way on a surface
 * -------------------------------------------------------------------------
 * The surface's OnOff cluster carries the OffOnly feature: Off is the only
 * command a controller can invoke, so the only remote-deliverable change is
 * OnOff=false, arriving as a normal +MTATTR URC (watch onOffChange print it
 * after a chip-tool `onoff off`). Turning a burner ON is always THIS
 * sketch's act, setOnOff(true), which is a plain AT+MTATTR write the fabric
 * observes. That is the deliberate asymmetry against the parent cooktop
 * class, which has no wire path to true at all. A chip-tool `onoff on`
 * against a surface answers UNSUPPORTED_COMMAND (0x81): the command does
 * not exist on the cluster.
 * -------------------------------------------------------------------------
 *
 * Level labels set AFTER Matter.begin(), the 534c189 lesson: before the
 * reconcile the endpoint id is still 0 and the setter refuses an
 * unaddressable endpoint (error 2, no wire traffic). The surfaces'
 * TEMPERATURE configuration is different: their owned begin() is cache-only
 * (the reconcile pushes the values), so it sits between Cooktop.begin() and
 * Matter.begin() where the first reconcile picks it up.
 *
 * Observe controller-side (endpoints: 1 cooktop, 2 front, 3 rear, on a
 * fresh composition):
 *   chip-tool descriptor read parts-list <node> 1
 *   chip-tool onoff read on-off <node> 2
 *   chip-tool onoff off <node> 2
 *   chip-tool onoff on <node> 2       (UNSUPPORTED_COMMAND 0x81 by design)
 *   chip-tool temperaturecontrol read temperature-setpoint <node> 2
 *   chip-tool temperaturecontrol read selected-temperature-level <node> 3
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!front->setOnOff(true)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterCooktop Cooktop;

// The owned surface references addSurface() hands back. File-scope
// pointers so loop()'s menu can reach them; assigned once in setup().
MatterCookSurface *front = nullptr;
MatterCookSurface *rear = nullptr;

// The rear surface's level identifiers: 0 Off, 1 Low, 2 Medium, 3 High.
uint8_t rearLevels[4] = { 0, 1, 2, 3 };

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Surfaces are added BEFORE Cooktop.begin(): begin() is what declares
  // them, parented under the cooktop, and addSurface() after it returns an
  // inert reject surface whose own begin() fails.
  front = &Cooktop.addSurface(MatterCookSurface::NUMBER);
  rear = &Cooktop.addSurface(MatterCookSurface::LEVELS);

  // Fires on every OnOff URC for that surface: a controller's Off (the
  // only value a remote can deliver, see the banner) and the firmware's
  // echo of this sketch's own setOnOff() writes.
  front->onOffChange([](bool state) {
    Serial.print("front onOffChange: ");
    Serial.println(state);
  });
  rear->onOffChange([](bool state) {
    Serial.print("rear onOffChange: ");
    Serial.println(state);
  });

  // Declares the cooktop, then both surfaces parented under it (registry
  // order becomes endpoint order: cooktop 1, front 2, rear 3).
  Serial.println(Cooktop.begin() ? "Cooktop.begin(): OK" : "Cooktop.begin(): failed");

  // Owned surface begin(): cache-only (the parent already declared the
  // endpoints), so it sits BEFORE Matter.begin() and the first reconcile
  // pushes the values. Each overload must match the flavour addSurface()
  // declared: front TemperatureNumber (90 C setpoint, 30..250 C, 5 C
  // step), rear TemperatureLevel (identifiers above, boot at 0).
  Serial.println(front->begin(90.0, 30.0, 250.0, 5.0) ? "front->begin(): OK" : "front->begin(): failed");
  Serial.println(rear->begin(rearLevels, 4, 0) ? "rear->begin(): OK" : "rear->begin(): failed");

  Matter.begin();

  // Level labels AFTER Matter.begin(); see the banner. Replaces the
  // generated "Level <n>" defaults with display text a controller UI shows.
  static const char *kRearLabels[4] = { "Off", "Low", "Medium", "High" };
  Serial.println(
    rear->setSupportedTemperatureLevelLabels(kRearLabels, 4) ? "rear labels: OK" : "rear labels: failed"
  );

  Serial.println("FullAPI MatterCooktopComposed ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=front setOnOff(true) 0=front setOnOff(false) 4=rear setOnOff(true) 3=rear setOnOff(false)");
  Serial.println("t=front setTemperatureSetpoint(180.0) l=rear setSelectedTemperatureLevel(2)");
  Serial.println("O=cooktop off() S=cooktop getOnOff s=surface state ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onOffChange callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(front->setOnOff(true) ? "front on: OK" : "front on: failed"); break;
      case '0': Serial.println(front->setOnOff(false) ? "front off: OK" : "front off: failed"); break;
      case '4': Serial.println(rear->setOnOff(true) ? "rear on: OK" : "rear on: failed"); break;
      case '3': Serial.println(rear->setOnOff(false) ? "rear off: OK" : "rear off: failed"); break;
      case 't':
        Serial.println(front->setTemperatureSetpoint(180.0) ? "front setpoint 180.0: OK" : "front setpoint: failed");
        break;
      case 'l':
        Serial.println(rear->setSelectedTemperatureLevel(2) ? "rear level 2: OK" : "rear level: failed");
        break;
      case 'O': Serial.println(Cooktop.off() ? "cooktop off: OK" : "cooktop off: failed"); break;
      case 'S': Serial.print("cooktop onOff: "); Serial.println(Cooktop.getOnOff()); break;
      case 's':
        Serial.print("front onOff: ");
        Serial.print(front->getOnOff());
        Serial.print(" setpoint: ");
        Serial.print(front->getTemperatureSetpoint());
        Serial.print(" | rear onOff: ");
        Serial.print(rear->getOnOff());
        Serial.print(" level: ");
        Serial.println(rear->getSelectedTemperatureLevel());
        break;
      case '?': printHelp(); break;
      default:  break;
    }
  }
  delay(10);
}
