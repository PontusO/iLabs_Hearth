/*
 * FullAPI reference: MatterRoboticVacuum
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Robotic Vacuum Cleaner class at all (see the class header), so
 * this is this port's own design against the firmware's wire contract
 * (docs/AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.21), not a mirror of anything
 * upstream. One endpoint carries three clusters at once: RvcRunMode (84),
 * RvcCleanMode (85), RvcOperationalState (97).
 *
 *   RvcState_t:  kStateStopped=0 kStateRunning=1 kStatePaused=2
 *                kStateSeekingCharger=0x40 kStateCharging=0x41 kStateDocked=0x42
 *
 *   MatterRoboticVacuum()                    global object below
 *   begin()                                  setup(), declares only; no
 *                                             initial state or mode list to
 *                                             reconcile
 *   setSupportedRunModes(...)                setup(), AFTER Matter.begin()
 *   setSupportedCleanModes(...)               setup(), AFTER Matter.begin()
 *   onChangeRunMode(cb)                      setup(), verdict reads the
 *                                             policy flag, caches on allow
 *   onChangeCleanMode(cb)                    setup(), same shape
 *   onPause(cb)                              setup(), verdict reads the
 *                                             policy flag
 *   onResume(cb)                             setup(), verdict reads the
 *                                             policy flag
 *   onGoHome(cb)                             setup(), verdict reads the
 *                                             policy flag
 *   setOperationalState(state)               menu '1'/'2'/'0'/'c'/'h'/'k'
 *   getOperationalState()                    menu 's'
 *   getCurrentRunMode()                      menu 'r'
 *   getCurrentCleanMode()                    menu 'l'
 *   attributeChangeCB / hearthAttrTypeFor    not called by this sketch:
 *                                             documented no-ops, see the
 *                                             class header
 *   end()                                    not called: tearing the
 *                                             endpoint down mid-demo is not
 *                                             a usable demo; call it when
 *                                             retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * Mode lists set AFTER Matter.begin() -- the 534c189 lesson
 * -------------------------------------------------------------------------
 * Before the reconcile the endpoint id is still 0, and setSupportedRunModes()
 * / setSupportedCleanModes() both refuse an unaddressable endpoint (error 2,
 * no wire traffic at all), the same refusal MatterModeSelect's own FullAPI
 * example was fixed to respect after commit 534c189 found the earlier
 * ordering booted with an empty SupportedModes list on real hardware. Both
 * calls below run after Matter.begin(), not between it and Vacuum.begin().
 * -------------------------------------------------------------------------
 *
 * -------------------------------------------------------------------------
 * CurrentMode has no ember-level signal on this class's clusters at all
 * -------------------------------------------------------------------------
 * RvcRunMode/RvcCleanMode's CurrentMode, and RvcOperationalState's own
 * OperationalState attribute, are AttributeAccessInterface-served, never
 * ember-backed (AT_MT_SPEC.md S3.20.1/S3.21): no +MTATTR URC is ever raised
 * for either, from the firmware's own clamp or a controller-driven change.
 * getCurrentRunMode()/getCurrentCleanMode() are therefore this host's own
 * bookkeeping, updated only when onChangeRunMode()/onChangeCleanMode()
 * itself returns true for a forwarded ChangeToMode -- there is no other
 * signal to watch. Same reasoning as the OperationalState trio's
 * setOperationalState()/getOperationalState(), which this class shares:
 * the host's own control loop is authoritative, never the wire.
 * -------------------------------------------------------------------------
 *
 * A deny IS the wire response here for Pause/Resume/GoHome (like the
 * OperationalState trio, unlike the water valve): the SDK copies the
 * adjudication verdict straight into the command's own
 * OperationalCommandResponse (ErrorStateID 0x02, UnableToCompleteOperation,
 * on a deny). The same 1000 ms verdict window and Hearth.poll()-every-
 * iteration requirement documented on the door lock apply here identically;
 * see examples/FullAPI/MatterDoorLock for the full explanation, not
 * repeated here.
 *
 * Observe controller-side:
 *   chip-tool rvcrunmode change-to-mode 1 <node> <ep>
 *   chip-tool rvcrunmode read current-mode <node> <ep>
 *   chip-tool rvcrunmode read supported-modes <node> <ep>
 *   chip-tool rvccleanmode change-to-mode 1 <node> <ep>
 *   chip-tool rvccleanmode read current-mode <node> <ep>
 *   chip-tool rvccleanmode read supported-modes <node> <ep>
 *   chip-tool rvcoperationalstate pause <node> <ep>
 *   chip-tool rvcoperationalstate resume <node> <ep>
 *   chip-tool rvcoperationalstate go-home <node> <ep>
 *   chip-tool rvcoperationalstate read operational-state <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Vacuum.setOperationalState(MatterRoboticVacuum::kStateRunning)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterRoboticVacuum Vacuum;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny. Every
// verdict callback below reads this same flag; a real sketch would consult
// its own control loop (is the requested mode/transition actually safe
// right now) here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and must
  // not touch the wire itself; see MatterDoorLock's own header comment for
  // the full verdict-window explanation.
  Vacuum.onChangeRunMode([](uint8_t mode) {
    bool allow = (policy == 'a');
    Serial.print("onChangeRunMode(");
    Serial.print(mode);
    Serial.print(") verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Vacuum.onChangeCleanMode([](uint8_t mode) {
    bool allow = (policy == 'a');
    Serial.print("onChangeCleanMode(");
    Serial.print(mode);
    Serial.print(") verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Vacuum.onPause([]() {
    bool allow = (policy == 'a');
    Serial.print("onPause verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Vacuum.onResume([]() {
    bool allow = (policy == 'a');
    Serial.print("onResume verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Vacuum.onGoHome([]() {
    bool allow = (policy == 'a');
    Serial.print("onGoHome verdict (policy=");
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

  Vacuum.begin();  // declares only; the C6's cluster defaults apply until a setter writes state.
  Matter.begin();

  // Mode lists set AFTER Matter.begin(); see the header comment above.
  // Tag 0 asks the firmware for this cluster's own conformance-required
  // default (kIdle-first/kCleaning for RvcRunMode, kVacuum for
  // RvcCleanMode, AT_MT_SPEC.md S3.20.1's table); this sketch has no
  // reason to care about explicit tags, so every triple below uses 0.
  static const uint8_t kRunModes[2] = { 0, 1 };
  static const uint16_t kRunTags[2] = { 0, 0 };
  static const char *kRunLabels[2] = { "Idle", "Cleaning" };
  Serial.println(Vacuum.setSupportedRunModes(kRunModes, kRunTags, kRunLabels, 2) ? "setSupportedRunModes: OK" : "setSupportedRunModes: failed");

  static const uint8_t kCleanModes[2] = { 0, 1 };
  static const uint16_t kCleanTags[2] = { 0, 0 };
  static const char *kCleanLabels[2] = { "Vacuum", "Mop" };
  Serial.println(Vacuum.setSupportedCleanModes(kCleanModes, kCleanTags, kCleanLabels, 2) ? "setSupportedCleanModes: OK" : "setSupportedCleanModes: failed");

  Serial.println("FullAPI MatterRoboticVacuum ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=setOperationalState(Running) 2=setOperationalState(Paused) 0=setOperationalState(Stopped)");
  Serial.println("c=setOperationalState(SeekingCharger) h=setOperationalState(Charging) k=setOperationalState(Docked)");
  Serial.println("e=setOperationalState(3) rejected-write demo (Error is reserved)");
  Serial.println("s=getOperationalState r=getCurrentRunMode l=getCurrentCleanMode");
  Serial.println("a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets onChangeRunMode()/
   * onChangeCleanMode()/onPause()/onResume()/onGoHome() run inside the
   * 1000 ms window at all. See the header comment above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1':
        Serial.println(
          Vacuum.setOperationalState(MatterRoboticVacuum::kStateRunning) ? "setOperationalState(Running): OK" : "setOperationalState(Running): failed"
        );
        break;
      case '2':
        Serial.println(
          Vacuum.setOperationalState(MatterRoboticVacuum::kStatePaused) ? "setOperationalState(Paused): OK" : "setOperationalState(Paused): failed"
        );
        break;
      case '0':
        Serial.println(
          Vacuum.setOperationalState(MatterRoboticVacuum::kStateStopped) ? "setOperationalState(Stopped): OK" : "setOperationalState(Stopped): failed"
        );
        break;
      case 'c':
        Serial.println(
          Vacuum.setOperationalState(MatterRoboticVacuum::kStateSeekingCharger) ? "setOperationalState(SeekingCharger): OK"
                                                                                 : "setOperationalState(SeekingCharger): failed"
        );
        break;
      case 'h':
        Serial.println(
          Vacuum.setOperationalState(MatterRoboticVacuum::kStateCharging) ? "setOperationalState(Charging): OK" : "setOperationalState(Charging): failed"
        );
        break;
      case 'k':
        Serial.println(
          Vacuum.setOperationalState(MatterRoboticVacuum::kStateDocked) ? "setOperationalState(Docked): OK" : "setOperationalState(Docked): failed"
        );
        break;
      case 'e':
        Serial.println(Vacuum.setOperationalState(3) ? "setOperationalState(3): OK (unexpected)" : "setOperationalState(3): failed (expected; Error is reserved)");
        break;
      case 's': Serial.print("operationalState: "); Serial.println(Vacuum.getOperationalState()); break;
      case 'r': Serial.print("currentRunMode: "); Serial.println(Vacuum.getCurrentRunMode()); break;
      case 'l': Serial.print("currentCleanMode: "); Serial.println(Vacuum.getCurrentCleanMode()); break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
