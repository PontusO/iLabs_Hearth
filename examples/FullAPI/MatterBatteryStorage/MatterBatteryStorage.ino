/*
 * FullAPI reference: MatterBatteryStorage
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * battery storage class at all (see the class header), so this is this
 * port's own design against the firmware's wire contract (energy round C1,
 * AT_MT_SPEC.md S3.17/S3.25/S3.26). Three surfaces ride one endpoint and
 * this sketch drives all three.
 *
 *   MatterBatteryStorage()         global object below
 *   begin(variant)                 setup(); declares only. FULL here; flip
 *                                  the #define below for NO_DEM
 *   -- the measurement surface (both variants) --
 *   setVoltage(mv)                 menu 'v'
 *   setActiveCurrent(ma)           menu 'c'
 *   setActivePower(mw)             menu 'p' (SIGNED: see the cycle below)
 *   setFrequency(mhz)              menu 'f'
 *   pushMeasurements(mv, ma, mw)   the charge/discharge cycle ('+'/'-')
 *   addEnergyImported(mwh)         the cycle while charging
 *   addEnergyExported(mwh)         the cycle while discharging
 *   getVoltage()                   menu 's'
 *   getActiveCurrent()             menu 's'
 *   getActivePower()               menu 's'
 *   getFrequency()                 menu 's'
 *   getEnergyImported()            menu 's'
 *   getEnergyExported()            menu 's'
 *   -- the ember PowerSource battery attributes (both variants) --
 *   setBatVoltage(mv)              the cycle (pack voltage tracks charge)
 *   setBatPercentRemaining(half)   the cycle, RAW half-percent (0..200)
 *   setBatTimeRemaining(s)         the cycle while discharging
 *   setBatChargeState(state)       the cycle: 1 IsCharging, 2 IsAtFullCharge,
 *                                  3 IsNotCharging
 *   setBatChargingCurrent(ma)      the cycle while charging
 *   setBatCapacity(mwh)            setup(), once: a nameplate figure. The
 *                                  library re-pushes it on every reconcile,
 *                                  because a value written once cannot
 *                                  repair itself after a C6 reboot the way
 *                                  the sampled readings below do
 *   setBatTimeToFullCharge(s)      the cycle while charging
 *   -- the DEM surface (FULL only) --
 *   setESAType(t)                  setup() (4 = BatteryStorage)
 *   setESACanGenerate(g)           setup() (true: this ESA can export)
 *   setESAState(s)                 menu 'o' (Offline) and 'O' (Online)
 *   setAbsMinPower(mw)             setup() (the discharge bound)
 *   setAbsMaxPower(mw)             setup() (the charge bound)
 *   setOptOutState(s)              menu 'x' (cycles the four opt-out states)
 *   pushAdjustmentEnergyUse(mwh)   loop(), when an adjustment ends
 *   setPowerAdjustmentCapability() setup() (two entries) and menu 'n' (null)
 *   onPowerAdjust(cb)              setup(); the verdict callback
 *   onCancelPowerAdjust(cb)        setup(); the verdict callback
 *   endAdjustment()                loop(), when the adjustment window expires
 *   PowerAdjustEntry               the capability list in setup()
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo
 *
 * THE CHARGE/DISCHARGE CYCLE, the point of the demo: '+' charges the pack
 * a step (positive ActivePower: energy entering the node, booked on the
 * IMPORTED counter, BatChargeState IsCharging, a charging current and a
 * time-to-full published), '-' discharges it a step (NEGATIVE ActivePower,
 * booked on EXPORTED, BatChargeState IsNotCharging, a time-remaining
 * published). The state of charge walks with it and drives BatVoltage and
 * BatPercentRemaining. Two different wire mechanisms are running side by
 * side here and the difference is worth watching on a bench: the
 * electrical fields are Instance-served pushes on AT+MTMEAS (no attribute
 * read path at all), the battery fields are ordinary ember attributes on
 * AT+MTATTR that a controller can read back and subscribe to.
 *
 * THE POWER-ADJUST CALLBACK NEVER TOUCHES THE WIRE, and that is not a
 * style choice: it runs inside the +MTCMD dispatch, where any wire write
 * is refused HEARTH_CMD_REENTRANT. onPowerAdjust() below therefore does
 * nothing but record the request and return a verdict; loop() is what
 * acts on it, and an accepted adjustment needs no push at all to start
 * (the firmware sets ESAState PowerAdjustActive and emits PowerAdjustStart
 * itself). When the window expires, loop() reports the energy used with
 * pushAdjustmentEnergyUse() and then calls endAdjustment(), which pushes
 * ESAState back to Online and is what makes the firmware emit
 * PowerAdjustEnd carrying that figure.
 *
 * VARIANTS. FULL carries the DEM triple. NO_DEM omits it and is still
 * conformant; every DEM call above then refuses host-side with
 * Hearth.lastError() == 1 and zero wire traffic, while the measurement and
 * battery surfaces keep working. Change the #define to watch it.
 *
 * Observe controller-side:
 *   chip-tool powersource read bat-voltage <node> <ep>
 *   chip-tool powersource read bat-percent-remaining <node> <ep>
 *   chip-tool powersource read bat-charge-state <node> <ep>
 *   chip-tool powersource read bat-time-remaining <node> <ep>
 *   chip-tool powersource read bat-time-to-full-charge <node> <ep>
 *   chip-tool powersource read bat-charging-current <node> <ep>
 *   chip-tool powersource read bat-capacity <node> <ep>
 *   chip-tool electricalpowermeasurement read active-power <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-imported <node> <ep>
 *   chip-tool deviceenergymanagement read esa-state <node> <ep>
 *   chip-tool deviceenergymanagement read power-adjustment-capability <node> <ep>
 *   chip-tool deviceenergymanagement power-adjust-request 5000000000 60 1 <node> <ep>
 *   chip-tool deviceenergymanagement cancel-power-adjust-request <node> <ep>
 *   chip-tool deviceenergymanagement read-event power-adjust-start <node> <ep>
 *   chip-tool deviceenergymanagement read-event power-adjust-end <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Battery.setBatVoltage(48000)) {
 *     // Hearth.lastError() holds the +MTERR code; see the README.
 *   }
 */
