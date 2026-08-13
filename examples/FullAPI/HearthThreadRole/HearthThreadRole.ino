/*
 * FullAPI reference: Hearth's Thread role and mesh identity surface
 * (Hearth.threadInfo(), Hearth.threadRole(), Hearth.onThreadRoleChange(),
 * hearthThreadRoleName()), AT_MT_SPEC.md S3.27 / event bit 28 (0.11.0).
 *
 * This is NOT one of this tier's fifty-one per-device-type sketches: the
 * surface it exercises lives on the Hearth global itself, not on a
 * Matter-named endpoint class (no arduino-esp32 counterpart exists for a
 * Thread role API, so per the README's split rule this is a Hearth
 * original, same as onLinkEvent()/transportMismatch()). There is
 * accordingly no MatterXxx object here and, deliberately, no
 * Matter.begin() call -- see the note below for why.
 *
 *   Hearth.threadInfo(info)        menu 'i': one full round trip, every
 *                                  field, including the has* flags
 *   Hearth.threadRole()            loop(), every iteration (no wire cost
 *                                  at all -- the whole point of this
 *                                  second read path); also menu 'r'
 *   Hearth.onThreadRoleChange(cb)  setup(); sets a flag, loop() prints
 *   hearthThreadRoleName(role)     used everywhere a role is printed
 *
 * THIS NEEDS THE THREAD IMAGE TO DO ANYTHING. On a WiFi image (or the
 * combined image booted in WiFi mode) there is no ThreadNetworkDiagnostics
 * cluster on endpoint 0 to read: AT+MTTHREAD? answers +MTERR:8, so
 * threadInfo() returns false with Hearth.lastError() == 8,
 * threadRole() reports HEARTH_THREAD_UNSPECIFIED, and +MTEVT:28 never
 * arrives because there is no role to change. That is not a bug in this
 * sketch or in the library; build and flash the Thread image to see real
 * data (see the firmware repo's README, "Thread is a build-time variant").
 *
 * WHY THERE IS NO Matter.begin() HERE: this sketch declares no endpoint
 * device type at all. Calling Matter.begin() with zero declared endpoints
 * against a co-processor that already carries a real composition would
 * read as a MISMATCH (0 declared vs N already on the device) and
 * reconcile the device down to zero endpoints -- destroying whatever
 * composition was there, a needlessly destructive side effect for a
 * sketch whose only job is to read one diagnostics cluster. A sketch that
 * wants both a real device type and this Thread role surface calls
 * Matter.begin() as every other FullAPI reference does, after declaring
 * its own endpoints; nothing below depends on it, because AT+MTTHREAD? is
 * not scoped to any declared endpoint.
 *
 * THE REENTRANCY RULE (the same shape as every other URC-dispatched
 * callback in this library -- see HearthDeviceEnergyManagement's
 * onPowerAdjust and the README's "Your onChange fires from inside your
 * own setter"): onThreadRoleChange()'s callback runs INSIDE URC dispatch,
 * with the link's busy gate held by whatever poll()/hearthCommand() call
 * is delivering the +MTEVT:28 line. A wire write from inside it --
 * including calling threadInfo() itself -- is refused
 * (HEARTH_CMD_REENTRANT) and reaches nothing. The pattern to copy, and the
 * one this sketch follows: set a flag in the callback, act on it from
 * loop().
 *
 * REGISTRATION IS WHAT SUBSCRIBES. Bit 28 is opt-in (AT_MT_SPEC.md S3.11's
 * default event mask has no Thread role bit), so the ONE call below --
 * Hearth.onThreadRoleChange(onRoleChange) in setup() -- is also what tells
 * the device to start sending +MTEVT:28 in the first place: it arms a
 * background AT+MTEVT?/AT+MTEVT= read-modify-write (never a blind write;
 * it never touches any other bit already subscribed) that the next
 * Hearth.poll() carries out. This sketch never has to repeat it: the
 * library re-arms the same exchange on its own after a co-processor
 * reboot, since AT_MT_SPEC.md S3.11 states the mask lives in RAM only and
 * reverts to the firmware default every time the C6 restarts.
 *
 * Observe controller-side (needs a commissioned Thread device; the static
 * reads work at any time, a live role CHANGE needs an actual mesh
 * transition, e.g. during commissioning):
 *   chip-tool threadnetworkdiagnostics read routing-role <node> 0
 *   chip-tool threadnetworkdiagnostics read channel <node> 0
 *   chip-tool threadnetworkdiagnostics read pan-id <node> 0
 *   chip-tool threadnetworkdiagnostics read extended-pan-id <node> 0
 *   chip-tool threadnetworkdiagnostics read partition-id <node> 0
 *   chip-tool threadnetworkdiagnostics read network-name <node> 0
 *
 * Error handling pattern (applies to every call in this library):
 *   if (!Hearth.threadInfo(info)) {
 *     // Hearth.lastError() holds the +MTERR code; see the README.
 *   }
 */
