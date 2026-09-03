/*
 * HearthFirstLight: the first sketch to run after flashing the Hearth
 * firmware. It turns the board into one Matter on/off light that a Matter
 * controller can commission, switch on and off, and see the state of. See
 * "WHICH APP" below before picking a controller.
 *
 * Hardware: an iLabs Challenger RP2350 WiFi6/BLE5, whose ESP32-C6
 * co-processor has been flashed with the Hearth firmware. If you have not
 * done that yet, stop here and read fw/README.md in this library: nothing
 * below can work until the C6 is running Hearth.
 *
 * WHAT YOU SHOULD SEE, in the Arduino IDE's Serial Monitor at 115200 baud:
 *
 *   Hearth first light
 *   Firmware on the co-processor: 1.1.0
 *   Endpoint declared. Starting Matter...
 *   Not commissioned yet. Add this device in your Matter app.
 *     Manual pairing code: 34970112332
 *     QR code URL:         https://project-chip.github.io/...
 *
 * The pairing code and the QR payload are FIXED: this development build
 * uses the SDK's test credentials (discriminator 3840, setup passcode
 * 20202021), so every board running this firmware prints exactly the same
 * two strings. Matching the example in the README is correct rather than a
 * sign of a bad flash, and it also means the code is not a secret: see the
 * README's step 3 for what that means while a board sits advertising.
 *
 * That is the sketch working. It is not commissioned yet, which is the
 * expected state on the first boot: a Matter device has to be adopted by a
 * hub before it belongs to anything. Open your Matter app, choose "add
 * device", and either scan the QR code the URL renders or type the manual
 * pairing code. Commissioning runs over Bluetooth LE, and the hub hands the
 * C6 your WiFi (or Thread) credentials as part of it, so nothing in this
 * sketch ever sees a password. It takes a minute or two.
 *
 * WHICH CONTROLLER: this firmware is uncertified and uses Matter's public
 * development credentials, which Apple Home, Google Home and Alexa are
 * entitled to refuse and are expected to. Two are verified against it: the
 * "NXP Matter Chip-tool" app on Android (minutes to install, no iOS
 * build), and the CLI chip-tool from the Matter SDK (hours to build). The
 * README's step 4 has both links and the honest costs.
 *
 * THE PAIRING WINDOW LASTS 15 MINUTES from boot. This sketch does not ask
 * whether it has closed, so the reminder below keeps printing the same
 * code afterwards. If pairing fails on a board that has been powered for
 * a while, press RESET and use the code printed after the reboot. (A
 * sketch can also reopen the window without a reboot, and ask whether one
 * is open: Matter.openCommissioningWindow() and Matter.deviceState(), see
 * the README's "Reopening the pairing window".)
 *
 * When it finishes, the serial monitor prints
 *
 *   Commissioned. The light is now controllable from your Matter app.
 *
 * and the on/off switch in the app drives the board's LED. The BOOTSEL
 * button toggles the same light from this end, and the app sees that
 * change too: state flows both ways.
 *
 * The commissioning only happens once. On every later boot the device is
 * already on the fabric and the sketch goes straight to the last line.
 *
 * IF NOTHING PRINTS AT ALL, or the firmware version comes back empty, the
 * host is not talking to the C6. The usual causes, in order: the C6 was
 * never flashed, or the USB-to-serial bridge sketch from fw/ is still
 * loaded on the RP2350 (uploading this sketch replaces it), or the board
 * selected in the IDE is not the Challenger. fw/README.md covers all
 * three.
 *
 * WHERE TO GO NEXT: the library README's examples map. In short, the
 * Matter* sketches are one device type each, and examples/FullAPI holds
 * one sketch per class exercising every call it has.
 */

#include <Matter.h>

/*
 * One endpoint. `MatterOnOffLight` is the class an unmodified
 * arduino-esp32 sketch would use, and it means the same thing here: an
 * endpoint that a controller sees as a switchable light.
 *
 * Declaring it at file scope like this, before setup(), is the normal
 * shape. Nothing happens on the wire yet.
 */
MatterOnOffLight light;

