/*
 * FullAPI reference: MatterChime
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Chime class or example at all (see the class header), so this
 * is this port's own design against the firmware's C6 wire contract, not
 * a mirror of anything upstream.
 *
 *   MatterChime()                             global object below
 *   begin()                                   setup(), no initial
 *                                              SelectedChime/Enabled/
 *                                              InstalledChimeSounds to
 *                                              reconcile
 *   onPlayChime(cb)                           setup(), verdict reads the
 *                                              policy flag AND receives
 *                                              the reserved fifth +MTCMD
 *                                              field (chimeID)
 *   setInstalledChimeSounds(ids, names, count)  setup() and menu 'v'
 *   setSelectedChime(id)                       menu 'c', cycles installed ids
 *   getSelectedChime()                         menu 'g'
 *   setEnabled(on)                             menu '1'/'0'
 *   getEnabled()                               menu 's'
 *   end()                       not called: tearing the endpoint down
 *                                mid-demo is not a usable demo; call it
 *                                when retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * PlayChimeSound is a REAL wire verdict, unlike the water valve's
 * -------------------------------------------------------------------------
 * Unlike MatterWaterValve, the host's verdict reaches the controller
 * exactly as given: Status::Success on allow, Status::Failure on deny,
 * with no SDK-side remapping. onPlayChime() below therefore behaves
 * exactly like MatterDoorLock's onLock()/onUnlock(); the same 1000 ms
 * verdict window and Hearth.poll()-every-iteration requirement apply
 * identically (see examples/FullAPI/MatterDoorLock for the full
 * explanation, not repeated here).
 *
 * It is also the first (and, at this writing, only) consumer in this
 * library of AT_MT_SPEC.md S3.17's reserved fifth +MTCMD payload field:
 * the requested chimeID arrives as onPlayChime()'s own argument, not
 * through a separate read.
 *
 * "whenever the command actually reaches the host at all" is load-bearing:
 * the SDK short-circuits PlayChimeSound before it ever forwards, answering
 * the controller itself with no +MTCMD URC raised at all, in two cases:
 * Enabled is false (answers Status::Success), or the chimeID names a sound
 * not currently installed via setInstalledChimeSounds() (answers
 * Status::NotFound). This sketch's onPlayChime() callback is simply never
 * consulted for either case; there is nothing this sketch could do about
 * it, since it is the SDK's own short-circuit.
 * -------------------------------------------------------------------------
 *
 * InstalledChimeSounds and SelectedChime/Enabled have opposite
 * persistence. InstalledChimeSounds is not persisted (the store lives in
 * RAM and starts empty every boot), so setInstalledChimeSounds()'s cached
 * list is re-sent verbatim on every later reconcile, the same
 * MatterModeSelect norm; menu 'v' demonstrates a replacement, not a
 * resend. SelectedChime/Enabled persist through CHIP's own attribute
 * persistence provider and survive AT+MTRESET, so setSelectedChime()/
 * setEnabled() need no reconcile push at all -- and, since neither
 * attribute has any AT+MTATTR path either, getSelectedChime()/getEnabled()
 * are cache-only in both directions: only this sketch's own writes ever
 * change what they return.
 *
 * Observe controller-side:
 *   chip-tool chime play-chime-sound <id> <node> <ep>
 *   chip-tool chime read installed-chime-sounds <node> <ep>
 *   chip-tool chime read selected-chime <node> <ep>
 *   chip-tool chime read enabled <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Chime.setSelectedChime(1)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterChime Chime;

// Sketch-side adjudication policy, menu-set: 'a' allow, 'd' deny.
// onPlayChime() below reads this same flag; a real sketch would consult
// its own logic (do-not-disturb hours, a local mute switch) here.
char policy = 'a';

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Runs synchronously from inside whichever library call dispatched the
  // +MTCMD URC (Hearth.poll(), in this sketch). Must return fast, and
  // must not touch the wire itself; see MatterDoorLock's own header
  // comment for the full verdict-window explanation.
  Chime.onPlayChime([](uint8_t chimeID) {
    bool allow = (policy == 'a');
    Serial.print("onPlayChime(chimeID=");
    Serial.print(chimeID);
    Serial.print(", policy=");
    Serial.print(policy);
    Serial.println(allow ? "): allow" : "): deny");
    return allow;
  });

  // A HEARTH_CMD_TIMEOUT means a verdict window closed with the firmware
  // never having heard back from this host: worth logging, since it is
  // the diagnostic for the poll-latency trap described in the door lock
  // example.
  Hearth.onLinkEvent([](hearthEvent_t e) {
    if (e == HEARTH_CMD_TIMEOUT) {
      Serial.println("HEARTH_CMD_TIMEOUT: a verdict window closed unanswered (denied by default).");
    }
  });

  Chime.begin();
  Matter.begin();

  // Two id/name pairs, one name containing a comma to exercise the
  // AT+MTCHIMESOUNDS quoting path (a comma inside a quoted name is legal
  // and part of its text, the same grammar MatterModeSelect's
  // setSupportedModes() enforces). Called AFTER Matter.begin(): before the
  // reconcile the endpoint id is still 0, and every setter here refuses an
  // unaddressable endpoint (error 2, no wire traffic at all), the same
  // post-reconcile placement MatterTemperatureControlledCabinet's FullAPI
  // example uses for setSupportedTemperatureLevelLabels().
  static const uint8_t kIds[2] = { 1, 2 };
  static const char *kNames[2] = { "Doorbell, classic", "Two-tone" };
  Serial.println(Chime.setInstalledChimeSounds(kIds, kNames, 2) ? "setInstalledChimeSounds: OK" : "setInstalledChimeSounds: failed");

  Chime.setSelectedChime(1);
  Chime.setEnabled(true);

  Serial.println("FullAPI MatterChime ready; '?' for menu");
}

void printHelp() {
  Serial.println("c=setSelectedChime(cycle 1/2)  g=getSelectedChime");
  Serial.println("1=setEnabled(true) 0=setEnabled(false) s=getEnabled");
  Serial.println("v=setInstalledChimeSounds(replace with a new pair set)");
  Serial.println("a=policy:allow d=policy:deny ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what lets onPlayChime() run
   * inside the 1000 ms window at all. See the header comment above. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'c':
        {
          uint8_t next = (Chime.getSelectedChime() == 1) ? 2 : 1;
          Serial.print("setSelectedChime(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Chime.setSelectedChime(next) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("selectedChime: "); Serial.println(Chime.getSelectedChime()); break;
      case '1': Serial.println(Chime.setEnabled(true)  ? "setEnabled(true): OK"  : "setEnabled(true): failed");  break;
      case '0': Serial.println(Chime.setEnabled(false) ? "setEnabled(false): OK" : "setEnabled(false): failed"); break;
      case 's': Serial.print("enabled: "); Serial.println(Chime.getEnabled()); break;
      case 'v':
        {
          static const uint8_t kNewIds[1] = { 3 };
          static const char *kNewNames[1] = { "Chirp" };
          Serial.print("setInstalledChimeSounds(replace, 1 entry): ");
          Serial.println(Chime.setInstalledChimeSounds(kNewIds, kNewNames, 1) ? "OK" : "failed");
        }
        break;
      case 'a': policy = 'a'; Serial.println("policy: allow"); break;
      case 'd': policy = 'd'; Serial.println("policy: deny"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
