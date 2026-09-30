/*
 * HearthFirmwareUpdate: the same on/off light as HearthFirstLight, with the
 * host side of the co-processor's FOTA turned on. Once the co-processor is
 * running Hearth and commissioned, this sketch downloads a signed bundle
 * when an OTA provider offers one, verifies it, asks for consent, applies
 * the Hearth part over the co-processor's UART, stages and reboots into
 * the host part, and rolls back to the retained image if the new one
 * fails. Every transition of the update prints on the Serial Monitor, and
 * the BOOTSEL button keeps its job.
 *
 * FLASH SIZE, in the Arduino IDE's Tools > Flash Size menu: the LittleFS
 * partition must hold the co-processor's image, and begin() refuses to
 * start FOTA when it is smaller. The need is per port: an ESP32-C6 takes
 * 6,815,744 bytes (6.5 MiB), so the Challenger 2350 wants "8MB (Sketch:
 * 1MB, FS: 7MB)"; a sketch over 1 MB then needs a 16 MB board. An
 * nRF54L15, an nRF54LM20A and an MGM240P take 3,670,016 bytes (3.5 MiB),
 * so the CPico 2350 wants "8MB (Sketch: 4MB, FS: 4MB)". A board flashed
 * with no FS partition at all gets HEARTH_UPDATE_ERR_NO_FS from begin()
 * instead, and this sketch then runs as the plain light.
 *
 * THE SIGNING KEY. This build verifies bundles against the development
 * key, fw/keys/hearth_bundle_dev_p256.pem, which is public: it gives
 * integrity, not authenticity, so anyone can sign a bundle this sketch
 * accepts. A product generates its own key pair and passes its public key
 * in HearthUpdateConfig::publicKey (src/HearthDevKey.h says the same
 * thing).
 *
 * BUILDING A BUNDLE for this sketch. The Matter header of a bundle must
 * carry the device's own vendor and product id, or the co-processor's OTA
 * requestor ignores the offer. The development builds use vendor 0xFFF1
 * with product 0x8000 (the ESP32-C6) and 0x8010 (the MGM240P); an nRF uses
 * its SDK's default. Read any device's ids with:
 *
 *   chip-tool basicinformation read product-id <node> 0
 *
 * First export this sketch's own image, which becomes the bundle's host
 * part:
 *
 *   arduino-cli compile --fqbn <board> --export-binaries examples/HearthFirmwareUpdate
 *
 * The .bin it writes under the sketch's build/ directory is the --host
 * file. The bundle's --version must be higher than the baseline this
 * sketch declares (0x00010000 below) for the update to be accepted. A
 * host-only bundle at 1.1.0 (0x00010100):
 *
 *   python3 fw/make_bundle.py --vendor 0xFFF1 --product 0x8000 \
 *     --version 0x00010100 --version-string 1.1.0 \
 *     --key fw/keys/hearth_bundle_dev_p256.pem \
 *     --host <sketch>.bin \
 *     -o product-1.1.0.ota
 *
 * and one with both parts, this sketch plus a WiFi build of the C6 image:
 *
 *   python3 fw/make_bundle.py --vendor 0xFFF1 --product 0x8000 \
 *     --version 0x00010100 --version-string 1.1.0 \
 *     --key fw/keys/hearth_bundle_dev_p256.pem \
 *     --host <sketch>.bin \
 *     --fw hearth-wifi-<version>.bin --target "ESP32-C6 Hearth" \
 *     --variant wifi --fw-version <version> \
 *     -o product-1.1.0.ota
 *
 * The host part must be the uncompressed .bin: the library refuses a gzip
 * host part. The Hearth part is applied only when its version differs from
 * what the co-processor reports to AT+MTVER?.
 *
 * For the nRF, --fw is the MCUboot-signed zephyr.signed.bin, --target the
 * exact AT+CGMM answer ("nRF54L15 Hearth" or "nRF54LM20A Hearth") and
 * --variant thread; for the MGM240P, --fw is the .gbl, --target "MGM240P
 * Hearth" and --variant thread. A bundle may carry either part or both,
 * and fw/check_bundle.py can verify one against the public key before you
 * offer it.
 *
 * OFFERING IT, the two ways (step by step in fw/README.md):
 * (a) Home Assistant: put the .ota and the descriptor from
 *
 *   python3 fw/ota_descriptor.py <bundle>.ota --url <where HA fetches it> -o <bundle>.json
 *
 * in the Matter server's provider directory, and the device shows an
 * update entity. (b) A bench: run the SDK's chip-ota-provider-app with
 * the bundle, commission the provider, write the provider's ACL so the
 * device's QueryImage may reach it, then
 *
 *   chip-tool otasoftwareupdaterequestor announce-otaprovider <provider-node> 0 0 0 <device-node> 0
 */

