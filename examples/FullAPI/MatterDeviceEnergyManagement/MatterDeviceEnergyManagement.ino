/*
 * FullAPI reference: MatterDeviceEnergyManagement
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * DEM class at all (see the class header), so this is this port's own
 * design against the firmware's wire contract (energy round C1,
 * AT_MT_SPEC.md S3.17/S3.25/S3.26). The sketch is a CONTROLLABLE-LOAD
 * SIMULATOR: a pretend appliance drawing 2 kW that a grid controller can
 * ask to turn itself down for a while.
 *
 *   MatterDeviceEnergyManagement() global object below
 *   begin(variant)                 setup(); declares only. CONTROLLABLE
 *                                  here; flip the #define for REPORT_ONLY
 *   setESAType(t)                  setup() (5 = SmartAppliance)
 *   setESACanGenerate(g)           setup() (false: this load only consumes)
 *   setESAState(s)                 menu 'o' (Offline) / 'O' (Online)
 *   setAbsMinPower(mw)             setup() (0: it cannot export)
 *   setAbsMaxPower(mw)             setup() (2 kW: its full draw)
 *   setOptOutState(s)              menu 'x' (cycles the four opt-out states)
 *   pushAdjustmentEnergyUse(mwh)   loop(), when the adjustment ends
 *   setPowerAdjustmentCapability() setup() (one envelope) and menu 'n' (null)
 *   onPowerAdjust(cb)              setup(); the verdict callback
 *   onCancelPowerAdjust(cb)        setup(); the verdict callback
 *   endAdjustment()                loop(), when the window expires
 *   PowerAdjustEntry               the capability list in setup()
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo
 *
 * THE LOAD SIMULATOR NEVER WRITES THE WIRE FROM INSIDE A CALLBACK, and
 * that is the shape to copy, not an incidental detail. onPowerAdjust()
 * runs inside the +MTCMD dispatch, where any wire write is refused
 * HEARTH_CMD_REENTRANT (the library's own re-entrancy guard). So the
 * callback does exactly two things: it decides, and it records the request
 * in plain variables. loop() is what acts on the record, one poll later:
 * it drops the simulated load to the requested power, counts the energy,
 * and when the window expires it calls pushAdjustmentEnergyUse() followed
 * by endAdjustment().
 *
 * An accepted request needs NO push at all to start: the firmware sets
 * ESAState to PowerAdjustActive and emits the fieldless PowerAdjustStart
 * itself, because on an accept that transition is unconditional. What the
 * host owns is the END of the adjustment: endAdjustment() pushes ESAState
 * back to Online, and THAT is what makes the firmware emit PowerAdjustEnd
 * (NormalCompletion, the measured duration, and the last energy figure
 * pushed). An accepted CancelPowerAdjustRequest ends it the other way
 * (PowerAdjustEnd with Cancelled), again firmware-side, so that callback
 * only stops the simulation.
 *
 * VARIANTS. CONTROLLABLE (the default here) builds the PowerAdjustment
 * feature: everything above works. REPORT_ONLY builds the same cluster
 * with FeatureMap 0, which is equally conformant and is a real product
 * shape: an ESA that reports its state and cannot be told what to do.
 * Exactly one call changes behaviour: setPowerAdjustmentCapability()
 * refuses host-side with Hearth.lastError() == 1 and never touches the
 * wire, because that variant serves no such attribute. Every state and
 * identity push keeps working, and a controller's power-adjust-request is
 * answered UnsupportedCommand by the C6 without this host being woken at
 * all, so the callbacks below simply never fire.
 *
 * Observe controller-side:
 *   chip-tool deviceenergymanagement read esa-type <node> <ep>
 *   chip-tool deviceenergymanagement read esa-can-generate <node> <ep>
 *   chip-tool deviceenergymanagement read esa-state <node> <ep>
 *   chip-tool deviceenergymanagement read abs-min-power <node> <ep>
 *   chip-tool deviceenergymanagement read abs-max-power <node> <ep>
 *   chip-tool deviceenergymanagement read opt-out-state <node> <ep>
 *   chip-tool deviceenergymanagement read power-adjustment-capability <node> <ep>
 *   chip-tool deviceenergymanagement power-adjust-request 1500000 120 1 <node> <ep>
 *   chip-tool deviceenergymanagement cancel-power-adjust-request <node> <ep>
 *   chip-tool deviceenergymanagement read-event power-adjust-start <node> <ep>
 *   chip-tool deviceenergymanagement read-event power-adjust-end <node> <ep>
 * The two read-event lines are the pair to watch across one full cycle:
 * Start fires on the accept (fieldless), End fires when this sketch pushes
 * ESAState Online again, carrying the cause, the measured duration and the
 * energy figure pushed just before it.
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Dem.setESAState(1)) {
 *     // Hearth.lastError() holds the +MTERR code; see the README.
 *   }
 */
