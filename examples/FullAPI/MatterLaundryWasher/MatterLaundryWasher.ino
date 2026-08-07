/*
 * FullAPI reference: MatterLaundryWasher
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no OperationalState-backed appliance class at all (see the class
 * header), so this is this port's own design against the firmware's C4
 * wire contract, not a mirror of anything upstream.
 *
 * MatterLaundryWasher is one of the OperationalState trio
 * (MatterLaundryWasher, MatterDishwasher, MatterLaundryDryer): the entire
 * wire contract and callback shape live in the shared internal base
 * MatterOperationalStateEndpoint, which this class subclasses unchanged,
 * adding only its own device type constant. This sketch and its
 * MatterDishwasher/MatterLaundryDryer siblings are therefore identical in
 * every particular except the class name and the object's own name; the
 * cross-class proof that two trio members really do share one
 * implementation (one endpoint of each device type, driven over the
 * identical wire) lives at the test level, test/host/test_laundrywasher.cpp.
 *
 *   OperationalState_t:  kStateStopped=0 kStateRunning=1 kStatePaused=2
 *
 *   MatterLaundryWasher()          global object below
 *   begin()                        setup(), starts Stopped (no initial
 *                                  state to reconcile, see the class
 *                                  header)
 *   onPause(cb)                    setup(), verdict reads the policy flag
 *   onResume(cb)                   setup(), verdict reads the policy flag
 *   onStart(cb)                    setup(), verdict reads the policy flag
 *   onStop(cb)                     setup(), verdict reads the policy flag
 *   setOperationalState(state)     menu '1'/'2'/'0'
 *   getOperationalState()          menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * A deny IS the wire response here, unlike the water valve
 * -------------------------------------------------------------------------
 * The SDK copies the adjudication verdict straight into the command's own
 * OperationalCommandResponse (ErrorStateID 0x02,
 * UnableToCompleteOperation, on a deny), so onPause()/onResume()/
 * onStart()/onStop()'s return value is a real allow/deny the controller
 * observes, the same shape as MatterDoorLock and MatterChime, not
 * MatterWaterValve's discarded one. The same 1000 ms verdict window and
 * Hearth.poll()-every-iteration requirement documented on the door lock
 * apply here identically; see examples/FullAPI/MatterDoorLock for the
 * full explanation, not repeated here.
 * -------------------------------------------------------------------------
 *
 * Every OperationalState attribute is managed internally by the cluster's
 * own SDK Instance, so there is no AT+MTATTR path to any of them:
 * setOperationalState()/getOperationalState() are the only way this host
 * reports the appliance's actual state, over AT+MTOPSTATE. State 3
 * (Error) is reserved for the device's own fault-detection path and is
 * rejected +MTERR:1; this class does not accept it either (menu 'e'
 * demonstrates the rejection).
 *
 * Observe controller-side:
 *   chip-tool operationalstate pause <node> <ep>
 *   chip-tool operationalstate resume <node> <ep>
 *   chip-tool operationalstate read operational-state <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Washer.setOperationalState(MatterLaundryWasher::kStateRunning)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterLaundryWasher Washer;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny. All
// four verdict callbacks below read this same flag; a real sketch would
// consult its own control loop (is a cycle actually safe to pause/resume
// right now) here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and
  // must not touch the wire itself; see MatterDoorLock's own header
  // comment for the full verdict-window explanation.
  Washer.onPause([]() {
    bool allow = (policy == 'a');
    Serial.print("onPause verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Washer.onResume([]() {
    bool allow = (policy == 'a');
    Serial.print("onResume verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Washer.onStart([]() {
    bool allow = (policy == 'a');
    Serial.print("onStart verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Washer.onStop([]() {
    bool allow = (policy == 'a');
    Serial.print("onStop verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });

  // A HEARTH_CMD_TIMEOUT means a verdict window closed with the firmware
  // never having heard back from this host: worth logging, since it is
  // the diagnostic for the poll-latency trap described on the door lock.
  Hearth.onLinkEvent([](hearthEvent_t e) {
    if (e == HEARTH_CMD_TIMEOUT) {
      Serial.println("HEARTH_CMD_TIMEOUT: a verdict window closed unanswered (denied by default).");
    }
  });

  Washer.begin();  // starts Stopped; reaches the C6 at the Matter.begin() reconcile
  Matter.begin();
  Serial.println("FullAPI MatterLaundryWasher ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=setOperationalState(Running) 2=setOperationalState(Paused) 0=setOperationalState(Stopped)");
  Serial.println("e=setOperationalState(3) rejected-write demo (Error is reserved)");
  Serial.println("s=getOperationalState a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets onPause()/
   * onResume()/onStart()/onStop() run inside the 1000 ms window at all.
   * See the header comment above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1':
        Serial.println(Washer.setOperationalState(MatterLaundryWasher::kStateRunning) ? "setOperationalState(Running): OK" : "setOperationalState(Running): failed");
        break;
      case '2':
        Serial.println(Washer.setOperationalState(MatterLaundryWasher::kStatePaused) ? "setOperationalState(Paused): OK" : "setOperationalState(Paused): failed");
        break;
      case '0':
        Serial.println(Washer.setOperationalState(MatterLaundryWasher::kStateStopped) ? "setOperationalState(Stopped): OK" : "setOperationalState(Stopped): failed");
        break;
      case 'e':
        Serial.println(Washer.setOperationalState(3) ? "setOperationalState(3): OK (unexpected)" : "setOperationalState(3): failed (expected; Error is reserved)");
        break;
      case 's': Serial.print("operationalState: "); Serial.println(Washer.getOperationalState()); break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