#include <Matter.h>

// Flip to MatterBatteryStorage::NO_DEM to declare the DEM-less variant and
// watch every DEM call refuse host-side.
#define BATTERY_VARIANT MatterBatteryStorage::FULL

MatterBatteryStorage Battery;

// Simulated pack: 13.5 kWh nameplate, state of charge in half-percent
// steps (the wire's own unit for BatPercentRemaining), and a signed load.
const uint32_t kCapacityMwh = 13500000;
uint8_t socHalfPercent = 100;  // 50%
long packWatts = 0;            // positive charges, negative discharges

// What the adjust callback recorded, for loop() to act on. Volatile is not
// needed (the callback runs from Hearth.poll(), on this same task), but the
// SEPARATION is: the callback must not touch the wire (see the banner).
bool adjustActive = false;
unsigned long adjustEndsAt = 0;
int64_t adjustPowerMw = 0;
int64_t adjustEnergyMwh = 0;

/* The verdict callbacks. Plain functions: no wire writes, no allocations,
 * just a decision and a note for loop(). Accepting an adjustment pushes
 * NOTHING here; the firmware owns the state transition and the
 * PowerAdjustStart event. */
bool onAdjust(int64_t powerMw, uint32_t durationS, uint8_t cause) {
  /* A real ESA decides on its own terms; this one accepts anything it
   * could physically deliver and refuses an implausibly long window. */
  if (durationS > 3600) {
    Serial.println("PowerAdjustRequest DENIED (window longer than an hour)");
    return false;
  }
  adjustActive = true;
  adjustPowerMw = powerMw;
  adjustEndsAt = millis() + (unsigned long)durationS * 1000UL;
  adjustEnergyMwh = 0;
  Serial.print("PowerAdjustRequest ACCEPTED: ");
  Serial.print((long)(powerMw / 1000));
  Serial.print(" W for ");
  Serial.print(durationS);
  Serial.print(" s, cause ");
  Serial.println(cause);
  return true;
}

