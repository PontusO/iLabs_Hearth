/*
 * FullAPI reference: MatterWaterValve
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no water valve class or example at all (see the class header), so
 * this is this port's own design against the firmware's C2 wire contract,
 * not a mirror of anything upstream.
 *
 *   ValveState_t:  kStateClosed=0 kStateOpen=1 kStateTransitioning=2
 *
 *   MatterWaterValve()             global object below
 *   begin()                        setup(), starts Closed (no initial
 *                                  state to reconcile, see the class
 *                                  header)
 *   onOpen(cb)                     setup(), verdict reads the policy flag
 *   onClose(cb)                    setup(), verdict reads the policy flag
 *   setValveState(state)           menu '1'/'0'
 *   setValveState(state, level)    menu 'l', level 0..100 accepted but
 *                                  never cached (see below)
 *   getValveState()                menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * The verdict cannot fail the command on the wire
 * -------------------------------------------------------------------------
 * Unlike MatterDoorLock, ValveConfigurationAndControl's own cluster server
 * calls the delegate's HandleOpenValve/HandleCloseValve synchronously and
 * discards what it returns (TEMPORARY_RETURN_IGNORED at both call sites,
 * an SDK property, not a firmware or library choice). The controller
 * always sees Status::Success once the command reaches this host at all.
 * The verdict onOpen()/onClose() give still travels back over
 * AT+MTCMDRESP -- the generic dispatcher does not know per-endpoint-type
 * that this particular reply is discarded -- but it decides nothing the
 * controller can observe. What it DOES gate is purely host-side: whether
 * this sketch's own handler goes on to actually move the physical valve.
 * A denying callback must still return false for that reason (it is the
 * only signal this sketch has that "no, don't actuate"), it just does not
 * mean what a denying MatterDoorLock::onLock() means.
 *
 * The same 1000 ms verdict window and Hearth.poll()-every-iteration
 * requirement documented on the door lock apply here identically; see
 * examples/FullAPI/MatterDoorLock for the full explanation, not repeated
 * here.
 * -------------------------------------------------------------------------
 *
 * <level> is accepted by the two-arg setValveState() but never cached or
 * readable back: this SDK revision's water_valve thunk fixes FeatureMap at
 * 0, so CurrentLevel/TargetLevel are never created as attributes at all
 * (AT_MT_SPEC.md S3.19). A level sent through AT+MTVALVE neither errors
 * nor does anything a controller can observe.
 *
 * Observe controller-side:
 *   chip-tool valveconfigurationandcontrol open <node> <ep>
 *   chip-tool valveconfigurationandcontrol close <node> <ep>
 *   chip-tool valveconfigurationandcontrol read current-state <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Valve.setValveState(MatterWaterValve::kStateOpen)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterWaterValve Valve;

// Sketch-side actuation policy, menu-set: 'a' allow, 'd' deny. Both
// onOpen() and onClose() below read this same flag; see the header
// comment above for why a deny here gates actuation only, never the
// controller's own view of the command.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and
  // must not touch the wire itself; see MatterDoorLock's own header
  // comment for the full verdict-window explanation.
  Valve.onOpen([]() {
    bool allow = (policy == 'a');
    Serial.print("onOpen verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow (actuates)" : "): deny (does not actuate; controller still sees success)");
    return allow;
  });
  Valve.onClose([]() {
    bool allow = (policy == 'a');
    Serial.print("onClose verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow (actuates)" : "): deny (does not actuate; controller still sees success)");
    return allow;
  });

  // A HEARTH_CMD_TIMEOUT means a verdict window closed with the firmware
  // never having heard back from this host: worth logging even though a
  // deny here never changes what the controller sees, since it still
  // means this sketch's own actuation logic never ran.
  Hearth.onLinkEvent([](hearthEvent_t e) {
    if (e == HEARTH_CMD_TIMEOUT) {
      Serial.println("HEARTH_CMD_TIMEOUT: a verdict window closed unanswered (denied by default).");
    }
  });

  Valve.begin();  // starts Closed; declares the endpoint only. The C6's cluster defaults apply until a setter writes state.
  Matter.begin();
  Serial.println("FullAPI MatterWaterValve ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=setValveState(Open) 0=setValveState(Closed) l=setValveState(Open,50)");
  Serial.println("s=getValveState a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets onOpen()/onClose()
   * run inside the 1000 ms window at all. See the header comment above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1':
        Serial.println(Valve.setValveState(MatterWaterValve::kStateOpen) ? "setValveState(Open): OK" : "setValveState(Open): failed");
        break;
      case '0':
        Serial.println(Valve.setValveState(MatterWaterValve::kStateClosed) ? "setValveState(Closed): OK" : "setValveState(Closed): failed");
        break;
      case 'l':
        Serial.println(Valve.setValveState(MatterWaterValve::kStateOpen, 50) ? "setValveState(Open, 50): OK (level not cached, see header)" : "setValveState(Open, 50): failed");
        break;
      case 's': Serial.print("valveState: "); Serial.println(Valve.getValveState()); break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