#include <Matter.h>

// Flip to MatterDeviceEnergyManagement::REPORT_ONLY to declare the
// non-controllable variant and watch the capability setter refuse.
#define DEM_VARIANT MatterDeviceEnergyManagement::CONTROLLABLE

MatterDeviceEnergyManagement Dem;

// The simulated appliance: 2 kW nominal, in milliwatts.
const int64_t kNominalPowerMw = 2000000;
int64_t currentLoadMw = kNominalPowerMw;

// What the callback recorded, for loop() to act on. The SEPARATION is the
// point (see the banner): the callback decides, loop() touches the wire.
bool adjustPending = false;   // accepted, not yet applied by loop()
bool adjustActive = false;    // applied, window running
int64_t adjustTargetMw = 0;
unsigned long adjustEndsAt = 0;
unsigned long adjustLastTick = 0;
int64_t adjustEnergyMwh = 0;

/*
 * The verdict callback. NO WIRE WRITES HERE: this runs inside the +MTCMD
 * dispatch and a write would be refused HEARTH_CMD_REENTRANT. It decides
 * and records; loop() applies. The CHIP server has already checked the
 * request against the capability list published in setup(), so anything
 * arriving here is inside an envelope this ESA advertised, and the only
 * question left is whether the appliance wants to co-operate right now.
 */
bool onAdjust(int64_t powerMw, uint32_t durationS, uint8_t cause) {
  if (durationS == 0 || durationS > 3600) {
    Serial.println("PowerAdjustRequest DENIED (window out of range)");
    return false;
  }
  adjustPending = true;
  adjustTargetMw = powerMw;
  adjustEndsAt = millis() + (unsigned long)durationS * 1000UL;
  adjustEnergyMwh = 0;
  Serial.print("PowerAdjustRequest ACCEPTED: hold ");
  Serial.print((long)(powerMw / 1000));
  Serial.print(" W for ");
  Serial.print(durationS);
  Serial.print(" s, cause ");
  Serial.print(cause);
  Serial.println(" (the firmware sets ESAState PowerAdjustActive and fires PowerAdjustStart)");
  return true;
}

bool onAdjustCancelled() {
  /* Again no wire write: on an accept the firmware resets ESAState to
   * Online itself and emits PowerAdjustEnd(Cancelled). Just stop. */
  adjustPending = false;
  adjustActive = false;
  currentLoadMw = kNominalPowerMw;
  Serial.println("CancelPowerAdjustRequest ACCEPTED, load back to nominal");
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Dem.begin(DEM_VARIANT);
  Matter.begin();

  // identity: what this ESA is and what it can physically do
  Dem.setESAType(5);             // SmartAppliance
  Dem.setESACanGenerate(false);  // a load, never a source
  Dem.setAbsMinPower(0);
  Dem.setAbsMaxPower(kNominalPowerMw);

  // what a controller may ask for: turn down to between 0.5 kW and the
  // full 2 kW, for one to sixty minutes
  const MatterDeviceEnergyManagement::PowerAdjustEntry caps[1] = {
    {500000, kNominalPowerMw, 60, 3600},
  };
  if (!Dem.setPowerAdjustmentCapability(1, caps, 1)) {
    Serial.print("setPowerAdjustmentCapability refused (REPORT_ONLY variant?), lastError=");
    Serial.println(Hearth.lastError());
  }

  Dem.onPowerAdjust(onAdjust);
  Dem.onCancelPowerAdjust(onAdjustCancelled);
  Dem.setESAState(1);  // Online

  Serial.println("FullAPI MatterDeviceEnergyManagement ready; '?' for menu");
}

