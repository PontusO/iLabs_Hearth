/*
 * FullAPI reference: MatterElectricalMeter
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no electrical meter class at all (see the class header), so this
 * is this port's own design against the firmware's AT+MTMEAS wire
 * contract (energy round A, AT_MT_SPEC.md S3.25).
 *
 * The meter subclasses MatterElectricalSensor and inherits its entire
 * surface (see MatterElectricalMeter.h: on the wire the two types differ
 * only in device type id and the sensor's PowerTopology cluster, which
 * has no host surface). This sketch therefore exercises the inherited
 * members through the meter type, with the ACCUMULATION driven by a
 * millis() timer rather than the sensor sketch's menu ramp: every
 * METER_TICK_MS the sketch integrates the simulated load into an energy
 * delta and calls addEnergyImported(), which is the shape a real meter
 * firmware has (a periodic sampling loop, not a keypress).
 *
 *   Variant_t:  FULL=0 (power + energy), POWER_ONLY=1. CONFORMANCE NOTE
 *               (class header): spec 1.5.1 marks BOTH measurement
 *               clusters mandatory on the Electrical Meter, so a
 *               POWER_ONLY meter is permissive-beyond-conformance and
 *               exists for variant scheme symmetry only; the conformant
 *               power-only declaration is the POWER_ONLY *sensor*.
 *
 *   MatterElectricalMeter()        global object below
 *   begin(variant)                 setup(), FULL (declares 0x0514)
 *   pushMeasurements(mv, ma, mw)   the timer tick, one 3-pair line per
 *                                  sample
 *   addEnergyImported(mwh)         the timer tick, when the net load
 *                                  direction is import (grid -> house)
 *   addEnergyExported(mwh)         the timer tick, when direction is
 *                                  export ('x' toggles, the solar case)
 *   setVoltage(mv)                 menu 'v'
 *   setActiveCurrent(ma)           menu 'c'
 *   setActivePower(mw)             menu 'p'
 *   setFrequency(mhz)              menu 'f'
 *   getVoltage()                   menu 's'
 *   getActiveCurrent()             menu 's'
 *   getActivePower()               menu 's'
 *   getFrequency()                 menu 's'
 *   getEnergyImported()            menu 's', and each tick's log line
 *   getEnergyExported()            menu 's', and each tick's log line
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo; call it
 *                                  when retiring the endpoint
 *
 * The energy adders accumulate HOST-side and push the new cumulative
 * total (mWh); the firmware wraps each pushed total in a timestamped
 * measurement struct and emits a CumulativeEnergyMeasured event per push
 * (S3.25), so the timer below is directly observable as a stream of
 * events controller-side. The accumulators restart at 0 on a host
 * reboot; a real meter persists lifetime totals itself (Preferences) and
 * seeds the first add with the restored value.
 *
 * Observe controller-side:
 *   chip-tool electricalpowermeasurement read active-power <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-imported <node> <ep>
 *   chip-tool electricalenergymeasurement read cumulative-energy-exported <node> <ep>
 *   chip-tool electricalenergymeasurement read-event cumulative-energy-measured <node> <ep>
 * The read-event capture grows by one CumulativeEnergyMeasured event per
 * timer tick; each event carries the pushed cumulative struct with the
 * firmware's own measurement-period timestamps.
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Meter.addEnergyImported(delta)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); on a POWER_ONLY declaration the energy adders fail
 *     // host-side with 1 and no wire traffic at all.
 *   }
 */
#include <Matter.h>

MatterElectricalMeter Meter;

// Simulated load and accumulation timer. 230 V mains assumed.
#define METER_TICK_MS 5000
int64_t loadWatts = 100;
bool exporting = false;  // 'x' toggles: import (grid -> house) or export (solar -> grid)
unsigned long lastTick = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Meter.begin(MatterElectricalMeter::FULL);  // declares 0x0514; fields null on the fabric until pushed
  Matter.begin();
  Serial.println("FullAPI MatterElectricalMeter ready; '?' for menu (accumulates every 5 s)");
  lastTick = millis();
}

