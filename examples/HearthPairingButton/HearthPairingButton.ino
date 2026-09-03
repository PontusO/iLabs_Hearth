/*
 * HearthPairingButton: put an already-commissioned device back into pairing
 * mode from the board itself, and watch its commissioning state as it
 * happens. This is the sketch behind a real product's "pair" or "add to
 * another home" button, and it uses the two calls the Hearth library added
 * in 1.1.0 that upstream arduino-esp32 does not have:
 * Matter.openCommissioningWindow() and Matter.deviceState().
 *
 * Hardware: an iLabs Challenger RP2350 WiFi6/BLE5 whose ESP32-C6 has been
 * flashed with the Hearth firmware (fw/README.md in this library). Start
 * with HearthFirstLight if this is a fresh board; this example assumes you
 * already know how to commission one.
 *
 * The one endpoint is an on/off light, only so the device is a genuine
 * Matter accessory a controller can adopt. Its on/off state prints to the
 * Serial Monitor when the controller changes it; the LED is doing a
 * different job.
 *
 * THE ONBOARD LED SHOWS COMMISSIONING STATE, not the light:
 *
 *   off    the device holds no fabric and no window is open (uninitialized)
 *   blink  a commissioning window is open right now, advertising for a hub
 *   solid  the device is on at least one fabric (operational)
 *
 * The blink is the interesting one: a window closes on its own after its
 * timeout, and the LED stops blinking the moment it does, because
 * Matter.deviceState() is the only way to learn "is a window open right
 * now". HearthFirstLight cannot tell; this sketch asks.
 *
 * THE BOOT BUTTON runs the commissioning lifecycle:
 *
 *   short press   Matter.openCommissioningWindow(): reopen the pairing
 *                 window without a reboot, so another controller can adopt
 *                 the device (or a second one can, for a second home). A
 *                 factory-fresh device opens a window at boot on its own; a
 *                 commissioned one does not, which is why this call exists.
 *   long press    hold for five seconds to Matter.decommission(): forget
 *                 every fabric and start over.
 *
 * WHICH CONTROLLER: this firmware is uncertified and uses Matter's public
 * development credentials, so Apple Home, Google Home and Alexa are
 * entitled to refuse it. The NXP Matter Chip-tool app on Android and the
 * CLI chip-tool from the Matter SDK both work; see the README's step 4.
 *
 * The pairing code is FIXED (SDK test credentials), so every window on
 * every board advertises the same code. Reopening the window changes
 * nothing about which code to type.
 */

#include <Matter.h>

// One on/off light, so the device is commissionable. Its value goes to the
// Serial Monitor, not the LED.
MatterOnOffLight light;

// The onboard LED shows commissioning state; the BOOT button drives the
// commissioning lifecycle.
#ifdef LED_BUILTIN
const uint8_t ledPin = LED_BUILTIN;
#else
const uint8_t ledPin = 2;  // set your board's LED pin if LED_BUILTIN is not defined
#warning "Do not forget to set the LED pin"
#endif
const uint8_t buttonPin = BOOT_PIN;

// Button timing. A press released before the long-press threshold reopens
// the window; a press held past it decommissions.
const uint32_t debounceMs = 50;
const uint32_t longPressMs = 5000;
bool buttonDown = false;
bool longHandled = false;  // the long press already fired for this hold
uint32_t buttonPressedAt = 0;

// deviceState() is a round trip on the AT link, so poll it on a timer, not
// every loop, and print only when something changed.
const uint32_t statePollMs = 2000;
uint32_t lastPollAt = 0;
MatterDeviceState liveState = MATTER_STATE_UNINITIALIZED;  // last good read, drives the LED
MatterDeviceState printedState = (MatterDeviceState)0xFF;  // force a first print
unsigned int printedFabrics = 0xFFFF;

// LED blink, active only while a window is open.
const uint32_t blinkMs = 400;
uint32_t lastBlinkAt = 0;
bool blinkOn = false;