#include <Matter.h>

/*
 * The same light as HearthFirstLight's: one endpoint a controller sees as
 * a switchable light, the board's LED doing the work. The global is what
 * the consent hook reads, through light.getOnOff().
 */
MatterOnOffLight light;

/* The LED this light drives, and the BOOTSEL button that toggles it
 * locally. Both come from the library, as in HearthFirstLight. */
const uint8_t ledPin = LED_BUILTIN;
const uint8_t buttonPin = BOOT_PIN;

/* Button debounce state, as in HearthFirstLight. */
uint32_t buttonPressedAt = 0;
bool buttonDown = false;
const uint32_t debounceMs = 250;

/*
 * The update's config. On a Challenger 2350 the reset and strap pins come
 * from the variant (PIN_ESP_RST and PIN_ESP_MODE) and stay at their
 * defaults. On a board that does not define those macros (the CPico 2350
 * carrier), setup() sets them to the pins the carrier wires before begin()
 * runs. A flasher with a pin of -1 would refuse to enter boot mode.
 */
HearthUpdateConfig updateCfg;

/*
 * The state and error names the status callback prints. Kept in noinline
 * helpers for the same reason HearthFirstLight keeps its print helpers
 * that way: a String temporary would be hoisted into the caller's frame
 * on every call whether it ran or not, and these run from inside
 * Hearth.poll(), on a stack that is not large.
 */
static char const * __attribute__((noinline)) stateName(HearthUpdateStateEnum s) {
  switch (s) {
    case HEARTH_UPDATE_UNAVAILABLE:   return "UNAVAILABLE";
    case HEARTH_UPDATE_DISABLED:      return "DISABLED";
    case HEARTH_UPDATE_IDLE:          return "IDLE";
    case HEARTH_UPDATE_DOWNLOADING:   return "DOWNLOADING";
    case HEARTH_UPDATE_VERIFYING:     return "VERIFYING";
    case HEARTH_UPDATE_WAIT_APPLY:    return "WAIT_APPLY";
    case HEARTH_UPDATE_APPLYING_FW:   return "APPLYING_FW";
    case HEARTH_UPDATE_APPLYING_HOST: return "APPLYING_HOST";
    case HEARTH_UPDATE_FAILED:        return "FAILED";
  }
  return "?";
}

static char const * __attribute__((noinline)) errName(HearthUpdateErr e) {
  switch (e) {
    case HEARTH_UPDATE_OK:            return "OK";
    case HEARTH_UPDATE_ERR_NO_FS:     return "ERR_NO_FS";
    case HEARTH_UPDATE_ERR_UNSUPPORTED: return "ERR_UNSUPPORTED";
    case HEARTH_UPDATE_ERR_LINK:      return "ERR_LINK";
    case HEARTH_UPDATE_ERR_BUNDLE:    return "ERR_BUNDLE";
    case HEARTH_UPDATE_ERR_FLASH:     return "ERR_FLASH";
    case HEARTH_UPDATE_ERR_HOST:      return "ERR_HOST";
    case HEARTH_UPDATE_ERR_NO_SPACE:  return "ERR_NO_SPACE";
    case HEARTH_UPDATE_ERR_COPROC:    return "ERR_COPROC";
  }
  return "?";
}

/*
 * The apply consent hook. It runs when the verdict is in and the bundle
 * is staged. A refusal is not a no on the wire: the bundle stays staged
 * and the library re-asks while the consent window runs
 * (consentWindowMs, ten minutes by default), then abandons the offer.
 * This sketch refuses while the light is on, a stand-in for "not during a
 * charge", and re-asks itself on every retry, so a refusal here is a
 * wait, not a no.
 */