void printHelp() {
  Serial.println("+=load +100W -=load -100W x=toggle import/export direction");
  Serial.println("v=setVoltage(231000) c=setActiveCurrent(450) p=setActivePower(103500) f=setFrequency(50000)");
  Serial.println("s=status ?=help  (energy accumulates on a 5 s timer)");
}

void meterTick() {
  // One sample: push the instantaneous readings as one 3-pair line, then
  // integrate the interval into the right cumulative counter. Sign
  // convention: both counters are unsigned, direction picks the counter.
  int64_t mv = 230000;
  int64_t mw = loadWatts * 1000 * (exporting ? -1 : 1);
  int64_t ma = (mw * 1000) / mv;
  if (!Meter.pushMeasurements(mv, ma, mw)) {
    Serial.print("pushMeasurements failed, lastError=");
    Serial.println(Hearth.lastError());
  }
  // mWh over METER_TICK_MS: mW * ms / 3600000.
  uint64_t deltaMwh = (uint64_t)(loadWatts * 1000) * METER_TICK_MS / 3600000ULL;
  bool ok = exporting ? Meter.addEnergyExported(deltaMwh) : Meter.addEnergyImported(deltaMwh);
  if (ok) {
    Serial.print(exporting ? "tick: exported +" : "tick: imported +");
    Serial.print((unsigned long)deltaMwh);
    Serial.print(" mWh, totals imported=");
    Serial.print((unsigned long)Meter.getEnergyImported());
    Serial.print(" exported=");
    Serial.print((unsigned long)Meter.getEnergyExported());
    Serial.println(" (CumulativeEnergyMeasured event fired)");
  } else {
    Serial.print("energy add failed, lastError=");
    Serial.println(Hearth.lastError());
  }
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch (and the link's own
   * health) lives here even for a push-direction class like this one. */
  Hearth.poll();

  if (millis() - lastTick >= METER_TICK_MS) {
    lastTick += METER_TICK_MS;
    meterTick();
  }

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        loadWatts += 100;
        Serial.print("load: ");
        Serial.print((long)loadWatts);
        Serial.println(" W");
        break;
      case '-':
        if (loadWatts >= 100) {
          loadWatts -= 100;
        }
        Serial.print("load: ");
        Serial.print((long)loadWatts);
        Serial.println(" W");
        break;
      case 'x':
        exporting = !exporting;
        Serial.println(exporting ? "direction: exporting (solar -> grid)" : "direction: importing (grid -> house)");
        break;
      case 'v':
        Serial.println(Meter.setVoltage(231000) ? "setVoltage(231000 mV): OK" : "setVoltage: failed");
        break;
      case 'c':
        Serial.println(Meter.setActiveCurrent(450) ? "setActiveCurrent(450 mA): OK" : "setActiveCurrent: failed");
        break;
      case 'p':
        Serial.println(Meter.setActivePower(103500) ? "setActivePower(103500 mW): OK" : "setActivePower: failed");
        break;
      case 'f':
        Serial.println(Meter.setFrequency(50000) ? "setFrequency(50000 mHz): OK" : "setFrequency: failed");
        break;
      case 's':
        Serial.print("voltage(mV): ");
        Serial.println((long)Meter.getVoltage());
        Serial.print("activeCurrent(mA): ");
        Serial.println((long)Meter.getActiveCurrent());
        Serial.print("activePower(mW): ");
        Serial.println((long)Meter.getActivePower());
        Serial.print("frequency(mHz): ");
        Serial.println((long)Meter.getFrequency());
        Serial.print("energyImported(mWh): ");
        Serial.println((unsigned long)Meter.getEnergyImported());
        Serial.print("energyExported(mWh): ");
        Serial.println((unsigned long)Meter.getEnergyExported());
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