/* The LED this light drives. Every Challenger variant defines
 * LED_BUILTIN, so this needs no editing. */
const uint8_t ledPin = LED_BUILTIN;

/*
 * BOOT_PIN is the button an arduino-esp32 sketch reads. This library
 * defines it for you and points it at the RP2350's BOOTSEL button, so the
 * upstream examples compile unmodified and this one gets a button without
 * any wiring. Pressed reads LOW.
 */
const uint8_t buttonPin = BOOT_PIN;

/* Button debounce state. */
uint32_t buttonPressedAt = 0;
bool buttonDown = false;
const uint32_t debounceMs = 250;

/* So the "not commissioned" reminder prints every few seconds rather than
 * thousands of times a second, and the "commissioned" line prints once. */
uint32_t lastReminderAt = 0;
uint32_t lastCheckAt = 0;
bool commissioned = false;

/*
 * The controller calls this. Anything a Matter app does to the light,
 * whether from a phone, a voice assistant or an automation, arrives here
 * as a new state, and the job of this function is to make the hardware
 * match it.
 *
 * WHAT THE RETURN VALUE ACTUALLY DOES, because it is easy to assume more:
 * by the time this runs the change has ALREADY happened on the C6 and the
 * controller already believes it. Returning false does not refuse it and
 * does not revert anything on the wire: the library discards this
 * function's answer (Hearth.cpp, hearthDispatchAttr(), which calls
 * attributeChangeCB() and ignores its result). All it changes is one
 * thing, inside this object: on false, MatterOnOffLight::attributeChangeCB()
 * skips updating its own cached copy, so getOnOff() keeps reporting the
 * old value while the device and your app report the new one.
 *
 * So return true unless you specifically want that disagreement. To
 * genuinely reject a state you cannot apply, push the correction back:
 * record it here, and call light.setOnOff(oldState) from loop()
 * afterwards. Not from inside this function, which is running inside a
 * library call, and a nested call into the library is refused rather than
 * served.
 *
 * IMPORTANT, and different from an ESP32: this runs from inside
 * Hearth.poll() (or any other call into the library), on the same core as
 * loop(). There is no background task on an RP2350. A loop() that blocks
 * for a second delays this callback by a second.
 */
bool onLightChange(bool state) {
  digitalWrite(ledPin, state ? HIGH : LOW);
  Serial.print("Light is now ");
  Serial.println(state ? "ON" : "OFF");
  return true;
}

/*
 * Kept out of loop() on purpose. Both of these return a String, and a
 * String temporary lives in the caller's stack frame; GCC hoists such
 * locals into the prologue of the function that contains them, where they
 * stay allocated on every single iteration whether the branch runs or not.
 * `noinline` is what stops the compiler from undoing that at -Os.
 *
 * This matters more than it looks on a device with 8 KB of stack per core.
 * The rule to copy from this sketch: keep large or non-trivial locals out
 * of any loop() that calls into this library.
 */
static void __attribute__((noinline)) printCommissioningInfo() {
  Serial.println();
  Serial.println("Not commissioned yet. Add this device in your Matter app.");
  Serial.print("  Manual pairing code: ");
  Serial.println(Matter.getManualPairingCode());
  Serial.print("  QR code URL:         ");
  Serial.println(Matter.getOnboardingQRCodeUrl());
}

static void __attribute__((noinline)) printFirmwareVersion() {
  /* AT+MTVER? over the UART the board wires between the RP2350 and the
   * C6. This is also the honest way to confirm a flash: it is the C6
   * answering, not the host guessing. An empty answer means the link is
   * not working, so the rest of this sketch cannot work either. */
  String version = Hearth.firmwareVersion();
  Serial.print("Firmware on the co-processor: ");
  if (version.length() == 0) {
    Serial.println("(no answer, see fw/README.md)");
  } else {
    Serial.println(version);
  }
}