void printHelp() {
  Serial.println("o=setESAState(Offline) O=setESAState(Online) x=cycle setOptOutState");
  Serial.println("n=capability to null (every request then refused by the server) c=restore the capability list");
  Serial.println("e=push a 500 mWh adjustment energy figure by hand  E=endAdjustment() by hand  s=status ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is what dispatches the +MTCMD
   * power-adjust forwards into the callbacks above. */
  Hearth.poll();

  /* Apply an accepted request, one poll after the callback recorded it.
   * Nothing is pushed to the wire for the START of an adjustment: the
   * firmware already did that (see the banner). */
  if (adjustPending) {
    adjustPending = false;
    adjustActive = true;
    currentLoadMw = adjustTargetMw;
    adjustLastTick = millis();
    Serial.print("load simulator: holding ");
    Serial.print((long)(currentLoadMw / 1000));
    Serial.println(" W");
  }

  /* Count the energy the adjustment actually used, once a second. */
  if (adjustActive && (long)(millis() - adjustLastTick) >= 1000) {
    adjustLastTick += 1000;
    adjustEnergyMwh += currentLoadMw / 3600;  // one second of mW = mWh/3600
  }

  /* The completion path, and the one place this sketch touches the DEM
   * wire on its own initiative: report the energy, then push ESAState
   * Online, which is what makes the firmware emit PowerAdjustEnd. */
  if (adjustActive && (long)(millis() - adjustEndsAt) >= 0) {
    adjustActive = false;
    currentLoadMw = kNominalPowerMw;
    Dem.pushAdjustmentEnergyUse(adjustEnergyMwh);
    Dem.endAdjustment();
    Serial.print("window expired: reported ");
    Serial.print((long)adjustEnergyMwh);
    Serial.println(" mWh, pushed ESAState Online (PowerAdjustEnd fires), load back to nominal");
  }

  if (Serial.available()) {
    static uint8_t optOut = 0;
    switch (Serial.read()) {
      case 'o': Serial.println(Dem.setESAState(0) ? "setESAState(0 Offline): OK" : "setESAState: failed"); break;
      case 'O': Serial.println(Dem.setESAState(1) ? "setESAState(1 Online): OK" : "setESAState: failed"); break;
      case 'x':
        optOut = (uint8_t)((optOut + 1) % 4);
        Serial.print("setOptOutState(");
        Serial.print(optOut);
        Serial.println(Dem.setOptOutState(optOut) ? "): OK" : "): failed");
        break;
      case 'n':
        Serial.println(
          Dem.setPowerAdjustmentCapability(0, nullptr, 0) ? "setPowerAdjustmentCapability(n=0): OK, capability reads null"
                                                          : "setPowerAdjustmentCapability: refused (REPORT_ONLY refuses host-side, zero wire traffic)"
        );
        break;
      case 'c': {
        const MatterDeviceEnergyManagement::PowerAdjustEntry caps[1] = {
          {500000, kNominalPowerMw, 60, 3600},
        };
        Serial.println(
          Dem.setPowerAdjustmentCapability(1, caps, 1) ? "setPowerAdjustmentCapability(n=1): OK, one envelope advertised"
                                                       : "setPowerAdjustmentCapability: refused (REPORT_ONLY refuses host-side, zero wire traffic)"
        );
        break;
      }
      case 'e': Serial.println(Dem.pushAdjustmentEnergyUse(500) ? "pushAdjustmentEnergyUse(500 mWh): OK (carried by the next PowerAdjustEnd)" : "pushAdjustmentEnergyUse: failed"); break;
      case 'E':
        adjustActive = false;
        currentLoadMw = kNominalPowerMw;
        Serial.println(Dem.endAdjustment() ? "endAdjustment(): OK, ESAState Online pushed (PowerAdjustEnd fires if one was running)" : "endAdjustment: failed");
        break;
      case 's':
        Serial.print("load(W): ");
        Serial.println((long)(currentLoadMw / 1000));
        Serial.print("adjustment running: ");
        Serial.println(adjustActive ? "yes" : "no");
        Serial.print("adjustment energy so far(mWh): ");
        Serial.println((long)adjustEnergyMwh);
        Serial.print("optOutState: ");
        Serial.println(optOut);
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
