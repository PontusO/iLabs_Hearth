/*
 * MatterDoorLockAdjudicated.ino
 *
 * This is a Hearth-original example: there is no upstream arduino-esp32
 * sketch to copy. arduino-esp32's Matter library ships no door lock class
 * or example at all (see MatterDoorLock.h's own header comment, and
 * README.md's "Hearth originals"), so unlike every other sketch under
 * examples/, this one is not a byte-identical copy of anything.
 *
 * -------------------------------------------------------------------------
 * The verdict window, and why loop() must never block
 * -------------------------------------------------------------------------
 * A controller's LockDoor/UnlockDoor command is forwarded to onLockRequest()/
 * onUnlockRequest() below. The firmware gives this host exactly 1000 ms,
 * measured from the moment it raised the +MTCMD URC, to answer with a
 * verdict (AT_MT_SPEC.md S3.17). That window is not a callback-execution
 * budget: it is the whole round trip, and it includes however long it takes
 * this sketch's loop() to next call into the library and actually see the
 * pending request in the first place. Hearth.poll() (called on every
 * iteration below) is what makes that happen; a loop() that blocks for any
 * real fraction of a second -- a delay(), a slow sensor read, a busy-wait --
 * can lose the race even with a callback that itself returns instantly. If
 * that happens, the firmware default-denies (a lock fails closed, never
 * open) and raises +MTCMDTO, which arrives here as HEARTH_CMD_TIMEOUT on
 * Hearth.onLinkEvent() -- the diagnostic for exactly this failure shape.
 * Nothing in this sketch's loop() blocks, on purpose.
 *
 * Two more things this example exists to show:
 *
 * - The verdict callback only decides allow/deny; it must not drive the
 *   physical lock itself (AT+MTCMDRESP goes out afterward, from a deferred
 *   queue, not synchronously from inside the callback -- see README.md).
 *   Actuation happens in loop(), from flags the callbacks set.
 * - Reporting that the bolt actually moved (setLockState()) is a separate
 *   step from the verdict, on a separate command (AT+MTLOCK). The firmware
 *   never calls it on its own, even after an allowed verdict (spec F4):
 *   actuation timing belongs entirely to this sketch.
 * -------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <Matter.h>

// The door lock endpoint. begin(true) declares it starting Locked; the
// state itself reaches the C6 at the next Matter.begin() reconcile, not
// from begin() itself.
MatterDoorLock FrontDoor;

// Sketch-side adjudication policy. MatterDoorLock's feature map is 0 (no
// PIN/user/credential surface, AT_MT_SPEC.md's device-type table): a
// controller can only send a bare LockDoor/UnlockDoor, never a PIN-carrying
// one, and the device's own CHIP stack refuses one before it ever reaches
// here. A real sketch might consult a wired keypad, an RFID reader or a
// companion app's own PIN check; here it is a simple "armed" flag toggled
// over Serial, standing in for whatever that policy would be.
volatile bool armed = true;  // true: refuse remote unlock; false: allow it

// Set from the (must-be-fast) verdict callbacks below, acted on in loop().
// Never touch the physical lock or call back into the library from inside
// onLockRequest()/onUnlockRequest() themselves.
volatile bool pendingLock = false;
volatile bool pendingUnlock = false;

// Runs synchronously from inside whichever library call dispatched the
// +MTCMD URC (Hearth.poll(), in this sketch). Locking is always allowed.
bool onLockRequest() {
  pendingLock = true;
  return true;
}

// Same calling context as onLockRequest(). Denies while armed; the denial
// itself is the adjudication, no separate PIN check is possible over this
// path (see the "armed" comment above).
bool onUnlockRequest() {
  bool allow = !armed;
  if (allow) {
    pendingUnlock = true;
  }
  return allow;
}

// A HEARTH_CMD_TIMEOUT here means a verdict window closed with the firmware
// never having heard back from this host -- worth logging, since it is the
// one Hearth-side event this sketch cares about; every other hearthEvent_t
// value is link-lifecycle noise for this example's purposes.
void onHearthLinkEvent(hearthEvent_t e) {
  if (e == HEARTH_CMD_TIMEOUT) {
    Serial.println("HEARTH_CMD_TIMEOUT: a verdict window closed unanswered (denied by default).");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("MatterDoorLockAdjudicated: send 'a' to arm, 'd' to disarm over Serial.");

  Hearth.onLinkEvent(onHearthLinkEvent);

  FrontDoor.onLock(onLockRequest);
  FrontDoor.onUnlock(onUnlockRequest);
  FrontDoor.begin(true);  // starts Locked

  // Matter beginning - last step, after the endpoint's own begin().
  Matter.begin();
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
  } else {
    Serial.println("Not commissioned yet. Manual pairing code:");
    Serial.println(Matter.getManualPairingCode());
  }
}

void loop() {
  // Every iteration, unconditionally: this is what lets onLockRequest()/
  // onUnlockRequest() run inside the 1000 ms window at all. See the header
  // comment above.
  Hearth.poll();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'a') {
      armed = true;
      Serial.println("Armed. Remote unlock will be refused.");
    } else if (c == 'd') {
      armed = false;
      Serial.println("Disarmed. Remote unlock will be allowed.");
    }
  }

  // Actuation, outside any callback: drive the real hardware here, then
  // report the state once it has actually moved. setLockState()'s cache
  // only updates on a successful write.
  if (pendingLock) {
    pendingLock = false;
    // ... drive the physical bolt closed here ...
    if (FrontDoor.setLockState(MatterDoorLock::kStateLocked, MatterDoorLock::kSourceManual)) {
      Serial.println("Locked.");
    }
  }
  if (pendingUnlock) {
    pendingUnlock = false;
    // ... drive the physical bolt open here ...
    if (FrontDoor.setLockState(MatterDoorLock::kStateUnlocked, MatterDoorLock::kSourceManual)) {
      Serial.println("Unlocked.");
    }
  }
}
