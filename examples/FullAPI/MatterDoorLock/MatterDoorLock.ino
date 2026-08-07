/*
 * FullAPI reference: MatterDoorLock
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. A Hearth-original class: arduino-esp32's Matter
 * library ships no door lock class or example at all (see the class
 * header), so this is this port's own design against the firmware's C3
 * wire contract, not a mirror of anything upstream.
 *
 *   LockState_t:  kStateNotFullyLocked=0 kStateLocked=1 kStateUnlocked=2
 *
 *   OperationSource_t:  kSourceUnspecified=0 kSourceManual=1
 *     kSourceProprietaryRemote=2 kSourceKeypad=3 kSourceAuto=4
 *     kSourceButton=5 kSourceSchedule=6 kSourceRemote=7 kSourceRfid=8
 *     kSourceBiometric=9 kSourceAliro=10
 *
 *   MatterDoorLock()               global object below
 *   begin(bool locked)             setup(), starts Locked
 *   onLock(cb)                     setup(), verdict reads the policy flag
 *   onUnlock(cb)                   setup(), verdict reads the policy flag
 *   setLockState(state, source)    menu '1'/'0' (manual), '2'/'3' (keypad)
 *   lock()                         menu 'k' (== setLockState(Locked, Manual))
 *   unlock()                       menu 'u' (== setLockState(Unlocked, Manual))
 *   getLockState()                 menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * The verdict window, and why loop() must never block
 * -------------------------------------------------------------------------
 * A controller's LockDoor/UnlockDoor command is forwarded to onLock()/
 * onUnlock() below. The firmware gives this host exactly 1000 ms, measured
 * from the moment it raised the +MTCMD URC, to answer with a verdict
 * (AT_MT_SPEC.md S3.17). That window is not a callback-execution budget: it
 * is the whole round trip, and it includes however long it takes this
 * sketch's loop() to next call into the library and actually see the
 * pending request, which is Hearth.poll()'s job (called unconditionally on
 * every iteration below). A loop() that blocks for any real fraction of a
 * second loses the race even with a callback that itself returns
 * instantly; the lock then fails closed (denied), never open, and
 * +MTCMDTO arrives here as HEARTH_CMD_TIMEOUT on Hearth.onLinkEvent() (see
 * setup() below) -- the diagnostic for exactly this failure shape.
 *
 * DEFERRED REPLY: the verdict callback's return value only decides
 * allow/deny. The AT+MTCMDRESP reply that carries it to the firmware goes
 * out afterward, from a queue drained once the current AT exchange
 * releases the link (Hearth.cpp's hearthDrainCmdRespQueue()) -- do not
 * expect the reply to have gone out yet by the time the callback returns.
 * -------------------------------------------------------------------------
 *
 * Observe controller-side:
 *   chip-tool doorlock lock-door <node> <ep>
 *   chip-tool doorlock unlock-door <node> <ep>
 *   chip-tool doorlock read lock-state <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Lock.setLockState(MatterDoorLock::kStateLocked)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterDoorLock Lock;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny. Both
// onLock() and onUnlock() below read this same flag; a real sketch would
// consult a keypad, an RFID reader or a companion app's own check here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). See the verdict-window
  // note above: must return fast, and must not touch the wire itself.
  Lock.onLock([]() {
    bool allow = (policy == 'a');
    Serial.print("onLock verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });
  Lock.onUnlock([]() {
    bool allow = (policy == 'a');
    Serial.print("onUnlock verdict (policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });

  // A HEARTH_CMD_TIMEOUT means a verdict window closed with the firmware
  // never having heard back from this host: worth logging, since it is
  // the diagnostic for the poll-latency trap described above.
  Hearth.onLinkEvent([](hearthEvent_t e) {
    if (e == HEARTH_CMD_TIMEOUT) {
      Serial.println("HEARTH_CMD_TIMEOUT: a verdict window closed unanswered (denied by default).");
    }
  });

  Lock.begin(true);  // starts Locked; reaches the C6 at the Matter.begin() reconcile
  Matter.begin();
  Serial.println("FullAPI MatterDoorLock ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=lock(manual) 0=unlock(manual) 2=lock(keypad) 3=unlock(keypad)");
  Serial.println("k=lock() u=unlock() s=getLockState a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets onLock()/onUnlock()
   * run inside the 1000 ms window at all. See the header comment above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1':
        Serial.println(Lock.setLockState(MatterDoorLock::kStateLocked, MatterDoorLock::kSourceManual) ? "lock(manual): OK" : "lock(manual): failed");
        break;
      case '0':
        Serial.println(Lock.setLockState(MatterDoorLock::kStateUnlocked, MatterDoorLock::kSourceManual) ? "unlock(manual): OK" : "unlock(manual): failed");
        break;
      case '2':
        Serial.println(Lock.setLockState(MatterDoorLock::kStateLocked, MatterDoorLock::kSourceKeypad) ? "lock(keypad): OK" : "lock(keypad): failed");
        break;
      case '3':
        Serial.println(Lock.setLockState(MatterDoorLock::kStateUnlocked, MatterDoorLock::kSourceKeypad) ? "unlock(keypad): OK" : "unlock(keypad): failed");
        break;
      case 'k': Serial.println(Lock.lock()   ? "lock(): OK"   : "lock(): failed");   break;
      case 'u': Serial.println(Lock.unlock() ? "unlock(): OK" : "unlock(): failed"); break;
      case 's': Serial.print("lockState: "); Serial.println(Lock.getLockState());    break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny");  break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