bool onAdjustCancelled() {
  /* The firmware resets ESAState to Online and emits PowerAdjustEnd
   * (Cancelled) itself on an accept, so there is nothing to push here
   * either: just stop the simulated adjustment. */
  adjustActive = false;
  Serial.println("CancelPowerAdjustRequest ACCEPTED, adjustment abandoned");
  return true;
}

void pushCycleSample() {
  int64_t mv = 48000 + (int64_t)socHalfPercent * 20;  // pack voltage walks with SoC
  int64_t mw = (int64_t)packWatts * 1000;
  int64_t ma = (mw * 1000) / mv;
  Battery.pushMeasurements(mv, ma, mw);
  Battery.setBatVoltage((uint32_t)mv);
  Battery.setBatPercentRemaining(socHalfPercent);
  if (packWatts > 0) {
    Battery.setBatChargeState(socHalfPercent >= 200 ? 2 : 1);  // IsAtFullCharge / IsCharging
    Battery.setBatChargingCurrent((uint32_t)(ma < 0 ? -ma : ma));
    Battery.setBatTimeToFullCharge((uint32_t)((200 - socHalfPercent) * 60));
    Battery.addEnergyImported(250);
  } else if (packWatts < 0) {
    Battery.setBatChargeState(3);  // IsNotCharging
    Battery.setBatTimeRemaining((uint32_t)(socHalfPercent * 60));
    Battery.addEnergyExported(250);
  }
  if (adjustActive) {
    /* crude but honest: the adjustment's energy is whatever the pack moved
     * while it was running, and it is the figure PowerAdjustEnd reports */
    adjustEnergyMwh += (mw < 0 ? -mw : mw) / 3600;
  }
  Serial.print("cycle: ");
  Serial.print(packWatts);
  Serial.print(" W, SoC ");
  Serial.print(socHalfPercent / 2);
  Serial.println("%");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Battery.begin(BATTERY_VARIANT);
  Matter.begin();

  /* One-off nameplate figure, an ember attribute like the rest, and the
   * reason the class re-pushes this one on reconcile: nothing in this
   * sketch ever writes it again, so after a C6 reboot only the library
   * can restore it. */
  Battery.setBatCapacity(kCapacityMwh);

  // the DEM identity: what this ESA is and what it can physically do
  Battery.setESAType(4);            // BatteryStorage
  Battery.setESACanGenerate(true);  // it can export to the grid
  Battery.setAbsMinPower(-5000000); // 5 kW discharge
  Battery.setAbsMaxPower(5000000);  // 5 kW charge

  // what a controller is allowed to ask for: two adjustment envelopes
  const MatterBatteryStorage::PowerAdjustEntry caps[2] = {
    {-5000000, -1000000, 60, 3600},  // discharge 1..5 kW, 1..60 minutes
    {1000000, 5000000, 60, 3600},    // charge 1..5 kW, 1..60 minutes
  };
  if (!Battery.setPowerAdjustmentCapability(1, caps, 2)) {
    Serial.print("setPowerAdjustmentCapability refused (NO_DEM variant?), lastError=");
    Serial.println(Hearth.lastError());
  }

  Battery.onPowerAdjust(onAdjust);
  Battery.onCancelPowerAdjust(onAdjustCancelled);
  Battery.setESAState(1);  // Online

  Serial.println("FullAPI MatterBatteryStorage ready; '?' for menu");
}

void printHelp() {
  Serial.println("+=charge 250W harder and push  -=discharge 250W harder and push (goes NEGATIVE)");
  Serial.println("v=setVoltage(48000) c=setActiveCurrent(-25000) p=setActivePower(-1200000) f=setFrequency(50000)");
  Serial.println("o=setESAState(Offline) O=setESAState(Online) x=cycle setOptOutState n=capability to null s=status ?=help");
}

