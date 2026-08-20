/*
 * FullAPI reference: MatterMicrowaveOven
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Microwave Oven class at all (see the class header), so this is
 * this port's own design against the firmware's wire contract
 * (AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.21), not a mirror of anything
 * upstream. One endpoint carries three clusters at once: MicrowaveOvenMode
 * (94), MicrowaveOvenControl (95), and the plain OperationalState (96) this
 * class inherits from MatterOperationalStateEndpoint.
 *
 *   MatterMicrowaveOven()                    global object below
 *   begin()                                  setup(), declares only; no
 *                                             initial state or mode list to
 *                                             reconcile
 *   setSupportedModes(...)                   setup(), AFTER Matter.begin()
 *   onCookingParameters(cb)                  setup(), verdict reads the
 *                                             policy flag, prints all four
 *                                             fields with their has-flags
 *   onAddMoreTime(cb)                        setup(), same shape; the
 *                                             field is the absolute new
 *                                             cook time, not a delta
 *   onPause()/onResume()/onStart()/onStop()  inherited from
 *                                             MatterOperationalStateEndpoint;
 *                                             setup(), verdict reads the
 *                                             policy flag
 *   setOperationalState(state)               inherited; menu '1'/'2'/'0'
 *   getOperationalState()                    inherited; menu 's'
 *   attributeChangeCB / hearthAttrTypeFor    not called by this sketch:
 *                                             documented no-ops inherited
 *                                             from MatterOperationalState-
 *                                             Endpoint, see the class header
 *   end()                                    not called: tearing the
 *                                             endpoint down mid-demo is not
 *                                             a usable demo; call it when
 *                                             retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * MicrowaveOvenMode has no ChangeToMode command at all
 * -------------------------------------------------------------------------
 * MicrowaveOvenMode's own CommandIds.h declares zero accepted commands
 * (AT_MT_SPEC.md S3.20.1: "MicrowaveOvenMode has no ChangeToMode command at
 * all; its mode is selected through SetCookingParameters' cookMode field").
 * This sketch therefore registers no mode-change handler for cluster 94 --
 * there is nothing on the wire to adjudicate -- and mode selection happens
 * entirely through onCookingParameters()'s cookMode field below, alongside
 * cook time, power and the start-after flag in the very same command.
 * -------------------------------------------------------------------------
 *
 * -------------------------------------------------------------------------
 * Mode list set AFTER Matter.begin() -- the 534c189 lesson
 * -------------------------------------------------------------------------
 * Before the reconcile the endpoint id is still 0, and setSupportedModes()
 * refuses an unaddressable endpoint (error 2, no wire traffic at all), the
 * same refusal MatterRoboticVacuum's own FullAPI example follows after
 * commit 534c189 found the earlier ordering booted with an empty
 * SupportedModes list on real hardware. The call below runs after
 * Matter.begin(), not between it and Oven.begin().
 * -------------------------------------------------------------------------
 *
 * -------------------------------------------------------------------------
 * No AT+MTATTR path for CookTime/PowerSetting -- read them from a controller
 * -------------------------------------------------------------------------
 * CookTime and PowerSetting are Instance/delegate-owned, command-driven
 * state, never ember-backed (AT_MT_SPEC.md S3.17): no +MTATTR URC ever
 * fires for either, from this firmware's own clamp or a controller-driven
 * change, so this class exposes no getters for them. onCookingParameters()/
 * onAddMoreTime()'s own arguments are the only signal this host ever sees;
 * a sketch that needs to display the oven's actual cook time or power must
 * read them back through a commissioned controller instead.
 * -------------------------------------------------------------------------
 *
 * A deny IS the wire response for SetCookingParameters/AddMoreTime (same
 * verdict-is-wire-response shape as chime's PlayChimeSound, not the
 * OperationalState family's GenericOperationalError indirection): the SDK
 * copies the host's allow/deny straight into the command's own
 * InvokeResponse (allow Status::Success, deny Status::Failure). The
 * inherited Pause/Stop/Start/Resume verdicts ARE the
 * GenericOperationalError shape instead (ErrorStateID 0x02 on a deny), the
 * same as every other OperationalState-family class. The same 1000 ms
 * verdict window and Hearth.poll()-every-iteration requirement documented
 * on the door lock apply to every forward here identically; see
 * examples/FullAPI/MatterDoorLock for the full explanation, not repeated
 * here.
 *
 * Observe controller-side:
 *   chip-tool microwaveovenmode read supported-modes <node> <ep>
 *   chip-tool microwaveovencontrol set-cooking-parameters --CookMode 1 --CookTime 90 --PowerSetting 80 --StartAfterSetting 0 <node> <ep>
 *   chip-tool microwaveovencontrol add-more-time 60 <node> <ep>
 *   chip-tool microwaveovencontrol read cook-time <node> <ep>
 *   chip-tool microwaveovencontrol read power-setting <node> <ep>
 *   chip-tool operationalstate pause <node> <ep>
 *   chip-tool operationalstate resume <node> <ep>
 *   chip-tool operationalstate read operational-state <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Oven.setOperationalState(MatterMicrowaveOven::kStateRunning)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterMicrowaveOven Oven;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny. Every
// verdict callback below reads this same flag; a real sketch would consult
// its own control loop (is the requested cook cycle actually safe right
// now, is the door closed) here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and must
  // not touch the wire itself; see MatterDoorLock's own header comment for
  // the full verdict-window explanation.
  Oven.onCookingParameters([](const HearthCookingParams &p) {
    bool allow = (policy == 'a');
    Serial.print("onCookingParameters(");
    if (p.hasCookMode) { Serial.print("cookMode="); Serial.print(p.cookMode); } else { Serial.print("cookMode=<absent>"); }
    Serial.print(", ");
    if (p.hasCookTime) { Serial.print("cookTimeSec="); Serial.print(p.cookTimeSec); } else { Serial.print("cookTimeSec=<absent>"); }
    Serial.print(", ");
    if (p.hasPower) { Serial.print("powerPercent="); Serial.print(p.powerPercent); } else { Serial.print("powerPercent=<absent>"); }
    Serial.print(", startAfterSetting=");
    Serial.print(p.startAfterSetting ? "true" : "false");
    Serial.print(") verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Oven.onAddMoreTime([](uint32_t finalCookTimeSec) {
    bool allow = (policy == 'a');
    Serial.print("onAddMoreTime(finalCookTimeSec=");
    Serial.print(finalCookTimeSec);
    Serial.print(") verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  // Inherited from MatterOperationalStateEndpoint: the plain
  // OperationalState cluster's Pause/Stop/Start/Resume, dispatched through
  // this class's own hearthOnForwardedCommandFields() deferral to the base
  // class (see the class header).
  Oven.onPause([]() {
    bool allow = (policy == 'a');
    Serial.print("onPause verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Oven.onResume([]() {
    bool allow = (policy == 'a');
    Serial.print("onResume verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Oven.onStart([]() {
    bool allow = (policy == 'a');
    Serial.print("onStart verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Oven.onStop([]() {
    bool allow = (policy == 'a');
    Serial.print("onStop verdict (policy=");
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

  Oven.begin();  // declares only; the C6's cluster defaults apply until a setter writes state.
  Matter.begin();

  // Mode list set AFTER Matter.begin(); see the header comment above. Tag 0
  // asks the firmware for this cluster's own conformance-required default
  // (kNormal on every mode, first or not, AT_MT_SPEC.md S3.20.1's table);
  // this sketch has no reason to care about explicit tags, so both triples
  // below use 0.
  static const uint8_t kModes[2] = { 0, 1 };
  static const uint16_t kTags[2] = { 0, 0 };
  static const char *kLabels[2] = { "Normal", "Reheat" };
  Serial.println(Oven.setSupportedModes(kModes, kTags, kLabels, 2) ? "setSupportedModes: OK" : "setSupportedModes: failed");

  Serial.println("FullAPI MatterMicrowaveOven ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=setOperationalState(Running) 2=setOperationalState(Paused) 0=setOperationalState(Stopped)");
  Serial.println("s=getOperationalState");
  Serial.println("a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets
   * onCookingParameters()/onAddMoreTime()/onPause()/onResume()/onStart()/
   * onStop() run inside the 1000 ms window at all. See the header comment
   * above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1':
        Serial.println(
          Oven.setOperationalState(MatterMicrowaveOven::kStateRunning) ? "setOperationalState(Running): OK" : "setOperationalState(Running): failed"
        );
        break;
      case '2':
        Serial.println(
          Oven.setOperationalState(MatterMicrowaveOven::kStatePaused) ? "setOperationalState(Paused): OK" : "setOperationalState(Paused): failed"
        );
        break;
      case '0':
        Serial.println(
          Oven.setOperationalState(MatterMicrowaveOven::kStateStopped) ? "setOperationalState(Stopped): OK" : "setOperationalState(Stopped): failed"
        );
        break;
      case 's': Serial.print("operationalState: "); Serial.println(Oven.getOperationalState()); break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