const char *stateName(MatterDeviceState s) {
  switch (s) {
    case MATTER_STATE_UNINITIALIZED: return "uninitialized (no fabric, no open window)";
    case MATTER_STATE_COMMISSIONING: return "commissioning (a window is open)";
    case MATTER_STATE_OPERATIONAL:   return "operational (on a fabric)";
    default:                         return "unknown";
  }
}

// The controller (or another sketch) changed the light. Report it; the LED
// is not ours to touch here, it belongs to commissioning state.
bool onLightChange(bool on) {
  Serial.printf("Light: %s\r\n", on ? "ON" : "OFF");
  return true;
}

void printPairingCode() {
  Serial.printf("  Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
  Serial.printf("  QR code URL:         %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}  // a board on a charger has nobody to wait for

  Serial.println();
  Serial.println("Hearth pairing button");

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  // The first call into the library brings the link up and prints the
  // co-processor's firmware version; an empty answer means the link is not
  // working and nothing below can either.
  String version = Hearth.firmwareVersion();
  Serial.printf("Firmware on the co-processor: %s\r\n",
                version.length() ? version.c_str() : "(no answer, see fw/README.md)");

  light.onChange(onLightChange);
  light.begin(false);   // declare the endpoint locally; nothing on the wire yet
  Matter.begin();       // reconcile against the C6, last

  // deviceState() tells us where we stand without assuming anything: a
  // fresh board reports uninitialized and opens its own window at boot; a
  // board that already holds a fabric reports operational.
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Not commissioned yet. Add this device in your Matter app, or press BOOT to (re)open the window.");
    printPairingCode();
  }
}

void loop() {
  // First, every iteration: drain whatever the C6 has sent (a controller
  // switching the light, a commissioning event) so onLightChange() fires.
  Hearth.poll();

  // Ask the device where it stands, on a timer. Drive the LED from the last
  // good read, and narrate only real transitions.
  if (millis() - lastPollAt > statePollMs) {
    lastPollAt = millis();
    MatterDeviceState s;
    unsigned int fabrics = 0;
    if (Matter.deviceState(&s, &fabrics)) {
      liveState = s;
      if (s != printedState || fabrics != printedFabrics) {
        printedState = s;
        printedFabrics = fabrics;
        Serial.printf("State: %s; fabrics: %u\r\n", stateName(s), fabrics);
      }
    }
  }

  // The LED mirrors commissioning state: off, blink or solid.
  if (liveState == MATTER_STATE_COMMISSIONING) {
    if (millis() - lastBlinkAt > blinkMs) {
      lastBlinkAt = millis();
      blinkOn = !blinkOn;
      digitalWrite(ledPin, blinkOn ? HIGH : LOW);
    }
  } else {
    digitalWrite(ledPin, liveState == MATTER_STATE_OPERATIONAL ? HIGH : LOW);
  }

  // The BOOT button: short press reopens the pairing window, a five-second
  // hold decommissions.
  if (digitalRead(buttonPin) == LOW && !buttonDown) {
    buttonDown = true;
    longHandled = false;
    buttonPressedAt = millis();
  }
  if (buttonDown) {
    uint32_t held = millis() - buttonPressedAt;
    if (!longHandled && held > longPressMs) {
      longHandled = true;  // fire once, even while still held
      Serial.println("Long press: decommissioning. The device will forget every fabric.");
      light.setOnOff(false);
      Matter.decommission();
    }
    if (digitalRead(buttonPin) == HIGH) {  // released
      buttonDown = false;
      if (!longHandled && held > debounceMs) {
        Serial.println("Short press: reopening the pairing window (same fixed pairing code).");
        if (Matter.openCommissioningWindow()) {
          Serial.println("  Window open. A controller can adopt the device now.");
        } else {
          Serial.println("  The firmware did not accept the request.");
        }
      }
    }
  }

  // No delay(): every millisecond asleep is latency a controller feels.
}
