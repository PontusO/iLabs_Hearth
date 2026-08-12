/*
 * FullAPI reference: MatterSolarPower
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * solar class at all (see the class header), so this is this port's own
 * design against the firmware's wire contract (energy round C1,
 * AT_MT_SPEC.md S3.25). The host surface is deliberately the shared
 * measurement-push surface plus identity and NOTHING else: PowerSource
 * (wired) exists at composition with no host surface, and the composed
 * Electrical Sensor is what the pushes below actually reach.
 *
 *   MatterSolarPower()             global object below
 *   begin(variant)                 setup(); declares only, every
 *                                  measurement stays null on the fabric
 *                                  until first pushed. FULL here; flip the
 *                                  #define below for NO_ENERGY
 *   setVoltage(mv)                 menu 'v' (one field, one wire line)
 *   setActiveCurrent(ma)           menu 'c'
 *   setActivePower(mw)             menu 'p' (SIGNED: see the ramp below)
 *   setFrequency(mhz)              menu 'f'
 *   pushMeasurements(mv, ma, mw)   menu '+'/'-' (the ramp: one wire line,
 *                                  three field/value pairs per sample)
 *   addEnergyImported(mwh)         menu 'i' and the ramp at night
 *   addEnergyExported(mwh)         menu 'e' and the ramp while generating
 *   getVoltage()                   menu 's'
 *   getActiveCurrent()             menu 's'
 *   getActivePower()               menu 's'
 *   getFrequency()                 menu 's'
 *   getEnergyImported()            menu 's'
 *   getEnergyExported()            menu 's'
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo
 *
 * THE RAMP IS SIGNED, and for this device type the sign IS the state: '+'
 * steps the array's output up in 250 W steps, which is energy LEAVING the
 * node, so ActivePower goes NEGATIVE and the energy lands on the EXPORTED
 * counter; '-' steps it back down and past zero into the small positive
 * draw a string inverter takes from the house at night, which books on the
 * IMPORTED counter. The sign travels the 64-bit pipeline end to end;
 * current follows the power's sign at a 415 V three-phase string voltage.
 *
 * VARIANTS. FULL (the default here) grafts the composed Electrical Sensor
 * with its energy cluster. NO_ENERGY is the current-clamp shape: no energy
 * cluster is built at all, so addEnergyImported()/addEnergyExported()
 * refuse host-side with Hearth.lastError() == 1 and never touch the wire,
 * while every power-side setter keeps working. Change the #define below to
 * watch that happen: the menu prints the refusal instead of a push.
 *
 * Observe controller-side (fields read null until pushed):
 *   chip-tool electricalpowermeasurement read voltage <node> <ep>
 *   chip-tool electricalpowermeasurement read active-current <node> <ep>
 *   chip-tool electricalpowermeasurement read active-power <node> <ep>
 *   chip-tool electricalpowermeasurement read frequency <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-imported <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-exported <node> <ep>
 *   chip-tool electricalenergymeasurement read-event cumulative-energy-measured <node> <ep>
 *   chip-tool powersource read feature-map <node> <ep>   (wired, no host surface)
 * The read-event line shows one CumulativeEnergyMeasured event per energy
 * push, timestamped by the firmware (S3.25's timestamp policy).
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Solar.setActivePower(-4980000)) {
 *     // Hearth.lastError() holds the +MTERR code; see the README.
 *   }
 */
#include <Matter.h>

// Flip to MatterSolarPower::NO_ENERGY to declare the current-clamp variant
// and watch the two energy adders refuse host-side.
#define SOLAR_VARIANT MatterSolarPower::FULL

MatterSolarPower Solar;

// Simulated array output in watts, SIGNED as the cluster wants it:
// negative generates (energy leaving), positive is the night-time draw.
long outputWatts = 0;

void pushSample() {
  int64_t mv = 415000;                            // 415 V three-phase string
  int64_t mw = -(int64_t)outputWatts * 1000;      // generating = negative
  int64_t ma = (mw * 1000) / mv;                  // sign follows the power
  if (Solar.pushMeasurements(mv, ma, mw)) {
    Serial.print("pushMeasurements: ");
    Serial.print(outputWatts);
    Serial.println(" W of generation pushed (one 3-pair AT+MTMEAS line, signed)");
  } else {
    Serial.print("pushMeasurements failed, lastError=");
    Serial.println(Hearth.lastError());
  }
  /* Energy follows the direction: a 250 W step held for one menu press is
   * booked as a demo-sized 250 mWh parcel on the matching counter. On the
   * NO_ENERGY variant both adders refuse host-side without touching the
   * wire, which is the variant's whole point. */
  if (outputWatts > 0) {
    if (!Solar.addEnergyExported(250)) {
      Serial.print("addEnergyExported refused (NO_ENERGY variant?), lastError=");
      Serial.println(Hearth.lastError());
    }
  } else if (outputWatts < 0) {
    Solar.addEnergyImported(250);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Solar.begin(SOLAR_VARIANT);  // declares only; all fields null until pushed
  Matter.begin();
  Serial.println("FullAPI MatterSolarPower ready; '?' for menu");
}

void printHelp() {
  Serial.println("+=generate 250W more and push  -=generate 250W less and push (goes past zero: night draw)");
  Serial.println("v=setVoltage(414000) c=setActiveCurrent(-12000) p=setActivePower(-4980000) f=setFrequency(50000)");
  Serial.println("i=addEnergyImported(500) e=addEnergyExported(2000) s=status ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch (and the link's own
   * health) lives here even for a push-direction class like this one. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        outputWatts += 250;
        pushSample();
        break;
      case '-':
        outputWatts -= 250;
        pushSample();
        break;
      case 'v': Serial.println(Solar.setVoltage(414000) ? "setVoltage(414000 mV): OK" : "setVoltage: failed"); break;
      case 'c': Serial.println(Solar.setActiveCurrent(-12000) ? "setActiveCurrent(-12000 mA): OK (signed)" : "setActiveCurrent: failed"); break;
      case 'p': Serial.println(Solar.setActivePower(-4980000) ? "setActivePower(-4980000 mW): OK (generating)" : "setActivePower: failed"); break;
      case 'f': Serial.println(Solar.setFrequency(50000) ? "setFrequency(50000 mHz): OK" : "setFrequency: failed"); break;
      case 'i':
        Serial.println(Solar.addEnergyImported(500) ? "addEnergyImported(500 mWh): OK (new total pushed, event fired)" : "addEnergyImported: refused (NO_ENERGY variant refuses host-side)");
        break;
      case 'e':
        Serial.println(Solar.addEnergyExported(2000) ? "addEnergyExported(2000 mWh): OK (new total pushed, event fired)" : "addEnergyExported: refused (NO_ENERGY variant refuses host-side)");
        break;
      case 's':
        Serial.print("generation(W): ");
        Serial.println(outputWatts);
        Serial.print("voltage(mV): ");
        Serial.println((long)Solar.getVoltage());
        Serial.print("activeCurrent(mA): ");
        Serial.println((long)Solar.getActiveCurrent());
        Serial.print("activePower(mW): ");
        Serial.println((long)Solar.getActivePower());
        Serial.print("frequency(mHz): ");
        Serial.println((long)Solar.getFrequency());
        Serial.print("energyImported(mWh): ");
        Serial.println((unsigned long)Solar.getEnergyImported());
        Serial.print("energyExported(mWh): ");
        Serial.println((unsigned long)Solar.getEnergyExported());
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