#include <Matter.h>

// Set by the callback, read by loop() -- see THE REENTRANCY RULE above.
volatile bool roleChanged = false;
HearthThreadRole lastRole = HEARTH_THREAD_UNSPECIFIED;

/*
 * NO WIRE WRITES HERE. This runs inside URC dispatch; it just records that
 * something happened. loop() decides what to do about it.
 */
void onRoleChange(HearthThreadRole role) {
  lastRole = role;
  roleChanged = true;
}

void printThreadInfo() {
  HearthThreadInfo info;
  if (!Hearth.threadInfo(info)) {
    Serial.print("Hearth.threadInfo() failed, lastError=");
    Serial.println(Hearth.lastError());
    if (Hearth.lastError() == HEARTH_ERR_NOT_SUPPORTED) {
      Serial.println("(HEARTH_ERR_NOT_SUPPORTED: this is a WiFi image, or the combined image booted in WiFi mode)");
    }
    return;
  }
  Serial.print("role: ");
  Serial.println(hearthThreadRoleName(info.role));
  Serial.print("attached: ");
  Serial.println(info.attached ? "yes" : "no");

  Serial.print("channel: ");
  if (info.hasChannel) {
    Serial.println(info.channel);
  } else {
    Serial.println("(unknown)");
  }

  Serial.print("panId: 0x");
  if (info.hasPanId) {
    Serial.println(info.panId, HEX);
  } else {
    Serial.println("(unknown)");
  }

  Serial.print("extPanId: 0x");
  if (info.hasExtPanId) {
    // Two 32-bit halves: Serial.println(uint64_t, HEX) is not available on
    // every core, arduino-pico's included.
    uint32_t hi = (uint32_t)(info.extPanId >> 32);
    uint32_t lo = (uint32_t)(info.extPanId & 0xFFFFFFFFu);
    if (hi < 0x10000000u) {
      Serial.print("0");  // keep the width visually stable across values
    }
    Serial.print(hi, HEX);
    Serial.println(lo, HEX);
  } else {
    Serial.println("(unknown)");
  }

  Serial.print("partitionId: 0x");
  if (info.hasPartitionId) {
    Serial.println(info.partitionId, HEX);
  } else {
    Serial.println("(unknown)");
  }

  Serial.print("name: \"");
  Serial.print(info.name);
  Serial.println("\"");
}

void printHelp() {
  Serial.println("i=threadInfo() full round trip   r=threadRole() cached, no wire   ?=help");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Hearth.onThreadRoleChange(onRoleChange);
  lastRole = Hearth.threadRole();  // seeded to UNSPECIFIED, no wire traffic

  Serial.println("FullAPI HearthThreadRole ready; '?' for menu");
  Serial.print("cached role before any query (threadRole(), no wire yet): ");
  Serial.println(hearthThreadRoleName(lastRole));
}

void loop() {
  Hearth.poll();  // dispatches +MTEVT:28 into onRoleChange()

  if (roleChanged) {
    roleChanged = false;
    Serial.print("role changed (+MTEVT:28): ");
    Serial.println(hearthThreadRoleName(lastRole));
  }

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'i':
        printThreadInfo();
        break;
      case 'r':
        Serial.print("threadRole() (cached, no wire): ");
        Serial.println(hearthThreadRoleName(Hearth.threadRole()));
        break;
      case '?':
        printHelp();
        break;
    }
  }
  delay(10);
}
