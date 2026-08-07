/*
 * FullAPI reference: MatterOccupancySensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 * OccupancySensorType_t has four values, chosen once at begin() (there
 * is no runtime setter for it in this header, so the menu cannot switch
 * it live): OCCUPANCY_SENSOR_TYPE_PIR=0, _ULTRASONIC=1,
 * _PIR_AND_ULTRASONIC=2, _PHYSICAL_CONTACT=3. This sketch begin()s with
 * a non-default value (_PIR_AND_ULTRASONIC) so the constant actually
 * exercised is not the header's own default.
 *
 * setHoldTime() and setHoldTimeLimits() are PARKED (README's "Parked"
 * section): HoldTime/HoldTimeLimits are AttributeAccessInterface
 * territory in the firmware, not reachable over AT+MTATTR, so both
 * always return false. The menu calls them anyway, to demonstrate the
 * documented failure. onHoldTimeChange()'s callback is stored but never
 * fired for the same reason; this sketch registers it anyway, for
 * coverage, with a comment explaining it will never print.
 *
 *   MatterOccupancySensor()            global object below
 *   OccupancySensorType_t              begin()'s 2nd arg, see note above
 *   begin(bool, OccupancySensorType_t) setup()
 *   setOccupancy(bool)                 menu 't' (toggle)
 *   getOccupancy()                     menu 's'
 *   setHoldTime(uint16_t)              menu 'h' (always false, parked)
 *   getHoldTime()                      menu 'l'
 *   setHoldTimeLimits(u16,u16,u16)     menu 'm' (always false, parked)
 *   onHoldTimeChange(cb)               setup(), never fires (parked)
 *   onChange(cb)                       setup(), prints the new state
 *   end()                              not called: tearing the endpoint
 *                                      down mid-demo is not a usable
 *                                      demo; call it when retiring the
 *                                      endpoint
 *
 * Observe controller-side:
 *   chip-tool occupancysensing read occupancy <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Occ.setOccupancy(true)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterOccupancySensor Occ;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Occ.onChange([](bool occupancy) {
    Serial.print("onChange: ");
    Serial.println(occupancy);
    return true;
  });
  Occ.onHoldTimeChange([](uint16_t holdTime_seconds) {
    /* Never fires: HoldTime changes are not exposed over AT+MTATTR
     * (README's "Parked" section). Registered here for API coverage. */
    Serial.print("onHoldTimeChange: ");
    Serial.println(holdTime_seconds);
    return true;
  });

  Occ.begin(false, MatterOccupancySensor::OCCUPANCY_SENSOR_TYPE_PIR_AND_ULTRASONIC);
  Matter.begin();
  Serial.println("FullAPI MatterOccupancySensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("t=toggle s=getOccupancy h=setHoldTime(30) l=getHoldTime");
  Serial.println("m=setHoldTimeLimits(0,3600,10) ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 't':
        Serial.println(Occ.setOccupancy(!Occ.getOccupancy()) ? "toggled" : "toggle failed");
        break;
      case 's': Serial.print("occupancy: "); Serial.println(Occ.getOccupancy()); break;
      case 'h':
        /* Always false: HoldTime is AttributeAccessInterface territory
         * in the firmware, not reachable over AT+MTATTR. */
        Serial.println(Occ.setHoldTime(30) ? "setHoldTime(30): OK" : "setHoldTime(30): failed (parked, see banner)");
        break;
      case 'l': Serial.print("holdTime: "); Serial.println(Occ.getHoldTime()); break;
      case 'm':
        /* Always false, same reason as setHoldTime(). */
        Serial.println(Occ.setHoldTimeLimits(0, 3600, 10) ? "setHoldTimeLimits: OK" : "setHoldTimeLimits: failed (parked, see banner)");
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