void setup() {
  Serial.begin(115200);
  /* Wait for the Serial Monitor to attach, but not forever: a board on a
   * USB charger has nobody to wait for. */
  while (!Serial && millis() < 8000) {}

  Serial.println();
  Serial.println("Hearth first light");

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  /* The first call into the library brings the link up: it resets the C6
   * into run mode and waits for its +MTREADY. A sketch never names a
   * serial port, because which UART reaches the C6 is a property of the
   * board, and the board variant already knows it. */
  printFirmwareVersion();

  /* Register the callback before begin(). Not because a state the C6
   * already holds arrives that way, which it does not: any +MTATTR that
   * lands before Matter.begin() has finished reconciling is deliberately
   * consumed and dropped, since no endpoint has been given its id yet and
   * there is nothing to deliver it to (Hearth.cpp, hearthDispatchAttr()'s
   * header comment spells out why). Register early simply so that the
   * first change AFTER that point has somewhere to go.
   *
   * The C6 holds the authoritative value across a host reboot. If your
   * sketch cares about it, read it back after Matter.begin() with
   * light.getOnOff(), or push your own with updateAccessory() as below. */
  light.onChange(onLightChange);

  /* begin() declares this endpoint locally. Still nothing on the wire. */
  light.begin(false);

  Serial.println("Endpoint declared. Starting Matter...");

  /*
   * Matter.begin() is the step that talks to the C6. It compares the
   * endpoints this sketch declared against the ones the co-processor is
   * holding in its own flash, rewrites them if they differ, and starts the
   * Matter stack.
   *
   * Two rules follow from that, and they are the two mistakes newcomers
   * make:
   *
   *   1. Call it LAST in setup(), after every endpoint's own begin().
   *      Endpoints declared after this point are refused.
   *   2. Never call it from loop(). Each call can rewrite the
   *      co-processor's stored composition and reboot it.
   */
  Matter.begin();

  commissioned = Matter.isDeviceCommissioned();
  if (commissioned) {
    Serial.println("Commissioned. The light is now controllable from your Matter app.");
    /* Push the local state up so the app's view matches the LED. */
    light.updateAccessory();
  }
}

void loop() {
  /*
   * First, every iteration, unconditionally.
   *
   * On an ESP32 the Matter stack runs in its own background task. Here it
   * runs on the C6, and everything it sends (a controller switching the
   * light, a commissioning event) arrives as a line sitting in this MCU's
   * UART buffer until something reads it. Hearth.poll() is that something,
   * and it is what makes onLightChange() fire.
   *
   * Any other call into the library pumps the link too, so a sketch that
   * is constantly calling Matter.* survives without this. Do not rely on
   * that. Put poll() first and the question never comes up.
   */
  Hearth.poll();

  /*
   * Only until it is commissioned, and only once a second even then.
   * Matter.isDeviceCommissioned() is not a local flag: every call sends
   * AT+MTFABRICS? and waits for the answer, so asking on every iteration
   * puts a round trip on the link thousands of times a second for a fact
   * that changes once in the device's life. Once it is true it stays true
   * until the device is removed from the fabric, so this stops asking.
   */
  if (!commissioned && millis() - lastCheckAt > 1000) {
    lastCheckAt = millis();
    commissioned = Matter.isDeviceCommissioned();
    if (commissioned) {
      Serial.println();
      Serial.println("Commissioned. The light is now controllable from your Matter app.");
      light.updateAccessory();
    } else if (lastReminderAt == 0 || millis() - lastReminderAt > 5000) {
      /* Print the pairing code every five seconds, so a monitor opened
       * halfway through still gets it. */
      lastReminderAt = millis();
      printCommissioningInfo();
    }
  }

  /* The BOOTSEL button toggles the light locally. The controller sees the
   * new state, exactly as if the app had made the change itself. */
  if (digitalRead(buttonPin) == LOW && !buttonDown) {
    buttonDown = true;
    buttonPressedAt = millis();
  }
  if (buttonDown && digitalRead(buttonPin) == HIGH &&
      millis() - buttonPressedAt > debounceMs) {
    buttonDown = false;
    Serial.println("Button pressed, toggling the light.");
    light.toggle();
  }

  /*
   * No delay() here. Every millisecond spent sleeping is a millisecond
   * poll() is not running, which is latency a controller feels.
   */
}
