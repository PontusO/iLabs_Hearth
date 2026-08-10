/*
 * FullAPI reference: MatterOven + MatterOvenCavity
 *
 * Demonstrates the complete public API of both classes. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. Hearth-original classes: arduino-esp32's Matter library
 * ships no Oven class at all (see the class headers), so this is this
 * port's own design against the firmware's wire contract (docs/AT_MT_SPEC.md
 * S3.9/S3.17/S3.20.1/S3.21), not a mirror of anything upstream. The second
 * COMPOSED appliance in this library, and the first with a TYPED child: the
 * oven parent endpoint (0x007B) is BARE by design (Descriptor + Identify,
 * S3.9's 0x007B note); every function lives on its cavity, a Temperature
 * Controlled Cabinet child endpoint (0x0071) that rides in the parent's
 * Descriptor PartsList and gains the Heater conditional cluster set from
 * the composition: OvenMode (0x49) and OvenCavityOperationalState (0x48).
 *
 *   CabinetFlavour_t:  NUMBER=0 (TemperatureNumber)  LEVELS=1 (TemperatureLevel)
 *
 *   MatterOven()                             global object below
 *   addCavity(flavour)                       setup(), BEFORE Oven.begin();
 *                                             one NUMBER cavity (this demo)
 *   begin()                                  setup(); declares the oven,
 *                                             then the cavity parented
 *                                             under it
 *   attributeChangeCB                        not called by this sketch:
 *                                             documented no-op, bare parent
 *
 *   MatterOvenCavity (the owned reference addCavity() hands back):
 *   cavity->begin(180.0, 30.0, 300.0, 5.0)  setup(), after Oven.begin();
 *                                             cache-only, the parent already
 *                                             declared the endpoint
 *   cavity->setSupportedModes(...)           setup(), AFTER Matter.begin()
 *   cavity->onChangeMode(cb)                 setup(), verdict reads the
 *                                             policy flag, caches on allow
 *   cavity->getCurrentMode()                 menu 'm'
 *   cavity->onStop(cb)                       setup(), verdict reads the
 *                                             policy flag
 *   cavity->onStart(cb)                      setup(), same
 *   cavity->setOperationalState(state)       menu '1'/'2'/'0' (plain 0/1/2)
 *   cavity->getOperationalState()            menu 's'
 *   cavity->setTemperatureSetpoint(...)      menu 't' (TemperatureNumber)
 *   hearthOnForwardedCommandFields           not called by this sketch:
 *                                             internal dispatch, class header
 *
 * There are deliberately NO onPause/onResume members on the cavity:
 * OvenCavityOperationalState marks Pause and Resume disallowConform
 * (OperationalState_Oven.xml revision 2), the firmware never registers or
 * forwards them, and the typed child means the compiler enforces that
 * surface: a sketch that tries cavity->onPause(...) does not build. A
 * controller that invokes pause anyway is answered UNSUPPORTED_COMMAND
 * (0x81) by the device itself; see the observe list below.
 *
 * -------------------------------------------------------------------------
 * Mode lists set AFTER Matter.begin() -- the 534c189 lesson
 * -------------------------------------------------------------------------
 * Before the reconcile the endpoint id is still 0 and setSupportedModes()
 * refuses an unaddressable endpoint (error 2, no wire traffic), the same
 * refusal MatterModeSelect's own FullAPI example was fixed to respect after
 * commit 534c189 found the earlier ordering booted with an empty
 * SupportedModes list on real hardware. The cavity's TEMPERATURE
 * configuration is different: its owned begin() is cache-only (the
 * reconcile pushes the values), so it sits between Oven.begin() and
 * Matter.begin() where the first reconcile picks it up.
 * -------------------------------------------------------------------------
 *
 * -------------------------------------------------------------------------
 * Neither CurrentMode nor OperationalState has an ember-level signal
 * -------------------------------------------------------------------------
 * OvenMode's CurrentMode and every OvenCavityOperationalState attribute are
 * Instance-served, never ember-backed (AT_MT_SPEC.md S3.20.1/S3.21): no
 * +MTATTR URC is ever raised for them. getCurrentMode() is therefore this
 * host's own bookkeeping, updated only when onChangeMode() itself returns
 * true for a forwarded ChangeToMode (B196: a same-mode request
 * short-circuits firmware-side and never reaches this host), and
 * getOperationalState() tracks this sketch's own successful
 * setOperationalState() calls. Tag 0 in a mode triple asks the firmware for
 * OvenMode's own conformance default, kBake on every mode (S3.20.1's tag
 * table).
 * -------------------------------------------------------------------------
 *
 * The onStop()/onStart() verdict IS the wire response the controller
 * observes (a deny answers ErrorStateID 0x02, UnableToCompleteOperation),
 * the same shape as the OperationalState trio. The same 1000 ms verdict
 * window and Hearth.poll()-every-iteration requirement documented on the
 * door lock apply to all three callbacks here identically; see
 * examples/FullAPI/MatterDoorLock for the full explanation, not repeated
 * here.
 *
 * Observe controller-side (endpoints: 1 oven, 2 cavity, on a fresh
 * composition):
 *   chip-tool descriptor read parts-list <node> 1
 *   chip-tool ovenmode read supported-modes <node> 2
 *   chip-tool ovenmode change-to-mode 1 <node> 2
 *   chip-tool ovenmode read current-mode <node> 2
 *   chip-tool ovencavityoperationalstate read operational-state <node> 2
 *   chip-tool ovencavityoperationalstate stop <node> 2
 *   chip-tool ovencavityoperationalstate start <node> 2
 *   chip-tool ovencavityoperationalstate pause <node> 2   (answers
 *     UNSUPPORTED_COMMAND 0x81 by design: the cluster disallows Pause and
 *     the firmware never registers it; resume behaves the same)
 *   chip-tool temperaturecontrol read temperature-setpoint <node> 2
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!cavity->setOperationalState(1)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterOven Oven;

// The owned cavity reference addCavity() hands back. A file-scope pointer
// so loop()'s menu can reach it; assigned once in setup().
MatterOvenCavity *cavity = nullptr;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny. All
// three verdict callbacks below read this same flag; a real sketch would
// consult its own control loop (is the heater actually safe to start
// right now) here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // The cavity is added BEFORE Oven.begin(): begin() is what declares it,
  // parented under the oven, and addCavity() after it returns an inert
  // reject cavity whose own begin() fails.
  cavity = &Oven.addCavity(MatterOvenCavity::NUMBER);

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and must
  // not touch the wire itself; see MatterDoorLock's own header comment for
  // the full verdict-window explanation.
  cavity->onChangeMode([](uint8_t mode) {
    bool allow = (policy == 'a');
    Serial.print("cavity onChangeMode(");
    Serial.print(mode);
    Serial.print(") verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  cavity->onStop([]() {
    bool allow = (policy == 'a');
    Serial.print("onStop verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  cavity->onStart([]() {
    bool allow = (policy == 'a');
    Serial.print("onStart verdict (policy=");
    Serial.print(policy);
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

  // Declares the oven, then the cavity parented under it (registry order
  // becomes endpoint order: oven 1, cavity 2).
  Serial.println(Oven.begin() ? "Oven.begin(): OK" : "Oven.begin(): failed");

  // Owned cavity begin(): cache-only (the parent already declared the
  // endpoint), so it sits BEFORE Matter.begin() and the first reconcile
  // pushes the values. TemperatureNumber, matching the flavour addCavity()
  // declared: 180 C setpoint, 30..300 C, 5 C step.
  Serial.println(cavity->begin(180.0, 30.0, 300.0, 5.0) ? "cavity->begin(): OK" : "cavity->begin(): failed");

  Matter.begin();

  // Mode list set AFTER Matter.begin(); see the header comment above.
  // Tag 0 asks the firmware for this cluster's own conformance-required
  // default (kBake on every mode for OvenMode, AT_MT_SPEC.md S3.20.1's
  // table); this sketch has no reason to care about explicit tags, so both
  // triples below use 0.
  static const uint8_t kModes[2] = { 0, 1 };
  static const uint16_t kTags[2] = { 0, 0 };
  static const char *kLabels[2] = { "Bake", "Roast" };
  Serial.println(
    cavity->setSupportedModes(kModes, kTags, kLabels, 2) ? "cavity->setSupportedModes: OK" : "cavity->setSupportedModes: failed"
  );

  Serial.println("FullAPI MatterOven ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=setOperationalState(1,Running) 2=setOperationalState(2,Paused) 0=setOperationalState(0,Stopped)");
  Serial.println("e=setOperationalState(3) rejected-write demo (host-side, error 1, no wire traffic)");
  Serial.println("s=getOperationalState m=getCurrentMode t=setTemperatureSetpoint(220.0)");
  Serial.println("a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets the three verdict
   * callbacks run inside the 1000 ms window at all. See the header comment
   * above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(cavity->setOperationalState(1) ? "setOperationalState(1): OK" : "setOperationalState(1): failed"); break;
      case '2': Serial.println(cavity->setOperationalState(2) ? "setOperationalState(2): OK" : "setOperationalState(2): failed"); break;
      case '0': Serial.println(cavity->setOperationalState(0) ? "setOperationalState(0): OK" : "setOperationalState(0): failed"); break;
      case 'e':
        if (!cavity->setOperationalState(3)) {
          Serial.print("setOperationalState(3): refused host-side as expected, error ");
          Serial.println(Hearth.lastError());
        } else {
          Serial.println("setOperationalState(3): OK (unexpected: 3/Error is reserved)");
        }
        break;
      case 's': Serial.print("operationalState: "); Serial.println(cavity->getOperationalState()); break;
      case 'm': Serial.print("currentMode: "); Serial.println(cavity->getCurrentMode()); break;
      case 't':
        Serial.println(cavity->setTemperatureSetpoint(220.0) ? "setTemperatureSetpoint(220.0): OK" : "setTemperatureSetpoint(220.0): failed");
        break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
      default:  break;
    }
  }
  delay(10);
}
