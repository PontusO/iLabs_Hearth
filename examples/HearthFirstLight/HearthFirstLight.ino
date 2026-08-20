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
 *   Firmware on the co-processor: 1.0.0
 *   Endpoint declared. Starting Matter...
 *   Not commissioned yet. Add this device in your Matter app.
 *     Manual pairing code: 34970112332
 *     QR code URL:         https://project-chip.github.io/...
 *
 * That is the sketch working. It is not commissioned yet, which is the
 * expected state on the first boot: a Matter device has to be adopted by a
 * hub before it belongs to anything. Open your Matter app, choose "add
 * device", and either scan the QR code the URL renders or type the manual
 * pairing code. Commissioning runs over Bluetooth LE, and the hub hands the
 * C6 your WiFi (or Thread) credentials as part of it, so nothing in this
 * sketch ever sees a password. It takes a minute or two.
 *
 * WHICH APP: this firmware is uncertified and uses Matter's public
 * development credentials, which Apple Home, Google Home and Alexa are
 * entitled to refuse and are expected to. NXP's chip-tool app for Android
 * and iOS is the commissioner this project verifies against; the CLI
 * chip-tool works too.
 *
 * THE PAIRING WINDOW LASTS 15 MINUTES from boot. Nothing here can tell
 * that it has closed, so the reminder below keeps printing the same code
 * afterwards. If pairing fails on a board that has been powered for a
 * while, press RESET and use the code printed after the reboot.
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
bool wasCommissioned = false;

/*
 * The controller calls this. Anything a Matter app does to the light,
 * whether from a phone, a voice assistant or an automation, arrives here
 * as a new state, and the job of this function is to make the hardware
 * match it and say whether that worked.
 *
 * Returning false tells the controller the change was refused, and the
 * attribute stays at its old value. A real device would return false when
 * it physically cannot do the thing.
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

  /* Register the callback before begin(), so a state the C6 already holds
   * from a previous run is delivered rather than missed. */
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

  wasCommissioned = Matter.isDeviceCommissioned();
  if (wasCommissioned) {
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

  bool commissioned = Matter.isDeviceCommissioned();

  if (!commissioned) {
    /* Print the pairing code every five seconds, so a monitor opened
     * halfway through still gets it. */
    if (lastReminderAt == 0 || millis() - lastReminderAt > 5000) {
      lastReminderAt = millis();
      printCommissioningInfo();
    }
  } else if (!wasCommissioned) {
    Serial.println();
    Serial.println("Commissioned. The light is now controllable from your Matter app.");
    light.updateAccessory();
  }
  wasCommissioned = commissioned;

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
