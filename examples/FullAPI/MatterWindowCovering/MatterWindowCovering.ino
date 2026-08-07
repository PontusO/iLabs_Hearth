/*
 * FullAPI reference: MatterWindowCovering
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 * ABSENT ABSOLUTE-POSITION API: the class header states it directly:
 * "esp-matter 1.5.1 carries no absolute-position attributes
 * (CurrentPositionLift/Tilt, InstalledOpenLimitLift/Tilt,
 * InstalledClosedLimitLift/Tilt), so setLiftPosition/setTiltPosition, the
 * InstalledOpenLimit(Lift/Tilt) and InstalledClosedLimit(Lift/Tilt) pairs,
 * and their getters exist for upstream parity but return false (or, for
 * the uint16_t getters, 0)". Those eight methods are still public and
 * still exercised below (menu 'k' for lift, 'j' for tilt), so the banner
 * checklist is honest about them, but nothing they do is live: the real
 * position surface on this stack is the percent100ths pair
 * (setTargetLiftPercent100ths/setTargetTiltPercent100ths) plus the plain
 * percentage pair (setLiftPercentage/setTiltPercentage).
 *
 *   WindowCoveringType_t   ten values; cycled by menu 't'
 *   OperationalState_t     three values (STALL, MOVING_UP_OR_OPEN,
 *                           MOVING_DOWN_OR_CLOSE); cycled by menu 'm'/'n'
 *   OperationalStatusField_t  GLOBAL/LIFT/TILT bitmap fields; LIFT used by
 *                           menu 'm', TILT by menu 'n', GLOBAL by menu 'v'
 *   MatterWindowCovering()         global object below
 *   begin(uint8_t, uint8_t,
 *         WindowCoveringType_t)    setup()
 *   setLiftPosition(uint16_t)      menu 'k' (stub: always false, see note)
 *   getLiftPosition()              menu 'k' (stub: always 0, see note)
 *   setLiftPercentage(uint8_t)     menu 'u', cycles 0/50/100
 *   getLiftPercentage()            menu 'i'
 *   setTargetLiftPercent100ths(uint16_t)  menu '+' / '-', step 1000
 *                                          (10.00%)
 *   getTargetLiftPercent100ths()   menu 'g'
 *   setInstalledOpenLimitLift(uint16_t)   menu 'k' (stub, see note)
 *   getInstalledOpenLimitLift()    menu 'k' (stub, see note)
 *   setInstalledClosedLimitLift(uint16_t) menu 'k' (stub, see note)
 *   getInstalledClosedLimitLift()  menu 'k' (stub, see note)
 *   setTiltPosition(uint16_t)      menu 'j' (stub: always false, see note)
 *   getTiltPosition()              menu 'j' (stub: always 0, see note)
 *   setTiltPercentage(uint8_t)     menu 'o', cycles 0/50/100
 *   getTiltPercentage()            menu 'p'
 *   setTargetTiltPercent100ths(uint16_t)  menu ']' / '[', step 1000
 *                                          (10.00%)
 *   getTargetTiltPercent100ths()   menu 'h'
 *   setInstalledOpenLimitTilt(uint16_t)   menu 'j' (stub, see note)
 *   getInstalledOpenLimitTilt()    menu 'j' (stub, see note)
 *   setInstalledClosedLimitTilt(uint16_t) menu 'j' (stub, see note)
 *   getInstalledClosedLimitTilt()  menu 'j' (stub, see note)
 *   setCoveringType(WindowCoveringType_t) menu 't', cycles all ten values
 *   getCoveringType()              menu 'y'
 *   setOperationalStatus(uint8_t)  menu 'v', writes the GLOBAL field bits
 *   getOperationalStatus()         menu 'V'
 *   setOperationalState(field, state)  menu 'm' (LIFT field) / 'n' (TILT
 *                                       field), cycles all three states
 *   getOperationalState(field)     menu 'r', reads both LIFT and TILT
 *   onOpen(cb)                     setup(), prints when fired
 *   onClose(cb)                    setup(), prints when fired
 *   onGoToLiftPercentage(cb)       setup(), prints the target percentage
 *   onGoToTiltPercentage(cb)       setup(), prints the target percentage
 *   onStop(cb)                     setup(), prints when fired
 *   onChange(cb)                   setup(), prints lift and tilt percent
 *   updateAccessory()              menu 'w' (see its header comment)
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * The six onXxx callbacks above are registered but never called directly
 * by this sketch: they fire only when a Matter controller sends the
 * corresponding WindowCovering cluster command (UpOrOpen, DownOrClose,
 * GoToLiftPercentage, GoToTiltPercentage, StopMotion), dispatched from
 * inside Hearth.poll() exactly like every onChange* callback elsewhere in
 * this library.
 *
 * Observe controller-side:
 *   chip-tool windowcovering read current-position-lift-percent100ths <node> <ep>
 *   chip-tool windowcovering read current-position-tilt-percent100ths <node> <ep>
 *   chip-tool windowcovering read type <node> <ep>
 *   chip-tool windowcovering up-or-open <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Covering.setLiftPercentage(50)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterWindowCovering Covering;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Covering.onOpen([]() {
    Serial.println("onOpen fired");
    return true;
  });
  Covering.onClose([]() {
    Serial.println("onClose fired");
    return true;
  });
  Covering.onGoToLiftPercentage([](uint8_t percent) {
    Serial.print("onGoToLiftPercentage: ");
    Serial.println(percent);
    return true;
  });
  Covering.onGoToTiltPercentage([](uint8_t percent) {
    Serial.print("onGoToTiltPercentage: ");
    Serial.println(percent);
    return true;
  });
  Covering.onStop([]() {
    Serial.println("onStop fired");
    return true;
  });
  Covering.onChange([](uint8_t liftPercent, uint8_t tiltPercent) {
    Serial.print("onChange: lift=");
    Serial.print(liftPercent);
    Serial.print(" tilt=");
    Serial.println(tiltPercent);
    return true;
  });

  Covering.begin(100, 0, MatterWindowCovering::ROLLERSHADE);
  Matter.begin();
  Serial.println("FullAPI MatterWindowCovering ready; '?' for menu");
}

void printHelp() {
  Serial.println("u=setLiftPercentage(cycle) i=getLiftPercentage  +/-=liftPercent100ths step1000  g=getTargetLiftPercent100ths");
  Serial.println("o=setTiltPercentage(cycle) p=getTiltPercentage  ]/[=tiltPercent100ths step1000  h=getTargetTiltPercent100ths");
  Serial.println("t=setCoveringType(cycle10) y=getCoveringType");
  Serial.println("v=setOperationalStatus(GLOBAL bits) V=getOperationalStatus");
  Serial.println("m=setOperationalState(LIFT,cycle3) n=setOperationalState(TILT,cycle3) r=getOperationalState(both)");
  Serial.println("k=lift absolute-position stubs (always false/0)  j=tilt absolute-position stubs (always false/0)");
  Serial.println("w=updateAccessory ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onOpen/onClose/onGoToLiftPercentage/onGoToTiltPercentage/onStop/
   * onChange callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'u':
        {
          static const uint8_t kPresets[3] = { 0, 50, 100 };
          static uint8_t idx = 0;
          idx = (idx + 1) % 3;
          Serial.print("setLiftPercentage(");
          Serial.print(kPresets[idx]);
          Serial.print("): ");
          Serial.println(Covering.setLiftPercentage(kPresets[idx]) ? "OK" : "failed");
        }
        break;
      case 'i': Serial.print("liftPercentage: "); Serial.println(Covering.getLiftPercentage()); break;
      case '+':
        {
          uint32_t raw = (uint32_t)Covering.getTargetLiftPercent100ths() + 1000;
          uint16_t p = (raw > 10000) ? 10000 : (uint16_t)raw;
          Serial.print("setTargetLiftPercent100ths(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Covering.setTargetLiftPercent100ths(p) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          int32_t raw = (int32_t)Covering.getTargetLiftPercent100ths() - 1000;
          uint16_t p = (raw < 0) ? 0 : (uint16_t)raw;
          Serial.print("setTargetLiftPercent100ths(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Covering.setTargetLiftPercent100ths(p) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("targetLiftPercent100ths: "); Serial.println(Covering.getTargetLiftPercent100ths()); break;
      case 'o':
        {
          static const uint8_t kPresets[3] = { 0, 50, 100 };
          static uint8_t idx = 0;
          idx = (idx + 1) % 3;
          Serial.print("setTiltPercentage(");
          Serial.print(kPresets[idx]);
          Serial.print("): ");
          Serial.println(Covering.setTiltPercentage(kPresets[idx]) ? "OK" : "failed");
        }
        break;
      case 'p': Serial.print("tiltPercentage: "); Serial.println(Covering.getTiltPercentage()); break;
      case ']':
        {
          uint32_t raw = (uint32_t)Covering.getTargetTiltPercent100ths() + 1000;
          uint16_t p = (raw > 10000) ? 10000 : (uint16_t)raw;
          Serial.print("setTargetTiltPercent100ths(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Covering.setTargetTiltPercent100ths(p) ? "OK" : "failed");
        }
        break;
      case '[':
        {
          int32_t raw = (int32_t)Covering.getTargetTiltPercent100ths() - 1000;
          uint16_t p = (raw < 0) ? 0 : (uint16_t)raw;
          Serial.print("setTargetTiltPercent100ths(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Covering.setTargetTiltPercent100ths(p) ? "OK" : "failed");
        }
        break;
      case 'h': Serial.print("targetTiltPercent100ths: "); Serial.println(Covering.getTargetTiltPercent100ths()); break;
      case 't':
        {
          uint8_t next = (static_cast<uint8_t>(Covering.getCoveringType()) + 1) % 10;
          MatterWindowCovering::WindowCoveringType_t type = static_cast<MatterWindowCovering::WindowCoveringType_t>(next);
          Serial.print("setCoveringType(");
          Serial.print(type);
          Serial.print("): ");
          Serial.println(Covering.setCoveringType(type) ? "OK" : "failed");
        }
        break;
      case 'y': Serial.print("coveringType: "); Serial.println(Covering.getCoveringType()); break;
      case 'v':
        {
          /* GLOBAL field bits, per OperationalStatusField_t. */
          uint8_t status = Covering.getOperationalStatus() ^ MatterWindowCovering::GLOBAL;
          Serial.print("setOperationalStatus(");
          Serial.print(status);
          Serial.print("): ");
          Serial.println(Covering.setOperationalStatus(status) ? "OK" : "failed");
        }
        break;
      case 'V': Serial.print("operationalStatus: "); Serial.println(Covering.getOperationalStatus()); break;
      case 'm':
        {
          uint8_t next = (static_cast<uint8_t>(Covering.getOperationalState(MatterWindowCovering::LIFT)) + 1) % 3;
          MatterWindowCovering::OperationalState_t state = static_cast<MatterWindowCovering::OperationalState_t>(next);
          Serial.print("setOperationalState(LIFT, ");
          Serial.print(state);
          Serial.print("): ");
          Serial.println(Covering.setOperationalState(MatterWindowCovering::LIFT, state) ? "OK" : "failed");
        }
        break;
      case 'n':
        {
          uint8_t next = (static_cast<uint8_t>(Covering.getOperationalState(MatterWindowCovering::TILT)) + 1) % 3;
          MatterWindowCovering::OperationalState_t state = static_cast<MatterWindowCovering::OperationalState_t>(next);
          Serial.print("setOperationalState(TILT, ");
          Serial.print(state);
          Serial.print("): ");
          Serial.println(Covering.setOperationalState(MatterWindowCovering::TILT, state) ? "OK" : "failed");
        }
        break;
      case 'r':
        Serial.print("operationalState LIFT: ");
        Serial.print(Covering.getOperationalState(MatterWindowCovering::LIFT));
        Serial.print("  TILT: ");
        Serial.println(Covering.getOperationalState(MatterWindowCovering::TILT));
        break;
      case 'k':
        /* Absolute-position stub group: every call below is a structural
         * no-op on this stack, see the ABSENT ABSOLUTE-POSITION API note. */
        Serial.print("setLiftPosition(500): ");
        Serial.println(Covering.setLiftPosition(500) ? "OK" : "false (expected, stub)");
        Serial.print("getLiftPosition(): ");
        Serial.println(Covering.getLiftPosition());
        Serial.print("setInstalledOpenLimitLift(0): ");
        Serial.println(Covering.setInstalledOpenLimitLift(0) ? "OK" : "false (expected, stub)");
        Serial.print("getInstalledOpenLimitLift(): ");
        Serial.println(Covering.getInstalledOpenLimitLift());
        Serial.print("setInstalledClosedLimitLift(1000): ");
        Serial.println(Covering.setInstalledClosedLimitLift(1000) ? "OK" : "false (expected, stub)");
        Serial.print("getInstalledClosedLimitLift(): ");
        Serial.println(Covering.getInstalledClosedLimitLift());
        break;
      case 'j':
        /* Same stub group, tilt side. */
        Serial.print("setTiltPosition(500): ");
        Serial.println(Covering.setTiltPosition(500) ? "OK" : "false (expected, stub)");
        Serial.print("getTiltPosition(): ");
        Serial.println(Covering.getTiltPosition());
        Serial.print("setInstalledOpenLimitTilt(0): ");
        Serial.println(Covering.setInstalledOpenLimitTilt(0) ? "OK" : "false (expected, stub)");
        Serial.print("getInstalledOpenLimitTilt(): ");
        Serial.println(Covering.getInstalledOpenLimitTilt());
        Serial.print("setInstalledClosedLimitTilt(1000): ");
        Serial.println(Covering.setInstalledClosedLimitTilt(1000) ? "OK" : "false (expected, stub)");
        Serial.print("getInstalledClosedLimitTilt(): ");
        Serial.println(Covering.getInstalledClosedLimitTilt());
        break;
      case 'w': Covering.updateAccessory(); Serial.println("updateAccessory called"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
