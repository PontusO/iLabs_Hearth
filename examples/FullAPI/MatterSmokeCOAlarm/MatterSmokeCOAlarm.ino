/*
 * FullAPI reference: MatterSmokeCOAlarm
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Smoke/CO Alarm class or example at all (see the class header),
 * so this is this port's own design against the firmware's C5 wire
 * contract, not a mirror of anything upstream.
 *
 *   MatterSmokeCOAlarm()                 global object below
 *   begin()                              setup(), no initial state to
 *                                         reconcile
 *   onSelfTest(cb)                       setup(), notify-only (see below)
 *   completeSelfTest()                   menu 't', reports test finished
 *   setSmokeState(s)                     menu '1', cycles 0/1/2
 *   getSmokeState()                      menu 'q'
 *   setCOState(s)                        menu '2', cycles 0/1/2
 *   getCOState()                         menu 'w'
 *   setBatteryAlert(s)                   menu '3', cycles 0/1/2
 *   getBatteryAlert()                    menu 'e'
 *   setDeviceMuted(s)                    menu '4', cycles 0/1
 *   getDeviceMuted()                     menu 'r'
 *   setHardwareFaultAlert(v)             menu '5', toggles
 *   getHardwareFaultAlert()              menu 'y'
 *   setEndOfServiceAlert(v)              menu '6', cycles 0/1
 *   getEndOfServiceAlert()               menu 'u'
 *   setInterconnectSmokeAlarm(s)         menu '7', cycles 0/1/2
 *   getInterconnectSmokeAlarm()          menu 'i'
 *   setInterconnectCOAlarm(s)            menu '8', cycles 0/1/2
 *   getInterconnectCOAlarm()             menu 'o'
 *   setContaminationState(s)             menu '9', cycles 0/1/2
 *   getContaminationState()              menu 'p'
 *   setSmokeSensitivityLevel(s)          menu '0', cycles 0/1/2
 *   getSmokeSensitivityLevel()           menu 'k'
 *   getExpressedState()                  menu 'x', a genuine AT+MTATTR
 *                                         read, not a cached value (see
 *                                         below)
 *   end()                       not called: tearing the endpoint down
 *                                mid-demo is not a usable demo; call it
 *                                when retiring the endpoint
 *
 * -------------------------------------------------------------------------
 * onSelfTest() is this library's first notify-only +MTCMD consumer
 * -------------------------------------------------------------------------
 * SmokeCoAlarmServer::HandleRemoteSelfTestRequest answers the controller
 * itself before this sketch's callback ever runs, so a controller-invoked
 * self test always arrives as +MTCMD:0,<ep>,92,0 (AT_MT_SPEC.md S3.17's
 * notify-only form, seq 0 reserved). onSelfTest()'s callback runs, but
 * there is no verdict to send back: this library's dispatcher never even
 * tries. There is therefore no 1000 ms deadline to race and no
 * HEARTH_CMD_TIMEOUT to watch for here, unlike every adjudicated class in
 * this library (MatterDoorLock, MatterWaterValve, MatterChime, the
 * OperationalState trio). This sketch's onSelfTest() handler simulates a
 * self test running for one second, then calls completeSelfTest() itself;
 * a real sketch would drive whatever hardware self-check the device
 * actually has.
 * -------------------------------------------------------------------------
 *
 * None of the eleven setters validates its <value> against its field's
 * own enum range host-side: an out-of-range value surfaces as an ordinary
 * failed AT+MTALARM (the firmware's own +MTERR:1), cache untouched. Every
 * setter is a no-op (no wire traffic) when the new value already matches
 * the cache. getExpressedState() is the exception to every other getter in
 * this class: ExpressedState is derived server-side from the ten states
 * above and never settable, so there is no cached value to return
 * instead -- it is a genuine wire read on every call, this library's
 * first real consumer of that path.
 *
 * Observe controller-side:
 *   chip-tool smokecoalarm self-test-request <node> <ep>
 *   chip-tool smokecoalarm read expressed-state <node> <ep>
 *   chip-tool smokecoalarm read smoke-state <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Alarm.setSmokeState(1)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterSmokeCOAlarm Alarm;

bool selfTestRunning = false;
uint32_t selfTestStartedAt = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  // Notify-only: no verdict to give, see the header comment above. This
  // handler simulates a one-second self test; a real sketch would start
  // whatever hardware check the device actually has and call
  // completeSelfTest() when it genuinely finishes.
  Alarm.onSelfTest([]() {
    Serial.println("onSelfTest: controller-requested self test starting (simulated, 1s)");
    selfTestRunning = true;
    selfTestStartedAt = millis();
  });

  Alarm.begin();
  Matter.begin();
  Serial.println("FullAPI MatterSmokeCOAlarm ready; '?' for menu");
}

void printHelp() {
  Serial.println("t=completeSelfTest (also fires automatically 1s after a controller self-test)");
  Serial.println("1=setSmokeState(cycle) q=getSmokeState  2=setCOState(cycle) w=getCOState");
  Serial.println("3=setBatteryAlert(cycle) e=getBatteryAlert  4=setDeviceMuted(cycle) r=getDeviceMuted");
  Serial.println("5=setHardwareFaultAlert(toggle) y=getHardwareFaultAlert");
  Serial.println("6=setEndOfServiceAlert(cycle) u=getEndOfServiceAlert");
  Serial.println("7=setInterconnectSmokeAlarm(cycle) i=getInterconnectSmokeAlarm");
  Serial.println("8=setInterconnectCOAlarm(cycle) o=getInterconnectCOAlarm");
  Serial.println("9=setContaminationState(cycle) p=getContaminationState");
  Serial.println("0=setSmokeSensitivityLevel(cycle) k=getSmokeSensitivityLevel");
  Serial.println("x=getExpressedState (genuine wire read)  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch for every other
   * endpoint and command in the sketch, even though this class itself has
   * no controller-driven attribute change to dispatch. */
  Hearth.poll();

  if (selfTestRunning && millis() - selfTestStartedAt > 1000) {
    selfTestRunning = false;
    Serial.println(Alarm.completeSelfTest() ? "completeSelfTest (auto): OK" : "completeSelfTest (auto): failed");
  }

  if (Serial.available()) {
    switch (Serial.read()) {
      case 't': Serial.println(Alarm.completeSelfTest() ? "completeSelfTest: OK" : "completeSelfTest: failed"); break;
      case '1':
        {
          uint8_t next = (Alarm.getSmokeState() + 1) % 3;
          Serial.print("setSmokeState(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setSmokeState(next) ? "OK" : "failed");
        }
        break;
      case 'q': Serial.print("smokeState: "); Serial.println(Alarm.getSmokeState()); break;
      case '2':
        {
          uint8_t next = (Alarm.getCOState() + 1) % 3;
          Serial.print("setCOState(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setCOState(next) ? "OK" : "failed");
        }
        break;
      case 'w': Serial.print("coState: "); Serial.println(Alarm.getCOState()); break;
      case '3':
        {
          uint8_t next = (Alarm.getBatteryAlert() + 1) % 3;
          Serial.print("setBatteryAlert(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setBatteryAlert(next) ? "OK" : "failed");
        }
        break;
      case 'e': Serial.print("batteryAlert: "); Serial.println(Alarm.getBatteryAlert()); break;
      case '4':
        {
          uint8_t next = (Alarm.getDeviceMuted() + 1) % 2;
          Serial.print("setDeviceMuted(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setDeviceMuted(next) ? "OK" : "failed");
        }
        break;
      case 'r': Serial.print("deviceMuted: "); Serial.println(Alarm.getDeviceMuted()); break;
      case '5':
        {
          bool next = !Alarm.getHardwareFaultAlert();
          Serial.print("setHardwareFaultAlert(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setHardwareFaultAlert(next) ? "OK" : "failed");
        }
        break;
      case 'y': Serial.print("hardwareFaultAlert: "); Serial.println(Alarm.getHardwareFaultAlert()); break;
      case '6':
        {
          uint8_t next = (Alarm.getEndOfServiceAlert() + 1) % 2;
          Serial.print("setEndOfServiceAlert(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setEndOfServiceAlert(next) ? "OK" : "failed");
        }
        break;
      case 'u': Serial.print("endOfServiceAlert: "); Serial.println(Alarm.getEndOfServiceAlert()); break;
      case '7':
        {
          uint8_t next = (Alarm.getInterconnectSmokeAlarm() + 1) % 3;
          Serial.print("setInterconnectSmokeAlarm(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setInterconnectSmokeAlarm(next) ? "OK" : "failed");
        }
        break;
      case 'i': Serial.print("interconnectSmokeAlarm: "); Serial.println(Alarm.getInterconnectSmokeAlarm()); break;
      case '8':
        {
          uint8_t next = (Alarm.getInterconnectCOAlarm() + 1) % 3;
          Serial.print("setInterconnectCOAlarm(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setInterconnectCOAlarm(next) ? "OK" : "failed");
        }
        break;
      case 'o': Serial.print("interconnectCOAlarm: "); Serial.println(Alarm.getInterconnectCOAlarm()); break;
      case '9':
        {
          uint8_t next = (Alarm.getContaminationState() + 1) % 3;
          Serial.print("setContaminationState(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setContaminationState(next) ? "OK" : "failed");
        }
        break;
      case 'p': Serial.print("contaminationState: "); Serial.println(Alarm.getContaminationState()); break;
      case '0':
        {
          uint8_t next = (Alarm.getSmokeSensitivityLevel() + 1) % 3;
          Serial.print("setSmokeSensitivityLevel(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Alarm.setSmokeSensitivityLevel(next) ? "OK" : "failed");
        }
        break;
      case 'k': Serial.print("smokeSensitivityLevel: "); Serial.println(Alarm.getSmokeSensitivityLevel()); break;
      case 'x': Serial.print("expressedState (genuine wire read): "); Serial.println(Alarm.getExpressedState()); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}
