/*
 * FullAPI reference: MatterHeatPump
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * heat pump class at all (see the class header), so this is this port's
 * own design against the firmware's wire contract (energy round B,
 * AT_MT_SPEC.md S3.25). The host surface is deliberately the shared
 * measurement-push surface plus identity and NOTHING else: PowerSource
 * (wired) exists at composition with no host surface, and the composed
 * Thermostat device type the XML mandates is a disclosed gap matching the
 * SDK's own build (the class header carries the conformance note).
 *
 *   MatterHeatPump()               global object below
 *   begin()                        setup(); declares only, every
 *                                  measurement stays null on the fabric
 *                                  until first pushed
 *   setVoltage(mv)                 menu 'v' (one field, one wire line)
 *   setActiveCurrent(ma)           menu 'c'
 *   setActivePower(mw)             menu 'p' (SIGNED: see the ramp below)
 *   setFrequency(mhz)              menu 'f'
 *   pushMeasurements(mv, ma, mw)   menu '+'/'-' (the ramp: one wire line,
 *                                  three field/value pairs per sample)
 *   addEnergyImported(mwh)         menu 'i' and the ramp while consuming
 *   addEnergyExported(mwh)         menu 'e' and the ramp while exporting
 *   getVoltage()                   menu 's'
 *   getActiveCurrent()             menu 's'
 *   getActivePower()               menu 's'
 *   getFrequency()                 menu 's'
 *   getEnergyImported()            menu 's'
 *   getEnergyExported()            menu 's'
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo
 *
 * THE RAMP IS SIGNED, the point of this demo: '+' steps the compressor
 * load up in 100 W steps (heating: the pump CONSUMES, positive
 * milliwatts, energy accumulates on the imported counter), '-' steps it
 * down and PAST ZERO (a grid-feeding defrost/export phase: negative
 * milliwatts, energy accumulates on the exported counter). The sign
 * travels the 64-bit pipeline end to end; current follows the power's
 * sign at 230 V mains.
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
 *   if (!HeatPump.setActivePower(-500000)) {
 *     // Hearth.lastError() holds the +MTERR code; see the README.
 *   }
 */
#include <Matter.h>

MatterHeatPump HeatPump;

// Simulated compressor load in watts, SIGNED: positive consumes (heating),
// negative exports (the defrost/grid-feed phase of the demo).
long loadWatts = 0;

void pushSample() {
  int64_t mv = 230000;
  int64_t mw = (int64_t)loadWatts * 1000;
  int64_t ma = (mw * 1000) / mv;  // sign follows the power
  if (HeatPump.pushMeasurements(mv, ma, mw)) {
    Serial.print("pushMeasurements: ");
    Serial.print(loadWatts);
    Serial.println(" W sample pushed (one 3-pair AT+MTMEAS line, signed)");
  } else {
    Serial.print("pushMeasurements failed, lastError=");
    Serial.println(Hearth.lastError());
  }
  // energy follows the direction: a 100 W step held for one menu press is
  // booked as a demo-sized 100 mWh parcel on the matching counter
  if (loadWatts > 0) {
    HeatPump.addEnergyImported(100);
  } else if (loadWatts < 0) {
    HeatPump.addEnergyExported(100);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  HeatPump.begin();  // declares only; all fields null on the fabric until pushed
  Matter.begin();
  Serial.println("FullAPI MatterHeatPump ready; '?' for menu");
}

void printHelp() {
  Serial.println("+=ramp +100W and push  -=ramp -100W and push (goes NEGATIVE: export)");
  Serial.println("v=setVoltage(231000) c=setActiveCurrent(6520) p=setActivePower(-500000) f=setFrequency(50000)");
  Serial.println("i=addEnergyImported(1000) e=addEnergyExported(250) s=status ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch (and the link's own
   * health) lives here even for a push-direction class like this one. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        loadWatts += 100;
        pushSample();
        break;
      case '-':
        loadWatts -= 100;
        pushSample();
        break;
      case 'v': Serial.println(HeatPump.setVoltage(231000) ? "setVoltage(231000 mV): OK" : "setVoltage: failed"); break;
      case 'c': Serial.println(HeatPump.setActiveCurrent(6520) ? "setActiveCurrent(6520 mA): OK" : "setActiveCurrent: failed"); break;
      case 'p': Serial.println(HeatPump.setActivePower(-500000) ? "setActivePower(-500000 mW): OK (signed)" : "setActivePower: failed"); break;
      case 'f': Serial.println(HeatPump.setFrequency(50000) ? "setFrequency(50000 mHz): OK" : "setFrequency: failed"); break;
      case 'i': Serial.println(HeatPump.addEnergyImported(1000) ? "addEnergyImported(1000 mWh): OK (new total pushed, event fired)" : "addEnergyImported: failed"); break;
      case 'e': Serial.println(HeatPump.addEnergyExported(250) ? "addEnergyExported(250 mWh): OK (new total pushed, event fired)" : "addEnergyExported: failed"); break;
      case 's':
        Serial.print("load(W): ");
        Serial.println(loadWatts);
        Serial.print("voltage(mV): ");
        Serial.println((long)HeatPump.getVoltage());
        Serial.print("activeCurrent(mA): ");
        Serial.println((long)HeatPump.getActiveCurrent());
        Serial.print("activePower(mW): ");
        Serial.println((long)HeatPump.getActivePower());
        Serial.print("frequency(mHz): ");
        Serial.println((long)HeatPump.getFrequency());
        Serial.print("energyImported(mWh): ");
        Serial.println((unsigned long)HeatPump.getEnergyImported());
        Serial.print("energyExported(mWh): ");
        Serial.println((unsigned long)HeatPump.getEnergyExported());
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