static bool onApplyRequest() {
  if (light.getOnOff()) {
    Serial.println("Update apply refused: the light is on (not during a charge).");
    return false;
  }
  return true;
}

/*
 * Fired on every state change (and on the QUERYING state, which changes
 * nothing else). Prints the state name, the percent while downloading,
 * the error name and reason on FAILED, the offered version on an offer,
 * and the deferred seconds when the provider defers. The offered version
 * and the deferral both arrive with the state still IDLE, so they are
 * printed there, where they are nonzero.
 */
static void onUpdateStatus(const HearthUpdateStatus &st) {
  Serial.print("update: ");
  Serial.print(stateName(st.state));
  if (st.state == HEARTH_UPDATE_DOWNLOADING) {
    Serial.print(", ");
    Serial.print(st.percent);
    Serial.print("%");
  }
  if (st.state == HEARTH_UPDATE_IDLE && st.offeredVersion != 0) {
    Serial.print(", offered ");
    Serial.print(st.offeredVersion);
  }
  if (st.state == HEARTH_UPDATE_IDLE && st.deferredSeconds != 0) {
    Serial.print(", deferred ");
    Serial.print(st.deferredSeconds);
    Serial.print(" s");
  }
  if (st.state == HEARTH_UPDATE_FAILED) {
    Serial.print(", ");
    Serial.print(errName(st.error));
    Serial.print(", reason ");
    Serial.print(st.reason);
    if (st.detail[0] != 0) {
      Serial.print(", detail ");
      Serial.print(st.detail);
    }
  }
  Serial.println();
}

/* The controller calls this, exactly as in HearthFirstLight. */
bool onLightChange(bool state) {
  digitalWrite(ledPin, state ? HIGH : LOW);
  Serial.print("Light is now ");
  Serial.println(state ? "ON" : "OFF");
  return true;
}

void setup() {
  Serial.begin(115200);
  /* Wait for the Serial Monitor to attach, but not forever: a board on a
   * USB charger has nobody to wait for. */
  while (!Serial && millis() < 8000) {}

  Serial.println();
  Serial.println("Hearth firmware update");

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  /* The first call into the library brings the link up: it resets the
   * co-processor into run mode where the board wires its reset line and
   * waits for its +MTREADY. An empty answer means the link is not working.
   */
  String version = Hearth.firmwareVersion();
  Serial.print("Firmware on the co-processor: ");
  if (version.length() == 0) {
    Serial.println("(no answer, see fw/README.md)");
  } else {
    Serial.println(version);
  }

  light.onChange(onLightChange);
  light.begin(false);

  Serial.println("Endpoint declared. Starting Matter...");

  /*
   * Matter.begin() is the step that talks to the C6. Call it last in
   * setup(), after every endpoint's own begin(), and never from loop().
   */
  Matter.begin();

  /* Register the update's hooks before begin(), for the same reason
   * HearthFirstLight registers its light callback before Matter.begin():
   * so the first state has somewhere to go when it arrives. The baseline
   * is 0x00010000 ("1.0.0"): a bundle is accepted only when its product
   * version is higher. A false return means FOTA is off for this boot,
   * no filesystem, no space or no link: print the error and keep running
   * as the plain light. */
  Hearth.update.onStatus(onUpdateStatus);
  Hearth.update.onApplyRequest(onApplyRequest);
#if !defined(PIN_ESP_RST)
  updateCfg.resetPin = 2;   /* the CPico 2350 carrier: reset on GP2 */
  updateCfg.strapPin = 3;   /* and the boot strap on GP3, both active low */
#endif
  if (!Hearth.update.begin(0x00010000, "1.0.0", updateCfg)) {
    const HearthUpdateStatus st = Hearth.update.status();
    Serial.print("Hearth.update is off (");
    Serial.print(errName(st.error));
    if (st.reason != 0) {
      Serial.print(", reason ");
      Serial.print(st.reason);
    }
    Serial.println("): running as a plain light.");
  } else {
    Serial.println("FOTA on. Downloads run alongside the light;");
    Serial.println("the apply blocks loop() while it flashes.");
  }
}

void loop() {
  /*
   * First, every iteration, unconditionally. It is what delivers the URCs
   * the update runs on, and what makes onLightChange() fire.
   */
  Hearth.poll();

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
}
