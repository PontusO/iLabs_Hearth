/*
 * FullAPI reference: MatterElectricalSensor
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no electrical measurement class at all (see the class header), so
 * this is this port's own design against the firmware's AT+MTMEAS wire
 * contract (energy round A, AT_MT_SPEC.md S3.25).
 *
 *   Variant_t:  FULL=0 (power + energy clusters)
 *               POWER_ONLY=1 (power only, the current-clamp case; both
 *               variants are fully conformant on the SENSOR, whose device
 *               type XML lists the clusters as pick-at-least-one)
 *
 *   MatterElectricalSensor()       global object below
 *   begin(variant)                 setup(), FULL; declares only, every
 *                                  measurement stays null on the fabric
 *                                  until first pushed (see the class
 *                                  header: readings are volatile, nothing
 *                                  is reconciled or re-pushed)
 *   setVoltage(mv)                 menu 'v' (one field, one wire line)
 *   setActiveCurrent(ma)           menu 'c'
 *   setActivePower(mw)             menu 'p'
 *   setFrequency(mhz)              menu 'f'
 *   pushMeasurements(mv, ma, mw)   menu '+'/'-' (the ramp: one wire line,
 *                                  three field/value pairs, always sent,
 *                                  the normal per-sample path)
 *   addEnergyImported(mwh)         menu 'i' (accumulates host-side, pushes
 *                                  the new cumulative total; FULL only)
 *   addEnergyExported(mwh)         menu 'e' (same, exported counter)
 *   getVoltage()                   menu 's'
 *   getActiveCurrent()             menu 's'
 *   getActivePower()               menu 's'
 *   getFrequency()                 menu 's'
 *   getEnergyImported()            menu 's'
 *   getEnergyExported()            menu 's'
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo; call it
 *                                  when retiring the endpoint
 *
 * The ramp ('+'/'-') simulates a resistive load on 230 V mains in 10 W
 * steps: each step pushes voltage, current and power as ONE batched
 * AT+MTMEAS line, exactly what a real sampling loop should do (per-field
 * setters cost one wire round trip each). Units are the wire's own:
 * millivolts, milliamperes, milliwatts, millihertz, milliwatt-hours.
 *
 * Getters read the host-side last-pushed cache. There is no wire
 * read-back and no +MTATTR URC for any of these attributes (they are
 * Instance-served on the C6, AT_MT_SPEC.md S3.25): a controller can only
 * read them, never write them, so the values this sketch pushes are the
 * single source of truth.
 *
 * Observe controller-side (the power fields read null until pushed):
 *   chip-tool electricalpowermeasurement read voltage <node> <ep>
 *   chip-tool electricalpowermeasurement read active-current <node> <ep>
 *   chip-tool electricalpowermeasurement read active-power <node> <ep>
 *   chip-tool electricalpowermeasurement read frequency <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-imported <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-exported <node> <ep>
 *   chip-tool electricalenergymeasurement read-event cumulative-energy-measured <node> <ep>
 * The last one shows one CumulativeEnergyMeasured event per energy push,
 * timestamped by the firmware (S3.25's timestamp policy).
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Sensor.setVoltage(230000)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section. On POWER_ONLY,
 *     // addEnergyImported()/addEnergyExported() fail host-side with 1
 *     // and no wire traffic at all.
 *   }
 */
#include <Matter.h>

MatterElectricalSensor Sensor;

// Simulated resistive load, ramped from the menu. 230 V mains assumed.
int64_t loadWatts = 0;

void pushSample() {
  // I = P/U in mA: watts * 1000000 / 230000 mV, then P in mW.
  int64_t mv = 230000;
  int64_t mw = loadWatts * 1000;
  int64_t ma = (mv > 0) ? (mw * 1000) / mv : 0;
  if (Sensor.pushMeasurements(mv, ma, mw)) {
    Serial.print("pushMeasurements: ");
    Serial.print((long)loadWatts);
    Serial.println(" W sample pushed (one 3-pair AT+MTMEAS line)");
  } else {
    Serial.print("pushMeasurements failed, lastError=");
    Serial.println(Hearth.lastError());
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Sensor.begin(MatterElectricalSensor::FULL);  // declares only; all fields null on the fabric until pushed
  Matter.begin();
  Serial.println("FullAPI MatterElectricalSensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("+=ramp load +10W and push  -=ramp load -10W and push");
  Serial.println("v=setVoltage(231000) c=setActiveCurrent(450) p=setActivePower(103500) f=setFrequency(50000)");
  Serial.println("i=addEnergyImported(1000) e=addEnergyExported(250) s=status ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch (and the link's own
   * health) lives here even for a push-direction class like this one. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        loadWatts += 10;
        pushSample();
        break;
      case '-':
        if (loadWatts >= 10) {
          loadWatts -= 10;
        }
        pushSample();
        break;
      case 'v':
        Serial.println(Sensor.setVoltage(231000) ? "setVoltage(231000 mV): OK" : "setVoltage: failed");
        break;
      case 'c':
        Serial.println(Sensor.setActiveCurrent(450) ? "setActiveCurrent(450 mA): OK" : "setActiveCurrent: failed");
        break;
      case 'p':
        Serial.println(Sensor.setActivePower(103500) ? "setActivePower(103500 mW): OK" : "setActivePower: failed");
        break;
      case 'f':
        Serial.println(Sensor.setFrequency(50000) ? "setFrequency(50000 mHz): OK" : "setFrequency: failed");
        break;
      case 'i':
        Serial.println(Sensor.addEnergyImported(1000) ? "addEnergyImported(1000 mWh): OK (new total pushed, event fired)" : "addEnergyImported: failed");
        break;
      case 'e':
        Serial.println(Sensor.addEnergyExported(250) ? "addEnergyExported(250 mWh): OK (new total pushed, event fired)" : "addEnergyExported: failed");
        break;
      case 's':
        Serial.print("voltage(mV): ");
        Serial.println((long)Sensor.getVoltage());
        Serial.print("activeCurrent(mA): ");
        Serial.println((long)Sensor.getActiveCurrent());
        Serial.print("activePower(mW): ");
        Serial.println((long)Sensor.getActivePower());
        Serial.print("frequency(mHz): ");
        Serial.println((long)Sensor.getFrequency());
        Serial.print("energyImported(mWh): ");
        Serial.println((unsigned long)Sensor.getEnergyImported());
        Serial.print("energyExported(mWh): ");
        Serial.println((unsigned long)Sensor.getEnergyExported());
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