void loop() {
  /* Every iteration, unconditionally: this is also what dispatches the
   * +MTCMD power-adjust forwards into the callbacks above. */
  Hearth.poll();

  /* The adjustment's completion path, deliberately HERE and not in the
   * callback: report the energy used, then push ESAState back to Online,
   * which is what makes the firmware emit PowerAdjustEnd. A wire write
   * from inside the callback would be refused HEARTH_CMD_REENTRANT. */
  if (adjustActive && (long)(millis() - adjustEndsAt) >= 0) {
    adjustActive = false;
    Battery.pushAdjustmentEnergyUse(adjustEnergyMwh);
    Battery.endAdjustment();
    Serial.print("adjustment window expired: reported ");
    Serial.print((long)adjustEnergyMwh);
    Serial.println(" mWh and pushed ESAState Online (PowerAdjustEnd fires)");
  }

  if (Serial.available()) {
    static uint8_t optOut = 0;
    switch (Serial.read()) {
      case '+':
        packWatts += 250;
        if (socHalfPercent < 200) {
          socHalfPercent += 2;
        }
        pushCycleSample();
        break;
      case '-':
        packWatts -= 250;
        if (socHalfPercent > 0) {
          socHalfPercent -= 2;
        }
        pushCycleSample();
        break;
      case 'v': Serial.println(Battery.setVoltage(48000) ? "setVoltage(48000 mV): OK" : "setVoltage: failed"); break;
      case 'c': Serial.println(Battery.setActiveCurrent(-25000) ? "setActiveCurrent(-25000 mA): OK (discharging)" : "setActiveCurrent: failed"); break;
      case 'p': Serial.println(Battery.setActivePower(-1200000) ? "setActivePower(-1200000 mW): OK (discharging)" : "setActivePower: failed"); break;
      case 'f': Serial.println(Battery.setFrequency(50000) ? "setFrequency(50000 mHz): OK" : "setFrequency: failed"); break;
      case 'o': Serial.println(Battery.setESAState(0) ? "setESAState(0 Offline): OK" : "setESAState: refused (NO_DEM variant refuses host-side)"); break;
      case 'O': Serial.println(Battery.setESAState(1) ? "setESAState(1 Online): OK" : "setESAState: refused (NO_DEM variant refuses host-side)"); break;
      case 'x':
        optOut = (uint8_t)((optOut + 1) % 4);
        Serial.print("setOptOutState(");
        Serial.print(optOut);
        Serial.println(Battery.setOptOutState(optOut) ? "): OK" : "): refused (NO_DEM variant refuses host-side)");
        break;
      case 'n':
        Serial.println(
          Battery.setPowerAdjustmentCapability(0, nullptr, 0) ? "setPowerAdjustmentCapability(n=0): OK, capability reads null (every request now refused by the server)"
                                                              : "setPowerAdjustmentCapability: refused (NO_DEM variant refuses host-side)"
        );
        break;
      case 's':
        Serial.print("packWatts: ");
        Serial.println(packWatts);
        Serial.print("SoC(%): ");
        Serial.println(socHalfPercent / 2);
        Serial.print("voltage(mV): ");
        Serial.println((long)Battery.getVoltage());
        Serial.print("activeCurrent(mA): ");
        Serial.println((long)Battery.getActiveCurrent());
        Serial.print("activePower(mW): ");
        Serial.println((long)Battery.getActivePower());
        Serial.print("frequency(mHz): ");
        Serial.println((long)Battery.getFrequency());
        Serial.print("energyImported(mWh): ");
        Serial.println((unsigned long)Battery.getEnergyImported());
        Serial.print("energyExported(mWh): ");
        Serial.println((unsigned long)Battery.getEnergyExported());
        Serial.print("adjustment running: ");
        Serial.println(adjustActive ? "yes" : "no");
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
