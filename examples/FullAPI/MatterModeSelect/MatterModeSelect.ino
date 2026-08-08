/*
 * FullAPI reference: MatterModeSelect
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Mode Select class or example at all (see the class header), so
 * this is this port's own design against the firmware's C3 wire contract,
 * not a mirror of anything upstream.
 *
 *   MatterModeSelect()                              global object below
 *   begin()                                         setup(), no initial
 *                                                    CurrentMode/
 *                                                    SupportedModes to
 *                                                    reconcile
 *   setSupportedModes(modes, labels, count)          setup() and menu 'v'
 *   setCurrentMode(m)                                menu 'm', cycles the
 *                                                     three installed modes
 *   getCurrentMode()                                 menu 'g'
 *   onChangeMode(cb)                                 setup(), prints the
 *                                                     new mode
 *   end()                       not called: tearing the endpoint down
 *                                mid-demo is not a usable demo; call it
 *                                when retiring the endpoint
 *
 * SupportedModes has no AT+MTATTR path at all: it is served by CHIP's own
 * SupportedModesManager, so AT+MTMODES (setSupportedModes()) is the only
 * way this host can set it, and there is no read-back command either --
 * this sketch keeps its own copy of what it last installed, the same
 * discipline MatterTemperatureControlledCabinet's TemperatureLevel labels
 * already established. Not persisted by the firmware: the cached list is
 * re-sent verbatim on every later Matter.begin() reconcile, which is why
 * setSupportedModes() is called once from setup() and need not be called
 * again unless the sketch wants to CHANGE the installed set (menu 'v'
 * demonstrates a replacement, not a resend).
 *
 * CurrentMode is the opposite: a plain esp_matter-managed attribute,
 * readable and writable over AT+MTATTR like any other integer.
 * setCurrentMode()/getCurrentMode() go through the base class the same
 * shape MatterOnOffLight::setOnOff() does. A controller's ChangeToMode
 * command sets CurrentMode itself, inside the SDK, after validating the
 * mode is in SupportedModes; this host sees that the ordinary way, a
 * generic +MTATTR URC, which is also what feeds onChangeMode() below --
 * there is no dedicated +MTCMD verdict for this class at all, because
 * ChangeToMode never needs an app-level adjudication.
 *
 * setSupportedModes() enforces the wire grammar host-side (1..8 pairs,
 * mode values unique within the list, labels 1..32 bytes of printable
 * ASCII with no '"'), the same discipline
 * MatterTemperatureControlledCabinet::setSupportedTemperatureLevelLabels()
 * established: a comma INSIDE a label is legal and part of its text (see
 * the "Warm, ish" demo below), only an unescaped '"' is rejected, since it
 * would corrupt the AT+MTMODES line's own field boundary.
 *
 * Observe controller-side:
 *   chip-tool modeselect read supported-modes <node> <ep>
 *   chip-tool modeselect read current-mode <node> <ep>
 *   chip-tool modeselect change-to-mode 1 <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Modes.setCurrentMode(1)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterModeSelect Modes;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Fired from attributeChangeCB() when a controller's ChangeToMode
  // command (handled entirely inside the SDK) changes CurrentMode.
  Modes.onChangeMode([](uint8_t mode) {
    Serial.print("onChangeMode: ");
    Serial.println(mode);
  });

  Modes.begin();
  Matter.begin();

  // Three mode/label pairs, one label containing a comma to exercise the
  // AT+MTMODES quoting path (a comma inside a quoted label is legal and
  // part of its text, per the header comment). Called AFTER Matter.begin():
  // before the reconcile the endpoint id is still 0, and setSupportedModes()
  // refuses an unaddressable endpoint (error 2, no wire traffic at all), the
  // same post-reconcile placement MatterTemperatureControlledCabinet's
  // FullAPI example uses for setSupportedTemperatureLevelLabels().
  static const uint8_t kModes[3] = { 0, 1, 2 };
  static const char *kLabels[3] = { "Quiet", "Normal, standard", "Boost" };
  Serial.println(Modes.setSupportedModes(kModes, kLabels, 3) ? "setSupportedModes: OK" : "setSupportedModes: failed");

  Serial.println("FullAPI MatterModeSelect ready; '?' for menu");
}

void printHelp() {
  Serial.println("m=setCurrentMode(cycle 0/1/2)  g=getCurrentMode");
  Serial.println("v=setSupportedModes(replace with a new pair set)  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets onChangeMode()
   * dispatch from the generic +MTATTR URC path. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'm':
        {
          uint8_t next = (Modes.getCurrentMode() + 1) % 3;
          Serial.print("setCurrentMode(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Modes.setCurrentMode(next) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("currentMode: "); Serial.println(Modes.getCurrentMode()); break;
      case 'v':
        {
          static const uint8_t kNewModes[2] = { 5, 9 };
          static const char *kNewLabels[2] = { "Eco", "Turbo" };
          Serial.print("setSupportedModes(replace, 2 entries): ");
          Serial.println(Modes.setSupportedModes(kNewModes, kNewLabels, 2) ? "OK" : "failed");
        }
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
