/*
 * FullAPI reference: MatterRefrigerator
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Refrigerator class at all (see the class header), so this is
 * this port's own design against the firmware's wire contract
 * (AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.22), not a mirror of anything
 * upstream. The first COMPOSED appliance in this library: one fridge parent
 * endpoint (0x0070, RefrigeratorAndTemperatureControlledCabinetMode 82 +
 * RefrigeratorAlarm 87) owning two Temperature Controlled Cabinet child
 * endpoints (0x0071), which ride in the parent's Descriptor PartsList and
 * gain their own 82 cluster (Cooler conformance) from the composition.
 *
 *   CabinetFlavour_t:  NUMBER=0 (TemperatureNumber)  LEVELS=1 (TemperatureLevel)
 *
 *   MatterRefrigerator()                     global object below
 *   addCabinet(flavour)                      setup(), BEFORE Fridge.begin();
 *                                             one NUMBER (fridge compartment)
 *                                             and one LEVELS (freezer)
 *   begin()                                  setup(); declares the fridge,
 *                                             then both cabinets parented
 *                                             under it
 *   setSupportedModes(...)                   setup(), AFTER Matter.begin()
 *   onChangeMode(cb)                         setup(), verdict reads the
 *                                             policy flag, caches on allow
 *   getCurrentMode()                         menu 'm'
 *   setDoorOpenAlarm(active)                 menu 'o'/'c'
 *   setAlarmState(bit, active)               menu 'b': unsupported-bit demo,
 *                                             the firmware's +MTERR:1 comes
 *                                             back through lastError()
 *   attributeChangeCB / hearthOnForwarded-   not called by this sketch:
 *   CommandFields                             documented no-op / internal
 *                                             dispatch, see the class header
 *
 *   Owned cabinet API exercised here (MatterTemperatureControlledCabinet):
 *   fridgeCab->begin(4.0, 0.0, 10.0, 0.5)    setup(), after Fridge.begin();
 *                                             cache-only, the parent already
 *                                             declared the endpoint
 *   freezerCab->begin(levels, 3, 1)          setup(), same
 *   cab->setSupportedModes(...)              setup(), AFTER Matter.begin();
 *                                             the cabinet's OWN 82 cluster,
 *                                             independent of the parent's
 *   cab->onChangeMode(cb)                    setup(), same shape as the
 *                                             parent's
 *   cab->getCurrentMode()                    menu 'f'/'z'
 *   fridgeCab->setTemperatureSetpoint(...)   menu 't' (TemperatureNumber)
 *   freezerCab->setSelectedTemperatureLevel  menu 'l' (TemperatureLevel)
 *
 * -------------------------------------------------------------------------
 * Mode lists set AFTER Matter.begin() -- the 534c189 lesson
 * -------------------------------------------------------------------------
 * Before the reconcile the endpoint id is still 0, and every
 * setSupportedModes() in this sketch (the parent's and both cabinets')
 * refuses an unaddressable endpoint (error 2, no wire traffic at all), the
 * same refusal MatterModeSelect's own FullAPI example was fixed to respect
 * after commit 534c189 found the earlier ordering booted with an empty
 * SupportedModes list on real hardware. All three calls below run after
 * Matter.begin(), never between it and Fridge.begin(). The cabinets'
 * TEMPERATURE configuration is different: their owned begin() calls are
 * cache-only (the reconcile pushes the values), so they sit between
 * Fridge.begin() and Matter.begin() where the first reconcile picks them up.
 * -------------------------------------------------------------------------
 *
 * -------------------------------------------------------------------------
 * CurrentMode has no ember-level signal on cluster 82 at all
 * -------------------------------------------------------------------------
 * RefrigeratorAndTemperatureControlledCabinetMode's CurrentMode is
 * AttributeAccessInterface-served, never ember-backed (AT_MT_SPEC.md
 * S3.20.1): no +MTATTR URC is ever raised for it, from the firmware's own
 * clamp or a controller-driven change. getCurrentMode() -- the parent's and
 * the cabinets' alike -- is therefore this host's own bookkeeping, updated
 * only when the matching onChangeMode() itself returns true for a forwarded
 * ChangeToMode; there is no other signal to watch. B196: a same-mode
 * ChangeToMode short-circuits firmware-side and never reaches this host.
 * -------------------------------------------------------------------------
 *
 * The door alarm rides AT+MTALARM (S3.22), not AT+MTATTR: writing State
 * through that command is what makes the cluster's Notify event actually
 * fire, so a controller subscribed to the event sees every 'o'/'c' below.
 * Mask/State/Supported READS are ordinary ember attributes; chip-tool reads
 * them directly, which is why this class has no read wrappers.
 *
 * The same 1000 ms verdict window and Hearth.poll()-every-iteration
 * requirement documented on the door lock apply to the three onChangeMode()
 * callbacks here identically; see examples/FullAPI/MatterDoorLock for the
 * full explanation, not repeated here.
 *
 * Observe controller-side (endpoints: 1 fridge, 2 fridge compartment,
 * 3 freezer, on a fresh composition):
 *   chip-tool descriptor read parts-list <node> 1
 *   chip-tool refrigeratorandtemperaturecontrolledcabinetmode read supported-modes <node> 1
 *   chip-tool refrigeratorandtemperaturecontrolledcabinetmode change-to-mode 1 <node> 1
 *   chip-tool refrigeratorandtemperaturecontrolledcabinetmode read current-mode <node> 1
 *   chip-tool refrigeratorandtemperaturecontrolledcabinetmode change-to-mode 1 <node> 2   (the cabinet's own cluster)
 *   chip-tool refrigeratoralarm read state <node> 1
 *   chip-tool refrigeratoralarm read supported <node> 1
 *   chip-tool refrigeratoralarm read-event notify <node> 1
 *   chip-tool temperaturecontrol read temperature-setpoint <node> 2
 *   chip-tool temperaturecontrol read selected-temperature-level <node> 3
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Fridge.setDoorOpenAlarm(true)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterRefrigerator Fridge;

// The owned cabinet references addCabinet() hands back. File-scope pointers
// so loop()'s menu can reach them; assigned once in setup().
MatterTemperatureControlledCabinet *fridgeCab = nullptr;
MatterTemperatureControlledCabinet *freezerCab = nullptr;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny. Every
// verdict callback below reads this same flag; a real sketch would consult
// its own control loop (can the compressor actually deliver the requested
// mode right now) here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Cabinets are added BEFORE Fridge.begin(): begin() is what declares
  // them, parented under the fridge, and addCabinet() after it returns an
  // inert reject cabinet whose own begin() fails.
  fridgeCab = &Fridge.addCabinet(MatterRefrigerator::NUMBER);
  freezerCab = &Fridge.addCabinet(MatterRefrigerator::LEVELS);

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and must
  // not touch the wire itself; see MatterDoorLock's own header comment for
  // the full verdict-window explanation.
  Fridge.onChangeMode([](uint8_t mode) {
    bool allow = (policy == 'a');
    Serial.print("fridge onChangeMode(");
    Serial.print(mode);
    Serial.print(") verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  fridgeCab->onChangeMode([](uint8_t mode) {
    bool allow = (policy == 'a');
    Serial.print("fridge-compartment cabinet onChangeMode(");
    Serial.print(mode);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  freezerCab->onChangeMode([](uint8_t mode) {
    bool allow = (policy == 'a');
    Serial.print("freezer cabinet onChangeMode(");
    Serial.print(mode);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });

  // A HEARTH_CMD_TIMEOUT means a verdict window closed with the firmware
  // never having heard back from this host: worth logging, since it is the
  // diagnostic for the poll-latency trap described on the door lock.
  Hearth.onLinkEvent([](hearthEvent_t e) {
    if (e == HEARTH_CMD_TIMEOUT) {
      Serial.println("HEARTH_CMD_TIMEOUT: a verdict window closed unanswered (denied by default).");
    }
  });

  // Declares the fridge, then both cabinets parented under it (registry
  // order becomes endpoint order: fridge, fridge compartment, freezer).
  Serial.println(Fridge.begin() ? "Fridge.begin(): OK" : "Fridge.begin(): failed");

  // Owned cabinet begin(): cache-only (the parent already declared the
  // endpoints), so these sit BEFORE Matter.begin() and the first reconcile
  // pushes the values. The overload must match the flavour addCabinet()
  // declared: TemperatureNumber for the fridge compartment (4 C setpoint,
  // 0..10 C, 0.5 C step), TemperatureLevel for the freezer.
  Serial.println(fridgeCab->begin(4.0, 0.0, 10.0, 0.5) ? "fridgeCab->begin(): OK" : "fridgeCab->begin(): failed");
  static uint8_t freezerLevels[3] = { 0, 1, 2 };
  Serial.println(freezerCab->begin(freezerLevels, 3, 1) ? "freezerCab->begin(): OK" : "freezerCab->begin(): failed");

  Matter.begin();

  // Mode lists set AFTER Matter.begin(); see the header comment above.
  // Tag 0 asks the firmware for this cluster's own conformance-required
  // default (kAuto on every mode for cluster 82, AT_MT_SPEC.md S3.20.1's
  // table); this sketch has no reason to care about explicit tags, so
  // every triple below uses 0. The parent's list and each cabinet's list
  // are independent stores (keyed by (ep, cluster)), hence three calls.
  static const uint8_t kModes[2] = { 0, 1 };
  static const uint16_t kTags[2] = { 0, 0 };
  static const char *kFridgeLabels[2] = { "Auto", "Energy Saver" };
  Serial.println(Fridge.setSupportedModes(kModes, kTags, kFridgeLabels, 2) ? "Fridge.setSupportedModes: OK" : "Fridge.setSupportedModes: failed");

  static const char *kFridgeCabLabels[2] = { "Auto", "Rapid Cool" };
  Serial.println(
    fridgeCab->setSupportedModes(kModes, kTags, kFridgeCabLabels, 2) ? "fridgeCab->setSupportedModes: OK" : "fridgeCab->setSupportedModes: failed"
  );

  static const char *kFreezerLabels[2] = { "Auto", "Deep Freeze" };
  Serial.println(
    freezerCab->setSupportedModes(kModes, kTags, kFreezerLabels, 2) ? "freezerCab->setSupportedModes: OK" : "freezerCab->setSupportedModes: failed"
  );

  Serial.println("FullAPI MatterRefrigerator ready; '?' for menu");
}

void printHelp() {
  Serial.println("o=setDoorOpenAlarm(true) c=setDoorOpenAlarm(false)");
  Serial.println("b=setAlarmState(3,true) rejected-write demo (bit 3 not in Supported)");
  Serial.println("m=fridge getCurrentMode f=fridge-compartment getCurrentMode z=freezer getCurrentMode");
  Serial.println("t=fridgeCab setTemperatureSetpoint(6.0) l=freezerCab setSelectedTemperatureLevel(2)");
  Serial.println("a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets the three
   * onChangeMode() verdicts run inside the 1000 ms window at all. See the
   * header comment above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'o': Serial.println(Fridge.setDoorOpenAlarm(true) ? "setDoorOpenAlarm(true): OK (Notify fired)" : "setDoorOpenAlarm(true): failed"); break;
      case 'c': Serial.println(Fridge.setDoorOpenAlarm(false) ? "setDoorOpenAlarm(false): OK (Notify fired)" : "setDoorOpenAlarm(false): failed"); break;
      case 'b':
        if (!Fridge.setAlarmState(3, true)) {
          Serial.print("setAlarmState(3,true): failed as expected, +MTERR:");
          Serial.println(Hearth.lastError());
        } else {
          Serial.println("setAlarmState(3,true): OK (unexpected: bit 3 is not in Supported)");
        }
        break;
      case 'm': Serial.print("fridge currentMode: "); Serial.println(Fridge.getCurrentMode()); break;
      case 'f': Serial.print("fridge-compartment currentMode: "); Serial.println(fridgeCab->getCurrentMode()); break;
      case 'z': Serial.print("freezer currentMode: "); Serial.println(freezerCab->getCurrentMode()); break;
      case 't':
        Serial.println(fridgeCab->setTemperatureSetpoint(6.0) ? "fridgeCab setTemperatureSetpoint(6.0): OK" : "fridgeCab setTemperatureSetpoint(6.0): failed");
        break;
      case 'l':
        Serial.println(
          freezerCab->setSelectedTemperatureLevel(2) ? "freezerCab setSelectedTemperatureLevel(2): OK" : "freezerCab setSelectedTemperatureLevel(2): failed"
        );
        break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
      default:  break;
    }
  }
  delay(10);
}
