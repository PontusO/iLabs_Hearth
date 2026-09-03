# iLabs Hearth

Build a **Matter** device with an Arduino sketch on an iLabs Challenger
board. A light, a sensor, a thermostat or a dishwasher, commissioned onto a
Matter fabric and driven from a Matter controller the way any other Matter
accessory is, written with the same `Matter` API that `arduino-esp32`
sketches use.

Hearth is two halves: **this library**, which runs on the board's RP2350
and gives your sketch that API, and the **Hearth firmware**, which runs on
the board's ESP32-C6 co-processor and speaks Matter to the network. The
library ships the firmware images and a flasher in [`fw/`](fw/), so you
never build the firmware yourself.

**Never used it before? [Start here](#start-here).** It goes from a board
in a drawer to a light you can switch from your phone. The board half is
about half an hour, most of it waiting for downloads. The other half is
getting a Matter controller to adopt it with, and that ranges from minutes
(an Android app) to an evening (building the CLI `chip-tool` from the
Matter SDK), so [step 4](#4-commission-it) is worth reading before you
start rather than when you get there.

Already know your way around? [Firmware and library
versions](#firmware-and-library-versions), [the examples
map](#the-examples-map), [how it works](#how-it-works), or read on for the
reference sections: the [event loop](#driving-the-event-loop), the
[supported device types](#supported-device-types), the
[examples](#examples) and the [limitations](#limitations).

## Start here

### What you need

| | |
|---|---|
| **Board** | An iLabs Challenger RP2350 WiFi6/BLE5, and a USB-C cable that carries data. |
| **Arduino IDE** | 2.x, from arduino.cc. |
| **The arduino-pico core** | "Raspberry Pi Pico/RP2040/RP2350" by Earle F. Philhower, III. Paste its index URL into **File > Preferences > Additional boards manager URLs**, then install it from **Boards Manager**: `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`. This release is verified against core 5.5.1 and 6.0.0. |
| **Python 3** | Only to flash the co-processor, once per board (step 2). The flasher runs on Linux and macOS; on Windows one of its two stages has to be done by hand, see [`fw/README.md`](fw/README.md). |
| **A Matter controller** | Not optional and not a free choice: an uncertified device is refused by most consumer hubs. The **NXP Matter Chip-tool** Android app is verified against this firmware and takes minutes to install; the CLI `chip-tool` also works and takes hours to build. Step 4 has the table, the links and the honest costs. Settle this before you start if you can. |
| **A network** | WiFi in almost every case. Thread works too and needs a border router; step 2 is where you choose. |

You do not need an ESP-IDF toolchain, an esp-matter checkout, or any
knowledge of the `AT+MT` protocol the two chips speak between themselves.

### 1. Install this library

The library is not in the Arduino Library Manager. Install it from the
repository:

- **From a ZIP**: download
  [`iLabs_Hearth`](https://github.com/PontusO/iLabs_Hearth) as a ZIP, then
  in the IDE choose **Sketch > Include Library > Add .ZIP Library** and
  pick the file.
- **Or with git**, into your sketchbook's `libraries` folder (the
  sketchbook location is in **File > Preferences**):

  ```sh
  cd ~/Arduino/libraries
  git clone https://github.com/PontusO/iLabs_Hearth
  ```

Restart the IDE afterwards so it picks up the new examples.

### 2. Flash the co-processor

**Do this once per board.** The Challenger has two chips: the RP2350 runs
your sketch, and the ESP32-C6 next to it is a co-processor. Matter runs on
the C6, and it needs the Hearth firmware before any sketch can do
anything. A board fresh out of its bag is not running it.

The images and the flasher live in this library, in [`fw/`](fw/). The
flasher has one prerequisite, and it refuses to start without it rather
than half-flashing a board, so install that first: the **iLabs fork of
esptool**. Stock `pip install esptool` cannot flash these boards, and no
newer release of it can either. The C6 has no USB of its own, so the reset
lines esptool toggles reach it only through the RP2350, and only the fork
knows how to drive that. Clone it, install its dependencies, and tell the
flasher where it is:

```sh
git clone https://github.com/PontusO/esptool ~/bin/esptool
pip install -e ~/bin/esptool
export ILABS_ESPTOOL_PATH=~/bin/esptool
```

Now check that much without touching the board. From the library's own
directory (the sketchbook path is whatever **File > Preferences** shows):

```sh
cd ~/Arduino/libraries/iLabs_Hearth
python3 fw/flash.py --list
```

It should answer `esptool fork OK` and print the three variants. Then plug
the board in and flash it:

```sh
python3 fw/flash.py
```

It offers the same three variants and **1, WiFi only, is the one to choose
unless you know you want Thread.** It then reboots the board into a
USB-to-serial bridge and writes the firmware to the C6 over it, with a
progress bar.

**[`fw/README.md`](fw/README.md) is the full account**: the prerequisites,
what each variant is for, how many endpoints each one carries, every
flasher option, and what to do when a step fails. Read it if `flash.py`
tells you something you did not expect.

### 3. Open `HearthFirstLight`

In the IDE:

1. **Tools > Board**, then the arduino-pico core's submenu (it is labelled
   "Raspberry Pi RP2040/RP2350 Boards" or "Raspberry Pi RP2040(x.y.z)",
   depending on the core release), and in it **iLabs Challenger 2350
   WiFi/BLE**.
2. **File > Examples > iLabs Hearth > HearthFirstLight**.
3. **Tools > Port**, and pick the board. Then upload. The upload replaces
   the serial bridge that step 2 left on the RP2350, which is expected. If
   the IDE cannot see the board at all, hold **BOOTSEL**, tap **RESET**,
   release BOOTSEL, and upload again.
4. Open **Tools > Serial Monitor** and set it to **115200 baud**.

**If you work from the command line instead**, the board's FQBN is
`rp2040:rp2040:challenger_2350_wifi6_ble5`. It is the same string the
[endpoint ceiling](fw/README.md#how-many-endpoints-fit) section needs, so
it is worth keeping:

```sh
cd ~/Arduino/libraries/iLabs_Hearth
arduino-cli compile -b rp2040:rp2040:challenger_2350_wifi6_ble5 examples/HearthFirstLight
arduino-cli upload  -b rp2040:rp2040:challenger_2350_wifi6_ble5 -p <port> examples/HearthFirstLight
arduino-cli monitor -p <port> -c baudrate=115200
```

`arduino-cli board list` names the port. Prefer its stable
`/dev/serial/by-id/usb-iLabs_Challenger_...-if00` form over `/dev/ttyACM0`,
which is only whichever device enumerated first.

You should see:

```
Hearth first light
Firmware on the co-processor: 1.1.0
Endpoint declared. Starting Matter...

Not commissioned yet. Add this device in your Matter app.
  Manual pairing code: 34970112332
  QR code URL:         https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00
```

That is a working device. "Not commissioned" is the normal state of a
Matter accessory nobody has adopted yet.

**Those two codes will match the ones printed above exactly, and that is
correct.** They are not a screenshot and not a sign of a bad flash. This
development build carries the SDK's fixed test credentials, discriminator
`3840` and setup passcode `20202021` (vendor ID `0xFFF1`), so **every board
running this firmware prints the same pairing code and the same QR
payload**. A production Matter device gets a per-device passcode and
per-device attestation certificates provisioned at manufacture; this one
deliberately does not, which is the same fact that makes consumer hubs
refuse it in step 4.

Two consequences worth knowing before you leave a board switched on:

- **The pairing code is not a secret.** It is published in the Matter SDK.
  Anyone in Bluetooth range can commission a board that is sitting there
  advertising, for as long as its 15 minute window is open. A board that
  has been commissioned stops advertising, so the exposure is that window
  rather than the life of the device.
- **Two boards cannot be told apart by their code.** If somebody nearby is
  running this firmware too, your app can offer you their board. Commission
  one at a time, and check that the device that appeared is the one you
  just powered up.

### 4. Commission it

**You need a Matter controller, and which one is not a free choice.** This
firmware is uncertified and uses Matter's public development credentials
(vendor ID `0xFFF1`, the test attestation certificates that ship with the
SDK). A controller is entitled to refuse a device presenting those, and the
big consumer hubs are expected to, either outright or behind an
uncertified-accessory warning. That is what every Matter project looks like
before its vendor pays for certification, not a defect in this firmware.

| Controller | Status | How to get it |
|---|---|---|
| **NXP Matter Chip-tool** (Android) | **Verified against this firmware**, 2026-07-28: commissioned a Challenger over BLE, the C6 joined WiFi, and the light was driven from the controller. Android only, no iOS build. | [Google Play](https://play.google.com/store/apps/details?id=com.verik.mattercontrol). Minutes. |
| **`chip-tool`, the CLI** (Linux, macOS) | **Verified**: every regression baseline in this project was recorded by driving `chip-tool` against a Challenger. | Built from source, see below. Hours. |
| Apple Home, Google Home, Alexa | **Expected to refuse** the development credentials. Not a bug worth debugging. | n/a |
| Anything else, including Home Assistant's Matter integration | **Untested here.** Not known to fail, simply no evidence either way. | n/a |

**If you want the CLI `chip-tool`, know what you are agreeing to.** It is
built from the Matter SDK, which means a multi-gigabyte `connectedhomeip`
checkout with submodules, a toolchain bootstrap, and a build measured in
tens of minutes to hours, plus a working BlueZ Bluetooth stack for the BLE
commissioning. Upstream documents it and this README will not duplicate it:
[Working with the CHIP
Tool](https://github.com/project-chip/connectedhomeip/blob/master/docs/development_controllers/chip-tool/chip_tool_guide.md)
for the tool, and
[BUILDING.md](https://github.com/project-chip/connectedhomeip/blob/master/docs/guides/BUILDING.md)
for the prerequisites underneath it. (That guide also mentions an Ubuntu
snap as an alternative to building; this project has not used it.)

Two shortcuts worth knowing. **If you have esp-matter installed**, for the
firmware or for anything else, you already have the SDK: `chip-tool` builds
out of the `connectedhomeip` tree inside it, no second clone. And **build
it from the revision your device's stack matches** where you can, which for
this firmware is what `esp-matter release/v1.5` pins; upstream's own guide
gives the same advice, because a controller and a device from distant
revisions can disagree.

**Then commission it.** With the app: open the QR code URL (it renders the
code your phone will scan), then in your Matter app choose to add a device
and scan it. Type the manual pairing code instead if the app offers that.
Commissioning runs over Bluetooth LE and takes a minute or two; the app
hands your WiFi credentials to the C6 as part of it, so the sketch never
sees a password.

With the CLI, the pairing command carries the credentials rather than
scanning them, and they are the fixed development pair from step 3
(passcode `20202021`, discriminator `3840`). The node id is yours to
choose:

```sh
chip-tool pairing ble-wifi 0x1234 "<your-ssid>" "<your-psk>" 20202021 3840
chip-tool onoff toggle 0x1234 1          # the light, endpoint 1
```

(On Thread it is `pairing ble-thread 0x1234 hex:<operational-dataset>
20202021 3840`. Treat that dataset as a credential: it contains the network
key.)

When it finishes, the serial monitor says so, and the switch in your app or
your `chip-tool` command drives the board's LED. The board's **BOOTSEL**
button toggles the same light from this end and the controller follows
along, which is the two-way path working.

Commissioning happens once. Every later boot goes straight to
"Commissioned".

**The pairing window is open for 15 minutes after boot**, and this sketch
does not ask whether it has closed, so it keeps printing the same pairing
code afterwards. If a pairing attempt fails and the board has been powered
for a while, press **RESET** and try again with the fresh code. A sketch can
also reopen the window without a reboot, and ask whether one is open: see
[Reopening the pairing window](#reopening-the-pairing-window).

### If something does not work

| What you see | What it means |
|---|---|
| Nothing at all in the serial monitor | The monitor is on the wrong port or the wrong baud rate. It is 115200. |
| `arduino-cli upload` fails with `No drive to deploy` | Its automatic 1200-baud touch reset did not take, which happens. Reset the board into its bootloader by hand and re-run the upload: hold **BOOTSEL**, tap **RESET**, release, or do the touch yourself with the same open-at-1200-with-DTR-low that `flash.py` uses: `python3 -c "import serial,sys,time; s=serial.Serial(port=None,baudrate=1200); s.port=sys.argv[1]; s.dtr=False; s.open(); time.sleep(0.15); s.close()" <port>` |
| `Firmware on the co-processor: (no answer, ...)` | The sketch cannot reach the C6. Either it was never flashed (step 2), or the serial bridge from step 2 is still on the RP2350 and the sketch upload did not take. |
| The version prints, but no pairing code appears | The device is already commissioned. It says so on the line before. Remove it from your Matter app to start over. |
| `flash.py` refuses to start | It is telling you which prerequisite is missing. [`fw/README.md`](fw/README.md) quotes the refusals verbatim and gives the fix for each. |
| The app finds the device, then refuses to add it | Almost always the development credentials described in step 4. Use NXP's `chip-tool` app rather than a consumer hub. |
| Pairing fails on a board that has been on for a while | The 15 minute commissioning window has closed. Press RESET and use the code printed after the reboot, or have the sketch call `Matter.openCommissioningWindow()`. |
| Callbacks fire late, or not at all | `loop()` is not calling into the library often enough. See [Driving the event loop](#driving-the-event-loop). |

### Where to go next

`HearthFirstLight` is one endpoint of one type. The rest of the examples
are laid out in [the examples map](#the-examples-map) below: one sketch per
device type to copy from, and a `FullAPI` tier that exercises every call a
class has.

Then, in rough order of when you will need them: [driving the event
loop](#driving-the-event-loop) (the one way this library genuinely differs
from `arduino-esp32`), [the supported device types](#supported-device-types),
and [the limitations](#limitations).

## Firmware and library versions

The library and the firmware are released together and are tested as a
pair. The pairing is about honest failure reporting rather than features:
an older firmware than the row says generally still runs, and misreports
some failures. **Run the pair a row names, and nothing else is promised.**
1.0.0 is a feature-completeness milestone, not a stability contract: the
`AT+MT` wire surface is not frozen by it, and a newer firmware is not
guaranteed to be a superset of an older one. The surface has already both
removed and changed things (`+MTCOMMISSION:STARTED`/`:COMPLETE`/`:FAILED`
were removed in phase C3, and several commands have changed which error
code they answer with), so a mismatched pair is a combination nobody tested
rather than a combination known to work.

| Library | Firmware | Notes |
|---|---|---|
| 1.1.0 | 1.1.0 | Adds `Matter.openCommissioningWindow()` and `Matter.deviceState()`. Ships the matching images in `fw/`. |
| 1.0.0 | 1.0.0 | The feature-completeness milestone. |
| 0.12.1 | 0.12.0 | EVSE stack margin fix; library-only, no firmware change. |
| 0.12.0 | 0.12.0 | Energy round C2: EVSE and electrical utility meter. |
| 0.11.0 | 0.11.0 | Thread role and mesh identity. |
| 0.10.0 | 0.10.1 | Firmware 0.10.1 corrects three ambiguous attribute-write failures that 0.10.0's own classes can hit. |

Ask the board which firmware it is running rather than remembering: the
version `HearthFirstLight` prints in step 3 comes from
`Hearth.firmwareVersion()`, which is the C6 answering. It should match
`version=` in this library's `library.properties`.

## The examples map

**The IDE lists examples alphabetically, and there is no ordering hidden in
the names.** This table is the map. The `Matter*` sketches at the examples
root are byte-identical copies of `arduino-esp32`'s own, which is the
evidence that an unmodified upstream sketch runs here, so they are never
renamed, renumbered or reordered.

| Example | What it is |
|---|---|
| **`HearthFirstLight`** | **Start here.** One on/off light, heavily commented for a first read: what `Matter.begin()` does, why the first boot needs commissioning, what the serial monitor should say. |
| `HearthPairingButton` | Hearth original: a BOOT-button press reopens the commissioning window on an already-commissioned device, and the LED shows live commissioning state. Exercises the two calls upstream does not have, `Matter.openCommissioningWindow()` and `Matter.deviceState()` (1.1.0). |
| `MatterOnOffLight`, `MatterDimmableLight`, `MatterTemperatureSensor`, and 16 more at the examples root | One device type each, copied byte-identical from `arduino-esp32`. Copy from these when you want a specific device type. |
| `MatterDoorLockAdjudicated`, `HearthSensorsAndAppliances` | Hearth originals: several classes composed into something closer to a real device, with an interactive serial menu. |
| `FullAPI/` (54 sketches) | The deep end: one sketch per endpoint class (plus one for the Thread role surface, which belongs to no class), exercising every public member it has, with the equivalent `chip-tool` command printed for each observable effect. Reference material, not a starting point. |

The [Examples](#examples) section further down describes each tier in
detail, including which upstream sketches were copied and why the WiFi
block in them compiles out here.

## How it works

Your sketch calls `Matter.*` or an endpoint object. This library turns each
call into an `AT+MT` command, sends it over the UART the board wires
between the RP2350 and the ESP32-C6, and reads the answer. The Matter stack
itself, the fabric, the BLE commissioning and the network credentials all
live on the C6, running the
[`iLabs_AT_Hearth`](https://github.com/PontusO/iLabs_AT_Hearth) firmware.
Changes coming the other way (a controller switching your light) arrive as
unsolicited lines on that UART, which is why
[`Hearth.poll()`](#driving-the-event-loop) exists.

The class API mirrors `arduino-esp32`'s `Matter` library closely enough
that an **unmodified** arduino-esp32 Matter sketch builds and runs on a
Challenger, which is the property the parity examples exist to prove.

## Hearth and Matter

The library's identity is **Hearth**, never Matter. `iLabs Hearth` is the
package name, `iLabs_Hearth` is the repo, and this README, the sentence and
paragraph in `library.properties`, and every doc title in this project say
Hearth. Matter appears only as the name of the protocol being spoken, which
is descriptive use; it is not, and must not read as, the name of this
product. The word mark belongs to the Connectivity Standards Alliance and
naming a commercial product after it requires paid adopter membership this
project does not have.

Against that constraint sits a hard requirement: an unmodified sketch is
verbatim `#include <Matter.h>`, `MatterOnOffLight light;`, `Matter.begin()`.
None of those identifiers can change without breaking the parity this
library exists to provide. The resolution is a split kept throughout the
source:

- **Interop symbols** (class names, the `Matter` global, method signatures,
  `matterEvent_t` and its values) are a closed set defined by
  `arduino-esp32` and reproduced verbatim. Nothing is added to them, because
  every addition would be new surface hanging off a Matter-named identifier.
- **Everything this library needs that upstream has no equivalent for** (the
  transport link, diagnostics, co-processor reboot detection, `+MTERR`
  detail) lives on a second global, **`Hearth`**, and its `Hearth*`/
  `hearth*`-prefixed members.

The full design rationale, including why this diverges from the sibling
`iLabs_ESP-NOW` host library (which has no such split, because `ESP_NOW`
carries no trademark), is recorded in
`superpowers/specs/2026-07-27-c4-host-library-naming-design.md`, in the
private `iLabs_Hearth_docs` repository.

That repository is where every document this library cites lives:
`AT_MT_SPEC.md` (the AT wire contract, and the authority for anything this
README says about the firmware), `ARCHITECTURE.md`, `TESTING.md` and the
design specs and plans. Comments in `src/` and in the examples cite them by
name and section, for example `AT_MT_SPEC.md` §3.9, and resolve there rather
than against any path in this repository or the firmware's. It is private,
and it is tagged with the same version as each release, so the documents
matching a given library are a checkout rather than a guess.

## Wiring

**A sketch does not name a serial port.** The board variant is the single
source of truth for which UART reaches the co-processor and which pins
drive its reset and boot-mode lines, exactly as in the sibling
`iLabs_ESP-NOW` library. There is nowhere to name a port in the
arduino-esp32 API this library mirrors, and the wiring is a property of the
board rather than of the sketch.

On a Challenger RP2350 WiFi6/BLE5 the variant supplies:

| Variant macro | Value | What it is |
|---|---|---|
| `ESP_SERIAL_PORT` | `Serial2` (GP4 TX, GP5 RX) | the AT link to the C6 |
| `PIN_ESP_MODE` | GP14 | boot mode: high selects run, low selects serial download |
| `PIN_ESP_RST` | GP15 | co-processor reset, active low |

Note that `Serial1` on that board is the *external* UART on GP12/13, not
the co-processor link. Earlier revisions of this library assumed it was.

The first use of the library brings `ESP_SERIAL_PORT` up at
`HEARTH_LINK_BAUD` (115200, matching the firmware's `AT_UART_BAUD`), then
resets the C6 into run mode and waits
up to `HEARTH_READY_TIMEOUT_MS` for its `+MTREADY`, discarding the boot ROM
chatter that shares this UART. That gives every host start the same
deterministic co-processor state, whether the host was power-cycled or
reset on its own. The reset does not touch the Matter fabric or the stored
endpoint composition; those are erased by the `AT+MTRESET` and
`AT+MTFRESET` commands (the latter also erases the composition), not by
a reboot.

The library refuses to compile for a board whose variant defines no
`ESP_SERIAL_PORT`. Override with `-DHEARTH_SERIAL_PORT=SerialN` for a board
no variant describes, and `-DHEARTH_LINK_BAUD=` or
`-DHEARTH_READY_TIMEOUT_MS=` to retune the other two.

The firmware does accept `AT+MTBAUD` to retune the link at runtime, and this
library deliberately does not use it. The rate is not persisted, so every
co-processor reset returns it to 115200, and the library resets the C6 on
every host start. Nothing in the Matter parity surface needs a faster link.
A sketch that wants one can drive `AT+MTBAUD` through
`Hearth.hearthCommand()` and restart the host UART itself.

Hardware flow control is not available: no C6 board routes RTS/CTS, so
`AT+MTFLOW` accepts only mode `0`. See `AT_MT_SPEC.md` §3.14, in the private
`iLabs_Hearth_docs` repository.

`Hearth.begin(stream)` remains as an escape hatch for a bench rig that puts
something else in the middle. The caller then owns that stream completely:
it must already be started at the right baud, and no automatic reset is
performed. Call `Hearth.hearthResetCoprocessor()` afterwards if the variant
does define the control pins.

## Minimal example

The shortest sketch that is a Matter device. For the same thing with every
step explained, open `HearthFirstLight` (see [Start here](#start-here)).

```cpp
#include <Matter.h>

MatterOnOffLight light;

void setup() {
  Serial.begin(115200);
  light.begin();      // declare the endpoint; no AT traffic yet
  Matter.begin();      // reconciles the composition against the C6, last
  if (!Matter.isDeviceCommissioned()) {
    Serial.println(Matter.getManualPairingCode());
  }
}

void loop() {
  // Any call into the library also pumps the link; see "Driving the event
  // loop" below. This is the call every upstream example already makes.
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Not commissioned yet.");
  }
  delay(500);
}
```

Endpoint objects' `begin()` only declares the endpoint locally; nothing goes
over the wire until `Matter.begin()` reconciles the sketch's declared
composition against the C6's live one. This is why every upstream example,
and this rule, calls `Matter.begin()` last, after every endpoint's own
`begin()`.

**Do not call `Matter.begin()` from `loop()`.** An earlier version of this
README suggested it. It is not harmless: on a composition that is refused
(an unimplemented device type, which the C6 rejects, or more endpoints than
`HEARTH_MAX_ENDPOINTS`, which is 24 and is refused by this library's
registry before the C6 is ever asked: the firmware itself accepts 28) every
call runs a clear, the endpoint writes, an apply and a co-processor reboot, so
calling it per iteration means unbounded NVS wear on the C6 and a device
that never finishes booting. The library now latches a failed reconcile for
the rest of the boot, so a stray repeat call is bounded rather than
catastrophic, but there is still no reason to make one: `Matter.begin()`
belongs in `setup()`.

**Calling `end()` then `begin()` on an endpoint object after `Matter.begin()`
has already run leaves it permanently unusable.** `end()` only clears the
object's own `started` flag; it does not remove the endpoint from the
declaration registry. A later `begin()` re-declares against that same
registry entry, and once `Matter.begin()` has reconciled, any declaration
arriving after that point is refused (`+MTERR:10`, the same code the wire
uses for a rejected composition change). `begin()` therefore returns `false`
and never sets `started` back to `true`, so every setter on that object
returns `false` from then on, with no crash and no further diagnostic. This
is exact upstream parity, not a Hearth bug: arduino-esp32 3.3.8 refuses a
second `begin()` on the same object the same way (`getEndPointId() != 0`
check, logged and returns `false`), so it is not being changed here, only
written down. `end(); begin();` is only safe *before* `Matter.begin()`.

## Reopening the pairing window

Two calls on `Matter` that arduino-esp32 does not have. They were added in
1.1.0 because the AT surface had both from the first release and the library
never wrapped them.

```cpp
// Is a window open right now, and how many fabrics hold the device?
MatterDeviceState state;
unsigned int fabrics;
if (Matter.deviceState(&state, &fabrics)) {
  // MATTER_STATE_UNINITIALIZED: no fabric and no open window
  // MATTER_STATE_COMMISSIONING: a window is open, the device is advertising
  // MATTER_STATE_OPERATIONAL:   at least one fabric
}

// Open a window on demand. 0 asks for the firmware's 300 s default; any
// other value is clamped to 180..900 s, Matter's own floor and ceiling.
Matter.openCommissioningWindow();
Matter.openCommissioningWindow(600);
```

A factory-fresh board opens a 15 minute window at boot on its own and needs
neither call. A commissioned board does not reopen one at boot, by design,
so once its window has closed there are exactly two ways to add another
fabric: a controller that already holds the device sends it Matter's
`OpenCommissioningWindow` command, or the host calls
`openCommissioningWindow()`. The host call is the one that needs no reboot
and no existing controller, so a "pair" button on a commissioned product is
this call rather than a reset. It returns `true` when the firmware accepted
the request; the window's progress then arrives through the commissioning
events `onEvent()` already delivers.

`deviceState()` is the resynchronisation read. `isDeviceCommissioned()` and
the event callbacks tell a running sketch what happened since it started; a
host that rebooted while the co-processor stayed up has missed those
events, and this is how it learns whether a window is open right now
without waiting for one to close. It returns `false` when the co-processor
did not answer, so a lost reply never reads as "uninitialised".

The pairing code is the same for every window on every board (fixed
development credentials, see step 3 of [Start here](#start-here)), so
reopening the window changes nothing about which code to type.

The `HearthPairingButton` example wires all of this to a board: a short BOOT
press calls `openCommissioningWindow()`, a five-second hold calls
`decommission()`, and the onboard LED blinks while a window is open and goes
solid once the device is on a fabric, driven by polling `deviceState()`.

## Driving the event loop

On an ESP32 the Matter stack runs in its own FreeRTOS task, so a controller
turning the light on reaches the sketch's `onChange()` handler no matter
what `loop()` is doing. There is no equivalent here: the RP2350 has nothing
running in the background, and a change arriving from the fabric shows up
as a `+MTATTR` line sitting in the host's UART buffer until something reads
it.

**Every call into this library reads that buffer first.** Any
`Matter.*` call, any endpoint method, anything that reaches the AT link
drains and dispatches whatever URCs are pending before it sends its own
command, and dispatches any that interleave with the reply. Upstream's own
examples call `Matter.isDeviceCommissioned()` on every `loop()` iteration,
and that alone is enough: an unmodified upstream sketch gets its change
callbacks with nothing added.

**A sketch whose `loop()` never calls into the library must call
`Hearth.poll()` itself**, or callbacks never fire:

```cpp
void loop() {
  Hearth.poll();   // drains pending +MTATTR / +MTEVT / +MTIDENT URCs
  // ... work that does not touch Matter or any endpoint object ...
}
```

Parity is therefore **conditional**, and worth being precise about: it holds
for a sketch that calls into the library at least once per iteration, which
covers all three shipped examples and the upstream pattern generally. It
does not hold for a sketch that sets everything up and then sits in a loop
doing unrelated work. Callback latency is also bounded by how often the
sketch calls in, not by the co-processor: a `loop()` that calls the library
once a second sees controller-driven changes up to a second late. There is
no upstream analogue of `Hearth.poll()` to hide this behind, which is why
it carries a Hearth name rather than being smuggled onto `Matter`.

**Late is not the only cost of calling in rarely: the link is lossy if you
leave it alone too long.** Nothing on this hardware can throttle the
co-processor (no C6 board routes RTS/CTS, so the firmware's `AT+MTFLOW`
accepts only mode 0), so the host UART's receive buffer is the only thing
holding a burst of URCs until the sketch reads it. The library raises that
buffer to `HEARTH_LINK_RX_BUFFER` bytes, 1024 by default, which is about
89 ms of continuous traffic at 115200. That was not always so: on the stock
arduino-pico buffer of 31 usable bytes, one controller `SelfTestRequest`
(53 contiguous bytes, with the `+MTCMD` the sketch needs arriving last) lost
its tail inside the UART driver every single time, and
`MatterSmokeCOAlarm::onSelfTest()` never ran. A `loop()` that can block for
longer than that between library calls should raise `HEARTH_LINK_RX_BUFFER`
rather than assume the link is lossless.

Two further consequences of the link being single-threaded and cooperative:

- A change callback runs **inside** whichever library call happened to
  dispatch it. Calling back into the library from that callback is refused
  rather than served (`HEARTH_CMD_REENTRANT`), because a nested command
  would consume the outer command's reply. Set a flag and act on it after
  the call returns.
- `Hearth.poll()` is likewise a no-op when called from a callback, for the
  same reason. Calling it from `loop()` is always safe.

### Your `onChange` fires from inside your own setter

`light.setOnOff(true)`, `setBrightness()`, `setColorTemperature()` and
`sensor.setTemperature()` all write with `AT+MTATTR` mode 1, and the
co-processor answers a mode-1 write with a `+MTATTR` URC (its own attribute
callback confirming the change) *before* the `OK`. That URC is dispatched
like any other, so **your `onChange` handler runs before the setter you
called has returned**, with the value the co-processor echoed back:

```cpp
light.onChange(setLightOnOff);
light.setOnOff(true);   // setLightOnOff(true) has already run by here
```

This is not a Hearth quirk. arduino-esp32 does the same: its `setOnOff()`
reaches `esp_matter::attribute::update()`, which calls the endpoint's
`attributeChangeCB` synchronously on the calling thread. A sketch written
for arduino-esp32 sees its handler fire from inside its own setter there
too. It is called out here only because it is easy to be surprised by, and
because of the one place where Hearth then differs:

**A library call made from that handler is refused.** The echo is dispatched
while the write is still in flight on the UART, so a nested
`Matter.isDeviceCommissioned()`, `otherLight.setOnOff()` or any other call
that reaches the AT link returns failure (`HEARTH_CMD_REENTRANT`, `-3`, from
`Hearth.hearthCommand()`; a `false` or an empty `String` from the API on
top of it) and puts nothing on the wire. On arduino-esp32 the same call is
served, because there is no shared serial link to protect. Set a flag in the
handler and act on it in `loop()`:

```cpp
volatile bool g_lightChanged = false;

bool setLightOnOff(bool state) {
  digitalWrite(ledPin, state);
  g_lightChanged = true;    // do not call the library from here
  return true;
}

void loop() {
  if (g_lightChanged) {
    g_lightChanged = false;
    otherLight.setOnOff(true);   // served normally: nothing is in flight
  }
}
```

Two smaller differences in the same area, for completeness:

- Mode 0 (`setAttributeVal()`) and mode 1 (`updateAttributeVal()`) both
  echo a `+MTATTR` URC to this host and both fire the sketch's handler; the
  firmware calls `esp_matter::attribute::set_val()` for mode 0, and that
  still runs the callback that raises the echo. What mode 0 actually
  suppresses is the *report to the fabric*: a host reflecting a change that
  came *from* a controller uses mode 0 so the fabric does not see its own
  change bounced back at it, not so the host's own handler stays quiet.
- Upstream calls `attributeChangeCB` on `PRE_UPDATE`, so a handler returning
  `false` there vetoes the write. Over the AT link the co-processor reports
  the change on `POST_UPDATE`, after it has already been applied, so the
  return value cannot veto anything. For a controller-driven change,
  returning `false` still suppresses the library's own cache update, as
  upstream's does. For a *local* write's own echo it does not: the setter
  that issued the write (e.g. `MatterOnOffLight::setOnOff()`) sets its
  cache unconditionally once the write itself succeeds, regardless of what
  the echo's own handler returned, so `false` from a handler only stops
  that particular echo from being treated as a fresh controller change; it
  does not roll back the cache the setter already committed.

## Thread role and mesh identity

`Hearth.threadInfo()`, `Hearth.threadRole()`, `Hearth.onThreadRoleChange()`
and `hearthThreadRoleName()` (library 0.11.0, firmware 0.11.0,
`AT_MT_SPEC.md` S3.27 / event bit 28) surface the Thread routing role and
mesh identity a Thread-image co-processor already carries on its
`ThreadNetworkDiagnostics` cluster. No `arduino-esp32` class has a Thread
role API on any SDK this library tracks, so this is a Hearth original, the
same as `onLinkEvent()`/`transportMismatch()` in "Wiring" above.

**Needs the Thread image to do anything.** On a WiFi image, or the combined
image booted in WiFi mode, there is no `ThreadNetworkDiagnostics` cluster on
endpoint 0 to read: `threadInfo()` returns `false` with `Hearth.lastError()
== HEARTH_ERR_NOT_SUPPORTED` (8), and `threadRole()` reports
`HEARTH_THREAD_UNSPECIFIED`.

Two read paths, deliberately different costs:

- `Hearth.threadInfo(HearthThreadInfo &out)` always round-trips
  (`AT+MTTHREAD?`) and fills every field, including the `has*` flags for
  the wire's four nullable ones (`hasChannel`/`hasPanId`/`hasExtPanId`/
  `hasPartitionId`): **a `has*` flag false is the only honest signal of
  "unknown"**, never the numeric value on its own. A real PAN ID of
  `0xFFFF`, an all-ones extended PAN ID, or a partition ID of `0xFFFFFFFF`
  renders on the wire identically to null; `AT_MT_SPEC.md` S3.27 documents
  this as a property of the underlying `Nullable` encoding, not something
  this command invented. `name` arrives as a plain, already-unescaped C
  string (up to 16 bytes plus a NUL): the wire's own quoting rule (`"`
  escaped as `\"`, `\` as `\\`, the first free-form string field in the
  `AT+MT` family) is this library's problem, not the sketch's.

  **`partitionId` is the one field where a true `has*` flag is not evidence
  of an active mesh membership** (bench round, design spec 2026-08-12
  S2.1 as corrected): a device with a Thread dataset installed but not
  yet attached already reports a real `channel`, `panId`, `extPanId` and
  `name`, and `partitionId` renders as `0x00000000` -- `hasPartitionId` is
  `true` and the value is a genuine `0`, not an unset field. CHIP's null
  gate is "no dataset installed", not "not attached" (S3.27). **`attached`
  is therefore the only reliable "am I on a network" predicate**: a sketch
  keying off `hasPanId`, `hasExtPanId` or a nonempty `name` gets a false
  positive for the whole window between dataset install and mesh
  attachment, because those fields describe the network the device has
  been told to join, not one it has joined. Check `attached`, not the
  presence of the id fields.
- `Hearth.threadRole()` returns a **cached** `HearthThreadRole` with **no
  wire traffic at all** -- not even a URC drain. Seeded to
  `HEARTH_THREAD_UNSPECIFIED` on `begin()` and refreshed by `threadInfo()`
  and by every `+MTEVT:28`, so a sketch polling its role every `loop()`
  iteration pays nothing for it. It never crashes on an enum value it does
  not recognise: the wire's own decimal fallback (a future SDK addition
  outside Matter's current seven-entry `RoutingRoleEnum`) and any token
  this library's parser fails to match both degrade to
  `HEARTH_THREAD_UNKNOWN` (255, chosen so it can never collide with a
  future `RoutingRoleEnum` member), **never to `HEARTH_THREAD_UNSPECIFIED`**.
  Those two are provably distinct: `UNSPECIFIED` is the wire's own honest
  "the Thread interface is down", and collapsing an unrecognised token onto
  it would misreport a device that is up, attached and running a role this
  library predates as if its radio were off (review round, design spec
  2026-08-12 S2.1's "degrades to a number rather than a lie" principle,
  which the library must not undo).

`Hearth.onThreadRoleChange(void (*cb)(HearthThreadRole))` registers the
role-change callback, a **plain function pointer** (`HearthDemControl`'s
`onPowerAdjust`/`onCancelPowerAdjust` convention, not `std::function`:
there is no verdict to return here, only a notification). It carries the
exact same reentrancy rule as every other URC-dispatched callback in this
library (see "Your `onChange` fires from inside your own setter" above,
and `HearthDeviceEnergyManagement`'s power-adjust pair): **it runs inside
URC dispatch**, so a wire write from inside it -- including calling
`threadInfo()` itself -- is refused (`HEARTH_CMD_REENTRANT`) and reaches
nothing. Set a flag in the callback and act on it from `loop()`:

```cpp
volatile bool g_roleChanged = false;
HearthThreadRole g_lastRole = HEARTH_THREAD_UNSPECIFIED;

void onRoleChange(HearthThreadRole role) {
  g_lastRole = role;
  g_roleChanged = true;   // do not call the library from here
}

void loop() {
  Hearth.poll();
  if (g_roleChanged) {
    g_roleChanged = false;
    Serial.println(hearthThreadRoleName(g_lastRole));
  }
}
```

**Registering is what subscribes** (bench round: on real hardware the
callback never fired at all, because nothing had ever asked the device to
send `+MTEVT:28` in the first place -- bit 28 is opt-in, `AT_MT_SPEC.md`
S3.11's default event mask has no Thread role bit, so the device was
correctly silent). The one call above, `Hearth.onThreadRoleChange(cb)`,
arms a background `AT+MTEVT?` / `AT+MTEVT=` read-modify-write that the
next `Hearth.poll()`/any library call carries out: it reads the mask
first, then OR's bit 28 in (or AND's it out for a `nullptr` registration)
and writes the result back whole, never blindly overwriting whatever else
a sketch or another part of this library already subscribed to. Passing
`nullptr` unsubscribes the same way, in reverse.

**The subscription survives a co-processor reboot with no sketch action.**
`AT_MT_SPEC.md` S3.11 states the mask lives in RAM only and reverts to the
firmware default on every reboot, expected (an `AT+MTEPAPPLY` composition
apply) or spontaneous -- so a subscription made once in `setup()` would
otherwise silently stop working the first time the C6 restarts, the same
shape of staleness this library's endpoint-composition reconcile already
guards against. This library re-arms the same read-modify-write on its
own on every `+MTREADY` while a callback is registered; a registration
made before the link even exists yet (before the first call that brings
it up) is deferred and applied the same way once it does.

`hearthThreadRoleName(HearthThreadRole)` returns a display string for any
of the seven named roles (`"UNSPECIFIED"`, `"UNASSIGNED"`,
`"SLEEPY_END_DEVICE"`, `"END_DEVICE"`, `"REED"`, `"ROUTER"`, `"LEADER"`)
and for `HEARTH_THREAD_UNKNOWN` (`"UNKNOWN"`), and falls back to
`"UNKNOWN"` for a value outside the whole enum rather than returning
`nullptr` or undefined text.

See `examples/FullAPI/HearthThreadRole/` for a full reference, and note
that sketch's own banner on why it does not call `Matter.begin()`: this
surface is not scoped to any declared endpoint, and the sketch declares
none.

## Supported device types

All twenty of arduino-esp32's `Matter*` endpoint classes exist today,
matching the firmware's own device type table (`AT_MT_SPEC.md` §3.9,
"Supported device types"). These twenty classes resolve
to nineteen device type IDs, as `MatterColorLight` and `MatterEnhancedColorLight`
both address the same `0x010D` wire endpoint:

| Class | Device type ID |
|---|---|
| `MatterOnOffLight` | `0x0100` |
| `MatterDimmableLight` | `0x0101` |
| `MatterColorTemperatureLight` | `0x010C` |
| `MatterTemperatureSensor` | `0x0302` |
| `MatterOnOffPlugin` | `0x010A` |
| `MatterDimmablePlugin` | `0x010B` |
| `MatterContactSensor` | `0x0015` |
| `MatterOccupancySensor` | `0x0107` |
| `MatterHumiditySensor` | `0x0307` |
| `MatterPressureSensor` | `0x0305` |
| `MatterRainSensor` | `0x0044` |
| `MatterWaterFreezeDetector` | `0x0041` |
| `MatterWaterLeakDetector` | `0x0043` |
| `MatterFan` | `0x002B` |
| `MatterWindowCovering` | `0x0202` |
| `MatterThermostat` | `0x0301` |
| `MatterEnhancedColorLight` | `0x010D` |
| `MatterGenericSwitch` | `0x000F` |
| `MatterColorLight` | `0x010D` |
| `MatterTemperatureControlledCabinet` | `0x0071` |

Declaring an unimplemented device type in a sketch fails to link, the same
as any other undefined symbol.

The last two are worth a note each. `MatterGenericSwitch` is event-driven,
not attribute-driven: `click()` sends `AT+MTSWITCH` and the firmware raises
an `InitialPress` CHIP event on the fabric; nothing comes back to the host
over that path, so there is no attribute cache to read and no `onChange` to
register. `MatterColorLight` rides the identical `0x010D` wire endpoint
`MatterEnhancedColorLight` does rather than a device type of its own; the
two are host-side views over the same firmware endpoint that differ only in
which of its clusters each class drives, and `MatterColorLight`'s is an
HS-only API: OnOff, CurrentLevel and CurrentHue/CurrentSaturation, with no
`setColorTemperature()` and no separate brightness accessor (brightness
lives in `colorHSV.v`, exactly as upstream has it).

Seven of these classes carry a documented Hearth-side addition beyond
upstream's own public API: `MatterContactSensor`, `MatterRainSensor`,
`MatterWaterFreezeDetector` and `MatterWaterLeakDetector` each add
`onChange(EndPointCB)`; `MatterHumiditySensor` and `MatterPressureSensor`
each add `onChange(...ChangeCB)` plus a public `getRaw*()`/`setRaw*()` pair
(`setRaw*` is protected upstream, `getRaw*` does not exist upstream at
all); `MatterOccupancySensor` adds `onChange(OccupancyChangeCB)`. All seven
exist because upstream's own class gives a sketch no way to learn about a
controller-driven change on that attribute. Naming (`onChange`, `EndPointCB`,
the `ChangeCB` typedefs) is provisional pending a decision on whether to
align it with a future upstream addition; see each class's header comment
for the exact members added. Nothing upstream is renamed.

`MatterGenericSwitch` carries a deviation of a different shape:
**`click()` returns `bool`, not upstream's `void`.** Upstream's `click()`
schedules a CHIP event locally and cannot fail in any way a caller could
observe; this port's `click()` is a real `AT+MTSWITCH` round trip that can
come back `+MTERR:2` (unknown endpoint) or `+MTERR:3` (no Switch cluster on
that endpoint), so silently discarding the result would hide a real failure
mode every other write-capable class already surfaces this way. The S3
review weighed this against `DE102`, the decision that keeps this project's
documented Hearth-side additions (the seven `onChange()` additions above)
rather than hiding them behind upstream-identical signatures, and upheld
the widening on the same reasoning: a signature difference, honestly
documented in the header and here, keeps the parity story truthful; a
silent `void` that swallowed a real error would not.

`MatterTemperatureControlledCabinet`, the twentieth and last class (Task
C5), has two mutually exclusive cluster shapes chosen by a composition
variant staged with `AT+MTEP=<id>[,<variant>]` and read back as the fourth
field of `AT+MTEP?`: `begin(tempSetpoint, minTemperature, maxTemperature,
step)` declares variant 0 (TemperatureNumber), whose four arguments become
ordinary `AT+MTATTR`-reachable attributes; `begin(supportedLevels,
levelCount, selectedLevel)` declares variant 1 (TemperatureLevel), whose
`SelectedTemperatureLevel` is likewise ordinary, but whose
`SupportedTemperatureLevels` is not an `AT+MTATTR` attribute at all: it is
served by a CHIP delegate, and the firmware does not persist its labels
across a reboot. `setSupportedTemperatureLevelLabels(const char *const
*labels, uint16_t count)` is a Hearth-only addition, not part of upstream's
surface (upstream's own `setSupportedTemperatureLevels()` carries only
numeric level identifiers, never label text): it sends real display text
through `AT+MTTEMPLEVELS`, automatically re-sent on every later
`Matter.begin()` reconcile alongside generated `"Level <n>"` defaults for
any level that has not been given a custom one. See the class header for
the exact grammar enforced host-side before any of it reaches the wire
(1..16 labels, 1..16 printable ASCII bytes each, never a double quote).

### Parked

Two narrower deferrals inside classes that are otherwise fully
implemented:

- **`MatterOccupancySensor::setHoldTime()` and `setHoldTimeLimits()` return
  `false`.** HoldTime and HoldTimeLimits are AttributeAccessInterface
  territory in the firmware, not attributes reachable over `AT+MTATTR`.
- **`MatterWindowCovering`'s absolute-position API returns `false`
  (`setLiftPosition`/`setTiltPosition`) or `0`** (the InstalledOpenLimit/
  InstalledClosedLimit pairs). esp-matter 1.5.1 carries no absolute-position
  attributes for the WindowCovering cluster; only the percent100ths lift/
  tilt attributes are live on the wire.

**Command-forwarding for app-adjudicated commands (the door-lock family)
is implemented, but as a Hearth original, not as part of this parity
table.** Every class above is either attribute-driven (`AT+MTATTR`) or, for
`MatterGenericSwitch`, a fire-and-forget event the cluster server resolves
on its own. A door lock's `LockDoor`/`UnlockDoor` commands are a different
shape: the cluster server cannot answer them autonomously, it needs a
synchronous verdict from the application, a round trip across the AT link
back to the host before the C6 can respond to the controller. No upstream
`arduino-esp32` class has this shape either, so `MatterDoorLock` is not a
twenty-first row here: it belongs in "Hearth originals" below, which is
where its class stays at 21 while this table stays at the 20/20 upstream
count.

## Hearth originals

Classes in this section have **no arduino-esp32 counterpart.** They are not
part of the parity table above (which stays at 20/20 upstream classes) and
are not "extra" parity: they are this library's own design against a wire
contract `arduino-esp32`'s Matter library has never needed, because it runs
its cluster server locally and can answer a command inline. Over the AT
link, some commands cannot be answered that way, and `MatterDoorLock` (Task
C3/C4) is the first one built.

### `MatterDoorLock`

Device type `0x000A` (door lock), cluster `0x0101` (`DoorLock`, 257
decimal). `LockState` reads over `AT+MTATTR` like any other attribute; what
is different is the two commands, `LockDoor` and `UnlockDoor`
(`AT_MT_SPEC.md` S3.17-S3.18).

**A controller's `LockDoor`/`UnlockDoor` is forwarded to the host for a
verdict, not answered by the C6 on its own.** The firmware raises
`+MTCMD:<seq>,<ep>,<cluster>,<command>`, and the callback registered with
`onLock(std::function<bool()>)` / `onUnlock(std::function<bool()>)` runs
synchronously from inside whichever library call dispatched the URC (the
same "your callback fires from inside a library call" shape every other
class's `onChange` already has; see "Driving the event loop" above). Its
return value is the verdict: `true` allows the command, `false` denies it.
**No callback registered denies by default.** A lock fails closed, never
open, on every path that is not an explicit allow.

**The verdict deadline is exactly 1000 ms, and it includes `Hearth.poll()`
latency, not just the callback's own running time.** The firmware starts
its clock the moment `+MTCMD` reaches the wire; the host only sees that URC,
and therefore only runs `onLock`/`onUnlock`, the next time something calls
into the library (a `Matter.*` call, an endpoint method, or `Hearth.poll()`
itself; see "Driving the event loop"). A sketch whose `loop()` blocks
(a long `delay()`, a synchronous sensor read, a busy-wait) for any real
fraction of that window can miss it entirely: the callback never runs in
time, the firmware's own deadline expires, and the lock denies by default,
exactly as if the callback had returned `false`. `+MTCMDTO:<seq>` arriving
at the host, mapped to `HEARTH_CMD_TIMEOUT` on `onLinkEvent()`, is the
diagnostic for exactly this: it means a verdict window closed with the
firmware never having heard back, whether because no callback was
registered, the callback was slow, or the sketch's own polling cadence lost
the race.

**A forward that arrives while the sketch already has a bridge command of
its own in flight cannot be answered inside the window, by construction.**
"In flight" means any AT-backed call: a query (`AT+MTNET?`,
`Matter.isDeviceCommissioned()`) or a setter (`setLockState()`, an
attribute write) on any endpoint, not just this one. The link is
single-reader (see "Driving the event loop" above): while one exchange is
still waiting on its own terminal `OK`/`ERROR`, `+MTCMD`'s callback still
runs (dispatch is not blocked), but the `AT+MTCMDRESP` reply it produces
can only be queued, never sent, until that exchange releases the link, and
`Hearth.poll()`/`hearthCommand()` are the only things that ever drain the
queue. A sketch whose hot loop issues its own bridge traffic on every pass
narrows the effective window every single time, and a slow enough one
(or one already close to the 1000 ms edge) loses the race the same way a
blocking `loop()` does above: the firmware's own deadline expires first,
it default-denies, and `+MTCMDTO:<seq>` arrives having genuinely never had
a chance. Keep per-loop status polling such as
`Matter.isDeviceCommissioned()` off the hot loop in any sketch where a
lock's (or other forwarded command's) verdict actually matters; the
`MatterDoorLockAdjudicated` example below does not poll anything of the
kind.

**Verdict replies do not go out from inside the callback.** `AT+MTCMDRESP`
is sent afterward, from a deferred queue drained once the current AT
exchange has released the link, so a sketch author should not expect the
reply to reach the wire synchronously inside `onLock`/`onUnlock`; the
verdict is captured there, not transmitted there.

**Reporting that the bolt actually moved is a separate step, on a separate
command.** The firmware never calls `AT+MTLOCK`'s effect on its own, even
after an allowed verdict (spec F4): allowing `LockDoor` only tells the
controller its request was accepted, it does not move anything, because
actuation timing belongs to whatever mechanism the sketch is driving, not
to the verdict. Call `setLockState(state, source = kSourceManual)` (or the
`lock()`/`unlock()` shorthands) once that mechanism confirms the state
change, most often right after acting on an allowed verdict from a *local*
source such as `kSourceManual` (a physical button, not a Matter command)
rather than in response to the forwarded command itself. The cache updates
only on a successful write, matching every other class's failed-write
discipline. `getLockState()` reads the cache; no wire round trip.

**Feature-map 0 in this round: no PIN/user/credential surface.** A
controller may only send bare `LockDoor`/`UnlockDoor`; it cannot send a
PIN-carrying variant, and the device's own CHIP stack refuses one before it
ever reaches this library or the sketch, since the cluster's PIN/USER/COTA
features are not enabled on the wire (`AT_MT_SPEC.md`'s device-type table,
door lock: "Feature map `0`"). A host-side keypad (a Matter PIN, an RFID
badge, a numeric pad wired to the host) is therefore entirely a sketch-side
concern: read it, decide, and return the verdict from `onLock`/`onUnlock`
with whichever `OperationSource_t` value describes what actually happened
recorded through `setLockState()`'s `source` argument afterward. This
library carries no PIN storage or matching of its own.

See `examples/MatterDoorLockAdjudicated/` for the full shape: a
sketch-side policy consulted from `onLock`/`onUnlock`, `Hearth.poll()` in
`loop()`, and `setLockState()` called on actuation with `kSourceManual`.

### The ten-type swoop

Ten more classes, none with an `arduino-esp32` counterpart (Tasks C2-C4),
join `MatterDoorLock` in this section rather than the parity table above,
which stays at 20/20. Three are Hearth-original sensors (C2), five are
actuator clones of an existing parity class onto a new device type (C3), and
two carry real surface design of their own (C4):

- `MatterLightSensor` (`0x0106`): `begin(rawValue)` / `setRawMeasuredValue()`
  / `getRawMeasuredValue()` push a raw `uint16` `IlluminanceMeasurement`
  reading; the sketch is the light source, nothing arrives back down.
- `MatterFlowSensor` (`0x0306`): the identical raw-`uint16` push shape as
  `MatterLightSensor`, over `FlowMeasurement` instead.
- `MatterAirQualitySensor` (`0x002C`): `setAirQuality(AirQuality_t)` /
  `getAirQuality()` push an enum8 `AirQuality` reading (`kUnknown` through
  `kExtremelyPoor`). **None of these three sensor classes adds `onChange`:**
  their one attribute is `kView`-only per the C2 adjudication, so a
  controller can never write it and no genuine controller-driven URC for it
  can ever arrive; an `onChange` callback would have nothing honest to fire
  on.
- `MatterMountedOnOffControl` (`0x010F`): `MatterOnOffPlugin`'s surface
  (`setOnOff`/`getOnOff`/`toggle`/`onChange`/`onChangeOnOff`) cloned onto the
  mounted-on-off-control device type.
- `MatterMountedDimmableLoadControl` (`0x0110`): `MatterDimmablePlugin`'s
  shape cloned onto the mounted-dimmable-load-control device type, using
  `MatterDimmableLight`'s `setBrightness`/`getBrightness` naming rather than
  `MatterDimmablePlugin`'s own `setLevel`/`getLevel`.
- `MatterAirPurifier` (`0x002D`): `MatterFan`'s surface (fan mode enum plus
  speed percent) cloned onto the air-purifier device type.
- `MatterExtractorHood` (`0x007A`): the identical `MatterFan` clone as
  `MatterAirPurifier`, on the extractor-hood device type instead.
- `MatterCooktop` (`0x0078`): **`OffOnly`**, the odd one out. The public
  surface is deliberately just `begin()`/`off()`/`getOnOff()`: there is no
  `on()`, no `toggle()`, and no write path of any kind that can turn the
  attribute true, because a remotely-started physical heating element is the
  failure mode this device class exists to prevent; a local turn-on still
  reaches `getOnOff()` through a controller-side URC, since only the
  *write* direction is closed off, not the read one.
- `MatterRoomAirConditioner` (`0x0072`): an `OnOff` leg (`MatterOnOffPlugin`'s
  shape) plus `MatterThermostat`'s setpoint/mode surface
  (`setCoolingSetpoint`/`setHeatingSetpoint`/`setMode` over the same
  `Thermostat` cluster IDs). **Dead-front is documentation, not code:**
  `setOnOff(false)` sends an ordinary `OnOff` write and nothing else; the
  device type's spec-mandated dead-front behaviour (turning the unit off
  disables its thermostat function) happens entirely in the C6 firmware's
  data model, and this class neither models nor enforces it, so
  `setCoolingSetpoint()`/`setHeatingSetpoint()`/`setMode()` remain ordinary
  writes whether the device is on or off. `onChangeCoolingSetpoint()`,
  `onChangeHeatingSetpoint()` and `onChangeMode()` (a C5 scope addition, the
  ten-type swoop's own review) let a sketch learn of a controller-driven
  change to any of the three without polling the getters.
- `MatterPump` (`0x0303`): an `OnOff` leg plus `PumpConfigurationAndControl`:
  `setOperationMode()`/`getOperationMode()` (controller-writable, with
  `onChangeOperationMode()`, a C5 scope addition, firing on a
  controller-driven change), `setMaxPressure()`/`setMaxSpeed()`/
  `setMaxFlow()` (sketch-published telemetry, Read-only on the wire, no
  getter), and `getEffectiveOperationMode()`/`getEffectiveControlMode()`
  (device-answered reads fed exclusively by URCs, no setter).

### The seven-type batch

Eight more classes, none with an `arduino-esp32` counterpart (Tasks C7-C8),
join `MatterDoorLock` and the ten-type swoop in this section rather than
the parity table above, which stays at 20/20. The firmware's own device
type table (`AT_MT_SPEC.md` §3.9, "Supported device types") listed 38 rows
at this point; this batch supplied the last eight of
them at the time, taking this section's own class count to nineteen
(`MatterDoorLock`, the ten-type swoop's ten, and these eight).

Three add no shared implementation (C7):

- `MatterWaterValve` (`0x0042`): `onOpen()`/`onClose()` register a verdict
  for a forwarded `Open`/`Close` invoke (`ValveConfigurationAndControl`,
  cluster `0x0081`); `setValveState(state[, level])` reports `CurrentState`
  over `AT+MTVALVE`, cache-only `getValveState()`. **The verdict cannot
  fail the command on the wire.** Unlike the door lock,
  `ValveConfigurationAndControl`'s own server calls the delegate
  synchronously and discards what it returns (`TEMPORARY_RETURN_IGNORED`,
  both call sites), so the controller always sees `Status::Success` once
  the command reaches the host at all; a deny gates only whether the
  sketch's own callback goes on to move the physical valve, never what the
  controller observes. `<level>` (0..100) is accepted but never cached or
  readable back: this SDK revision's `water_valve` thunk fixes
  `FeatureMap` at 0, so `CurrentLevel`/`TargetLevel` are never created as
  attributes at all.
- `MatterModeSelect` (`0x0027`): `setSupportedModes(modes, labels, count)`
  replaces the `SupportedModes` list over `AT+MTMODES` (1..8 mode/label
  pairs, each mode unique, each label 1..32 printable ASCII bytes with no
  `"`), served by CHIP's own `SupportedModesManager` rather than
  `esp_matter`'s attribute store, so there is no `AT+MTATTR` path to it and
  no read-back command either; not persisted, so the cached list is resent
  on every later `Matter.begin()` reconcile, the
  `MatterTemperatureControlledCabinet` norm. `CurrentMode` is the
  opposite: a plain `esp_matter`-managed attribute, `setCurrentMode()`/
  `getCurrentMode()` going through the base class the same way
  `MatterOnOffLight::setOnOff()` does, with `onChangeMode()` firing on a
  controller's `ChangeToMode`.
- `MatterChime` (`0x0146`): `setInstalledChimeSounds(ids, names, count)`
  is the `AT+MTCHIMESOUNDS`-only counterpart of `MatterModeSelect`'s
  `setSupportedModes()`, identical grammar, not persisted, resent per
  reconcile. `setSelectedChime()`/`getSelectedChime()` and
  `setEnabled()`/`getEnabled()` are cache-only in both directions over
  `AT+MTCHIME` (no `AT+MTATTR` path exists for any of this cluster's three
  attributes), but persist firmware-side across `AT+MTRESET`, so neither
  needs a reconcile push. `onPlayChime(std::function<bool(uint8_t
  chimeID)>)` registers the verdict for a forwarded `PlayChimeSound`
  invoke, and unlike the water valve, **this one is a real wire verdict**:
  the SDK passes the host's allow/deny straight through as
  `Status::Success`/`Status::Failure`, with no remapping. It is also the
  first consumer of `AT_MT_SPEC.md` S3.17's reserved fifth `+MTCMD`
  payload field, the requested `chimeID`. The SDK short-circuits
  `PlayChimeSound` before it ever reaches the host in two cases (`Enabled`
  false, or an uninstalled `chimeID`): the callback simply never fires,
  there is nothing to deny.

Two more add no shared implementation either (C8):

- `MatterSmokeCOAlarm` (`0x0076`): eleven `AT+MTALARM` fields, ten cached
  setter/getter pairs (`setSmokeState`, `setCOState`, `setBatteryAlert`,
  `setDeviceMuted`, `setHardwareFaultAlert`, `setEndOfServiceAlert`,
  `setInterconnectSmokeAlarm`, `setInterconnectCOAlarm`,
  `setContaminationState`, `setSmokeSensitivityLevel`) plus
  `completeSelfTest()` (field 5, `TestInProgress`, value 0 only).
  **`onSelfTest(std::function<void()>)` is this library's first notify-only
  `+MTCMD` consumer.** `SmokeCoAlarmServer::HandleRemoteSelfTestRequest`
  answers the controller itself before the app-level hook ever runs, so a
  controller-invoked self test always arrives as `+MTCMD:0,<ep>,92,0`
  (`AT_MT_SPEC.md` S3.17's notify-only form, seq `0` reserved): the
  callback runs, but there is no verdict to send back and this library's
  dispatcher never tries to send one. `getExpressedState()` is the one
  genuine `AT+MTATTR` read in this class (`ExpressedState` is derived
  server-side from the ten states above, so there is no cached value to
  return instead), and this library's first use of that live-read path
  anywhere.
- `MatterPowerSource` (`0x0011`): a flat sibling endpoint, not composed
  onto another one, enabling the `Battery` feature only.
  `setBatChargeLevel()`/`setBatPercentRemaining(double percent)`/
  `setBatReplacementNeeded()` are ordinary `AT+MTATTR` writes, the
  `MatterAirQualitySensor::setAirQuality()` shape for a host-authoritative
  reading pushed to the fabric; no getters, matching the task brief's own
  API for this class. `BatPercentRemaining` is the Matter spec's own
  half-percent-step type (0-200 for 0-100%); the `double` argument is
  clamped to 0..100 (an out-of-range `double` cast to `uint8_t` is
  undefined behaviour in C++, not a wire-validation gap) and doubled
  before the write.

The last three (C8) share one implementation, `MatterOperationalStateEndpoint`,
an internal base class (not itself a public device type: it has no device
type ID of its own) behind three thin public subclasses,
`MatterLaundryWasher` (`0x0073`), `MatterDishwasher` (`0x0075`) and
`MatterLaundryDryer` (`0x007C`), all three wiring the identical
`OperationalState` cluster (`0x0060`) with no device-specific extension
(`AT_MT_SPEC.md` S3.21). Each subclass's `begin()` differs only in the
device type ID it declares. `onPause()`/`onResume()`/`onStart()`/
`onStop()` register the verdict for a forwarded `Pause`/`Resume`/`Start`/
`Stop` invoke; `setOperationalState()`/`getOperationalState()` report the
appliance's actual state over `AT+MTOPSTATE` (state `0`/`1`/`2`; state
`3`, `Error`, is reserved for the device's own fault-detection path and
rejected `+MTERR:1`), cache-only getter, no `AT+MTATTR` path exists for
any `OperationalState` attribute. **A deny IS the wire response here**,
unlike the water valve: the SDK copies the adjudication verdict straight
into the command's own `OperationalCommandResponse`, so `onPause()`/
`onResume()`/`onStart()`/`onStop()`'s return value is a real allow/deny
the controller observes, the same shape as the door lock and the chime.

### The RVC + Microwave batch

Two more classes, neither with an `arduino-esp32` counterpart (Tasks 7-8),
join the sections above rather than the parity table, which stays at 20/20.
The firmware's own device type table (`AT_MT_SPEC.md` §3.9, "Supported
device types") listed 40 rows at this point; this batch supplied the last
two of them, taking this section's own class count
to twenty-one (`MatterDoorLock`, the ten-type swoop's ten, the seven-type
batch's eight, and these two) and the library's total public `Matter*`
endpoint class count to forty-one: twenty parity classes plus twenty-one
Hearth originals.

- `MatterRoboticVacuum` (`0x0074`): one endpoint carrying three clusters at
  once, `RvcRunMode` (`84`), `RvcCleanMode` (`85`) and `RvcOperationalState`
  (`97`). `setSupportedRunModes()`/`setSupportedCleanModes()` each replace
  one cluster's `SupportedModes` list over the cluster-aware `AT+MTMODES`
  form (`AT_MT_SPEC.md` S3.20.1: mode/tag/label triples, an addition over
  `MatterModeSelect`'s own mode/label pairs), independent stores on the same
  endpoint, both re-sent on every reconcile since the firmware does not
  persist either. `onChangeRunMode()`/`onChangeCleanMode()` register the
  verdict for a forwarded `ChangeToMode`; `onPause()`/`onResume()`/
  `onGoHome()` do the same for `RvcOperationalState`'s three supported
  commands (`Start`/`Stop` are not supported on this derived cluster at
  all). **`CurrentMode`/`OperationalState` have no ember-level signal of any
  kind on these three clusters**: `AttributeAccessInterface` intercepts both
  the read and the change-notification path the same way it does for
  `MatterModeSelect`'s stale-`AT+MTATTR` finding, so
  `getCurrentRunMode()`/`getCurrentCleanMode()`/`getOperationalState()` are
  this host's own bookkeeping, updated only when the matching `on*()`
  callback itself allows the forward; no `+MTATTR` URC ever fires for any of
  them, from the firmware's own clamp or a controller-driven change. A deny
  IS the wire response for `Pause`/`Resume`/`GoHome` (the `OperationalState`
  trio's own `GenericOperationalError` shape), `RvcState_t` extends
  `OperationalState_t`'s three shared values with three derived-cluster-only
  ones (`kStateSeekingCharger` `0x40`, `kStateCharging` `0x41`,
  `kStateDocked` `0x42`).
- `MatterMicrowaveOven` (`0x0079`): another single endpoint carrying three
  clusters, `MicrowaveOvenMode` (`94`), `MicrowaveOvenControl` (`95`), and
  the plain `OperationalState` (`96`, not the RVC's derived cluster) this
  class inherits unchanged from `MatterOperationalStateEndpoint` rather than
  reimplementing it: `onPause()`/`onResume()`/`onStart()`/`onStop()` and
  `setOperationalState()`/`getOperationalState()` are the identical trio
  members, subclassed the same way `MatterLaundryWasher` and its siblings
  are. **`MicrowaveOvenMode` has no `ChangeToMode` command at all** (its own
  generated `CommandIds.h` declares zero accepted commands): mode selection
  rides `SetCookingParameters`' `cookMode` field instead, so this class
  registers no mode-change handler for that cluster, only
  `setSupportedModes()` to publish the list itself, same cluster-aware
  `AT+MTMODES` grammar as the RVC's two lists, narrowed to the one this
  class has. `onCookingParameters(std::function<bool(const
  HearthCookingParams &)>)` registers the verdict for a forwarded
  `SetCookingParameters`; the callback's argument carries all four fields
  (`cookMode`, `cookTimeSec`, `powerPercent`, `startAfterSetting`) with
  honest `has*` flags read from the wire's own present/absent tail
  positions, not assumed always-present, even though this firmware's server
  resolves every optional before the callback ever runs in practice.
  `onAddMoreTime(std::function<bool(uint32_t finalCookTimeSec)>)` registers
  the verdict for a forwarded `AddMoreTime`; its argument is the
  server-computed ABSOLUTE new cook time, not a delta to add to anything.
  Both use the chime's `PlayChimeSound` verdict shape (`Status::Success`/
  `Status::Failure` passed straight through), not the `OperationalState`
  family's `GenericOperationalError` indirection its own inherited
  `onPause()`/`onResume()`/`onStart()`/`onStop()` still use. **No getters
  for `CookTime`/`PowerSetting`**: both are Instance/delegate-owned,
  command-driven state that never reaches `esp_matter::attribute::set_val()`,
  so no `+MTATTR` URC ever fires for either and `AT+MTATTR` cannot serve
  them, the identical finding the RVC's own `CurrentMode` note above
  documents for a different pair of attributes. A sketch reads `CookTime`/
  `PowerSetting` back only through a commissioned controller.

### The composed appliance round

Three composing appliances (library 0.7.0, firmware 0.7.0): a parent
endpoint that OWNS child endpoints, declared with the parent's own
registry index riding the `AT+MTEP` grammar's third field
(`AT+MTEP=<id>,<variant>,<parent_idx>`), so the firmware composes the
children into the parent's Descriptor `PartsList` and derives their
conditional cluster sets from the pairing. This adds four public classes
(`MatterRefrigerator`, `MatterOven`, `MatterOvenCavity`,
`MatterCookSurface`) and composes the existing `MatterCooktop`, taking the
library's total public `Matter*` endpoint class count to forty-five. The
registry capacity (`HEARTH_MAX_ENDPOINTS`) rises 16 to 24 alongside the
firmware's own cap, since one composed appliance can now occupy up to five
endpoints.

All three owners share one pattern. `add*()` (pre-begin only) hands back a
reference to a child the appliance owns; the owner's `begin()` declares
the parent first, then every added child with the flavour as the variant
byte and `parent_idx` pointing at itself; the owned child's own `begin()`
declares NOTHING (the parent's declaration is authoritative, parent index
and all) and only validates its flavour and caches its temperature
configuration for the reconcile push. Past capacity, or after the owner's
`begin()`, `add*()` returns an inert reject child whose every call fails,
so the mistake surfaces at that child's `begin()` instead of as a silent
extra endpoint. A changed parent triggers a composition rebuild exactly
the way a changed variant does: it is part of what makes two compositions
identical.

- `MatterRefrigerator` (`0x0070`): `addCabinet(flavour)` hands back up to
  four plain `MatterTemperatureControlledCabinet` children (`NUMBER` =
  TemperatureNumber, `LEVELS` = TemperatureLevel, the `0x0071` variant
  byte). The parent carries `RefrigeratorAndTemperatureControlledCabinetMode`
  (`0x52`) and `RefrigeratorAlarm`: `setSupportedModes()` (cluster-aware
  `AT+MTMODES` triples), `onChangeMode()`/`getCurrentMode()` (the 0.6.0
  Instance-served rule), and `setDoorOpenAlarm()`/`setAlarmState()` over
  the cluster-aware `AT+MTALARM`, which is what makes the cluster's
  `Notify` event actually fire. A refrigerator-owned cabinet additionally
  gains the same three mode members on its own `0x52` cluster, refused
  without wire traffic on a standalone cabinet (the cluster only exists
  when the firmware derives it from the parent).
- `MatterOven` (`0x007B`) with `MatterOvenCavity`, the first TYPED owned
  child: the oven parent is bare by design (Descriptor plus Identify), and
  `addCavity(flavour)` hands back a cavity whose compile-time surface is
  exactly the legal cluster set the composition derives: the whole
  inherited cabinet temperature API, `OvenMode` (`0x49`,
  `setSupportedModes()` with tag `0` = kBake, `onChangeMode()`/
  `getCurrentMode()`) and `OvenCavityOperationalState` (`0x48`):
  `onStop()`/`onStart()` verdicts and `setOperationalState()` with plain
  `{0,1,2}` enforced host-side. There are deliberately NO
  `onPause`/`onResume` members: the cluster marks both commands
  disallowConform, the firmware never forwards them, and the typed
  reference means a sketch that tries does not compile.
- `MatterCooktop` (`0x0078`), composed, with `MatterCookSurface`
  (`0x0077`), the second typed owned child and the first device type the
  firmware only accepts WITH a parent: `addSurface(flavour)` hands back a
  cabinet-shaped surface (same temperature machinery, own endpoint) whose
  `OnOff` cluster carries the OffOnly feature. A controller can switch a
  surface off but never on (`Off` is the entire accepted command list), so
  the remote side arrives as a plain `+MTATTR` URC that fires
  `onOffChange(std::function<void(bool)>)`, per OffOnly always with
  `false`, and turning a burner ON is always the sketch's own act:
  `setOnOff(bool)`/`getOnOff()`, both directions, an ordinary reported
  `AT+MTATTR` write. That is the deliberate asymmetry against the parent
  class, which keeps its structural no-path-to-true guarantee: a
  zero-surface `MatterCooktop` stays byte-identical to 0.6.0, wire and
  API both.

### The energy round A

Two measurement-push classes (library 0.8.0, firmware 0.8.0), neither with
an `arduino-esp32` counterpart, taking the library's total public `Matter*`
endpoint class count to forty-seven. Both push electrical readings up to
the fabric over `AT+MTMEAS` (`AT_MT_SPEC.md` §3.25), riding the 64-bit value pipeline this round added: every value is
full-width `int64_t`/`uint64_t` end to end, host cache to wire grammar.

- `MatterElectricalSensor` (`0x0510`): `ElectricalPowerMeasurement`
  (voltage mV, active current mA, active power mW, frequency mHz) plus, at
  the `FULL` variant, `ElectricalEnergyMeasurement` (cumulative
  imported/exported counters, mWh). `POWER_ONLY` (variant 1) builds power
  measurement only, the current-clamp case, and is fully conformant on the
  sensor. Individual setters push one field per wire line;
  `pushMeasurements(mv, ma, mw)` batches the three power readings into ONE
  line, the intended per-sample path. `addEnergyImported()`/
  `addEnergyExported()` accumulate host-side and push the new cumulative
  total; the firmware timestamps the measurement period and emits a
  `CumulativeEnergyMeasured` event per push. On `POWER_ONLY` the energy
  adders are refused host-side (error 1, zero wire traffic).
- `MatterElectricalMeter` (`0x0514`): a thin subclass inheriting the
  sensor's entire surface; on the wire the two differ only in device type
  id and the sensor's `PowerTopology` cluster, which has no host surface.
  One conformance note: the meter's device type XML marks BOTH measurement
  clusters mandatory, so a `POWER_ONLY` meter is
  permissive-beyond-conformance (accepted for variant scheme symmetry);
  the strictly conformant power-only declaration is the `POWER_ONLY`
  sensor.

Every attribute both classes serve is Instance-served on the C6 (the
0.6.0 rule at full strength): no `AT+MTATTR` path, no `+MTATTR` URC, ever.
Getters read the host-side last-pushed cache, which updates only on a
successful push; the fabric-side fields are null until first pushed, and
measurements are deliberately NOT re-pushed on reconcile, since a stale
sample re-reported as fresh would be a lie (the class headers carry the
full reasoning).

### The energy round B

Two more energy classes (library 0.9.0, firmware 0.9.0), neither with an
`arduino-esp32` counterpart, taking the total to forty-nine. Both are
direct `MatterEndPoint` children embedding the round's extracted
`HearthMeasurementPush` helper (the electrical push surface as a shared
member, byte-identical semantics, pinned by the electrical suite passing
untouched across the extraction):

- `MatterWaterHeater` (`0x050F`): four surfaces on one endpoint.
  `WaterHeaterManagement` state rides `AT+MTMEAS`'s `0x94` field table
  (HeaterTypes, HeatDemand, BoostState, and at the `FULL` variant
  TankVolume, EstimatedHeatRequired int64 mWh, TankPercentage); `Boost` and
  `CancelBoost` arrive as adjudicated `+MTCMD` forwards, the wire's first
  five-field tail, whose packed presence mask the library unpacks into
  `BoostInfo` (the sketch never touches the encoding). The verdict answers
  the controller, and on an accept the library itself pushes BoostState
  right after the verdict, which is what makes the firmware derive the
  `BoostStarted`/`BoostEnded` events; `endBoost()` is the sketch's own
  timer path. `WaterHeaterMode` carries the ModeBase surface
  (`setSupportedModes`, tag 0 = `kManual`, plus the cluster-qualified
  `onChangeWaterHeaterMode`/`getCurrentWaterHeaterMode`), and the
  Thermostat cluster (heating-only) carries `MatterThermostat`'s attribute
  helpers, ember-served with `+MTATTR`-driven callbacks. The thermostat
  cache seeds from the C6's own cluster defaults (heating setpoint 2000,
  SystemMode Auto), which is what makes an unchanged-value first write a
  true no-op instead of a swallowed one. `MINIMAL` (variant 1) is the
  disclosed sub-conformant SDK-bare build: the tank trio and the whole
  measurement surface refuse host-side (error 1, zero wire traffic).
- `MatterHeatPump` (`0x0309`): the shared measurement surface plus
  identity, nothing else, and that is the documented point: PowerSource
  (wired) exists at composition with no host surface, and the composed
  Thermostat device type the XML mandates is a disclosed gap matching the
  SDK's own build (the class header carries the note, the variant-1
  meter's precedent). ActivePower is signed, full-width int64: a heat
  pump moving energy out reports negative milliwatts.

The reconcile split is test-pinned in both directions: HeaterTypes,
TankVolume and the mode list are configuration and re-pushed on every
reconcile; HeatDemand, BoostState, TankPercentage, EstimatedHeatRequired
and every electrical field follow the B229 volatile rule (wire-pushed
memory cleared, values not re-sent).

### The energy round C1

Three more energy classes (library 0.10.0, firmware 0.10.0, though
the version table above pairs library 0.10.0 with firmware 0.10.1), none with an `arduino-esp32`
counterpart, taking the total to fifty-two. The round adds
one shared helper, `HearthDemControl`, the DeviceEnergyManagement (`0x0098`,
152) surface: the state pushes on `AT+MTMEAS`'s `0x0098` field table, the
`AT+MTDEMCAP` capability replacement, and the two adjudicated power-adjust
command forwards. Two of the three classes embed it.

- `MatterSolarPower` (`0x0017`): the shared measurement surface plus
  identity, nothing else, the heat pump's shape with a variant byte.
  `ActivePower` is signed and that is the point: an array that is
  generating reports negative milliwatts and books its energy on the
  exported counter. `NO_ENERGY` (variant 1) is the current-clamp shape,
  disclosed sub-conformant: the composed sensor is built without its
  energy cluster, so the two energy adders refuse host-side (error 1, zero
  wire traffic) while every power-side setter keeps working.
- `MatterBatteryStorage` (`0x0018`): three surfaces on one endpoint. The
  measurement surface (on both variants: the firmware grafts the sensor
  with energy measurement unconditionally here, unlike solar); seven
  ember-served PowerSource battery attributes over the ordinary
  `AT+MTATTR` path (`setBatVoltage`, `setBatPercentRemaining` in raw
  half-percent steps, `setBatTimeRemaining`, `setBatChargeState`,
  `setBatChargingCurrent`, `setBatCapacity`, `setBatTimeToFullCharge`;
  the two fault lists are lists, which `AT+MTATTR` cannot carry, and ship
  empty and host-untouched by design); and the whole DEM surface on the
  `FULL` variant. `NO_DEM` (variant 1) omits the DEM triple and stays
  conformant, refusing every DEM call host-side with error 1 and zero wire
  traffic.
- `MatterDeviceEnergyManagement` (`0x050D`): the DEM helper plus identity.
  `CONTROLLABLE` (variant 0) is a ControllableESA with the PowerAdjustment
  feature; `REPORT_ONLY` (variant 1) is an equally conformant ESA that
  reports and cannot be told what to do. The two variants differ in exactly
  one host-visible way, and it is a deliberate one: `REPORT_ONLY` refuses
  `setPowerAdjustmentCapability()` host-side (error 1, zero wire traffic,
  because that variant serves no such attribute) while every state and
  identity push still reaches the wire. That is a narrower thing than
  battery storage's `NO_DEM`, where the cluster is absent altogether and
  the whole surface refuses; the two class headers carry the comparison.

The power-adjust protocol's division of labour is worth stating once:
`onPowerAdjust()`/`onCancelPowerAdjust()` return a verdict and nothing
else. On an accept the FIRMWARE moves `ESAState` to PowerAdjustActive and
emits `PowerAdjustStart` itself, so the callback pushes nothing (and must
not: a wire write from inside the `+MTCMD` dispatch is refused
`HEARTH_CMD_REENTRANT`). The sketch reports its energy figure with
`pushAdjustmentEnergyUse()` and ends the adjustment with
`endAdjustment()`, both from ordinary `loop()` context; the `ESAState`
push back to Online is what makes the firmware emit `PowerAdjustEnd` with
that figure. The FullAPI DEM sketch's load simulator is built exactly this
way: the callback records the request, `loop()` acts on it.

The reconcile split is test-pinned per surface, and the same
configuration-versus-sampled question is answered three times. The DEM
configuration (ESAType, ESACanGenerate, AbsMin/MaxPower and the capability
list) is re-pushed on every reconcile; the DEM volatile fields (ESAState,
OptOutState) and every electrical field follow the B229 rule (wire-pushed
memory cleared, values not re-sent); and battery storage's ember
attributes are split the same way, per attribute: `BatCapacity` (a
nameplate written once at `setup()`) and `BatChargeState` (asserted on
transitions, not sampled) are re-pushed, while the five sampled readings
are cleared and not re-sent, so the next sample repairs the fabric even if
it is byte-identical to the last one.

There is no house-wide rule for ember attributes on reconcile, and this
README used to imply there was: **this library has both behaviours on
purpose.** `MatterThermostat` and `MatterPowerSource` never re-push, which
is right for a value a sketch resamples;
`MatterTemperatureControlledCabinet` re-pushes its four TemperatureControl
attributes unconditionally, and its own comment records the bench run that
forced it (the alternative left the device at esp-matter's defaults
forever, because the setters' skip-if-equal suppressed every later write
of a value the host believed it had already set). A value a sketch writes
once cannot self-heal, so it has to be re-pushed; a value it resamples
must not be, because a stale reading re-reported as fresh is a lie. The
battery class header lists all seven attributes with the reason each lands
where it does.

## Examples

[The examples map](#the-examples-map) near the top of this README is the
short version. This section is the long one.

`examples/` holds four tiers of sketches, each proving a different thing:

- **The entry point**, `HearthFirstLight`: one on/off light, Hearth-original,
  written to be read rather than to prove anything. It is the sketch
  [Start here](#start-here) walks a first-time user through, and the only
  one whose comments explain what `Matter.begin()` and commissioning are.
- **Parity proofs**, at the examples root (documented in the list right
  below): byte-identical copies of `arduino-esp32`'s own Matter example
  sketches. They are the evidence that an unmodified upstream sketch
  compiles and runs on a Challenger; editing one to make it build would
  erase the proof, so this tier stays untouched.
- **FullAPI references**, one per class under `examples/FullAPI/`
  (documented below, in its own subsection): a sketch that exercises the
  complete public surface of exactly one endpoint class, fifty-four
  folders in all. Each opens with a banner comment listing every public member and
  where the sketch exercises it; the banner is a coverage checklist against
  the class header, not narrative.
- **Scenario showcases**: `MatterDoorLockAdjudicated` and
  `HearthSensorsAndAppliances` (sketch descriptions further down in this
  section; the classes they drive are documented under "Hearth originals"),
  Hearth-original sketches that compose several classes into something
  closer to a real device, rather than exhaustively covering one class.

`examples/` holds nineteen of `arduino-esp32`'s own Matter example sketches,
copied **byte-identical** from
`libraries/Matter/examples/` in the `esp32` Arduino core (3.3.8), and
verified with `cmp` against that source. Byte-identity is the point: these
sketches are the actual evidence that an unmodified sketch is in scope, and
editing even one line to make it compile would remove that evidence. See
`iLabs_AT_Hearth`'s Task 9 report for the original three, its Task 6 report
(devtype expansion) for the thirteen added alongside this table, its
Task S4 report for the next two, and its Task C5 report for the last.

- `MatterOnOffLight`
- `MatterDimmableLight`
- `MatterTemperatureSensor`
- `MatterOnOffPlugin`
- `MatterDimmablePlugin`
- `MatterContactSensor`
- `MatterRainSensor`
- `MatterWaterFreezeDetector`
- `MatterWaterLeakDetector`
- `MatterHumiditySensor`
- `MatterPressureSensor`
- `MatterOccupancySensor` (upstream's basic example; the `HoldTime` variant
  is out of scope along with `setHoldTime()`/`setHoldTimeLimits()`
  themselves, see "Parked" above)
- `MatterFan`
- `MatterWindowCovering`
- `MatterThermostat`
- `MatterEnhancedColorLight`
- `MatterGenericSwitch` (upstream ships it as `MatterSmartButton`; renamed
  here to match the class name)
- `MatterColorLight`
- `MatterTemperatureControlledCabinet` (upstream ships both a
  TemperatureNumber and a `MatterTemperatureControlledCabinetLevels`
  TemperatureLevel example; this copy is the TemperatureNumber one, matching
  the class's default variant)

`examples/MatterDoorLockAdjudicated/` is one of the three sketches at the
examples root that are not part of the count above: **it is
Hearth-original, not copied from upstream.**
`MatterDoorLock` has no `arduino-esp32` counterpart, so there is no example
to copy byte-identical; the sketch header says so. See "Hearth originals"
above for the class it demonstrates.

`examples/HearthSensorsAndAppliances/` is the second Hearth-original at the
root (`HearthFirstLight` is the third): it composes a representative subset of the
ten-type swoop (`MatterAirQualitySensor`, `MatterPump`,
`MatterRoomAirConditioner`), `Hearth.poll()`-driven and CDC (`Serial`)
interactive like `MatterDoorLockAdjudicated`, with no upstream counterpart
to copy from. See "The ten-type swoop" above for the classes it
demonstrates.

**They call `WiFi.begin()`, and on this platform that call can never
succeed.** It is not merely redundant: the sketch would sit in
`while (WiFi.status() != WL_CONNECTED)` forever and never reach
`Matter.begin()`.

The RP2350 has no radio of its own. Its `WiFi` library drives the C6, over
either esp-at (UART) or esp-hosted (SPI), whichever the board's "ESP WiFi
type" menu selects. The C6 is running Hearth instead of either of them. One
co-processor, one personality: the host's WiFi and the Matter stack want the
same chip, so the host cannot have it.

Nor does it need it. **The library sets `CONFIG_ENABLE_CHIPOBLE` to 1**
(`HearthCompat.h`), which is upstream's own switch for "this device is
commissioned over BLE, so the sketch must not connect WiFi itself". Every
one of these examples already guards its WiFi bring-up with
`#if !CONFIG_ENABLE_CHIPOBLE`; undefined evaluates to 0, so leaving it unset
compiled the block in. Setting it is not a workaround but an accurate
description of the firmware on the other end of the link: Hearth is built
with `CONFIG_ENABLE_CHIPOBLE=y` and `CONFIG_ENABLE_WIFI_STATION=y`, so the
C6 advertises over BLE, receives the WiFi credentials from the commissioner,
and joins the network on its own.

So the sketches keep their byte-identity and the WiFi block simply compiles
out, taking 43 KB of unusable WiFi stack with it. A sketch that genuinely
wants host-side WiFi (with the C6 reflashed to esp-at or esp-hosted, and
therefore no Matter) can `-DCONFIG_ENABLE_CHIPOBLE=0`.

**Compiling them against `arduino-pico` found, and then closed, a real
parity gap in this library.** `arduino-cli` compiles against a Challenger
RP2350 board first surfaced three blockers, none hidden by patching a
sketch:

1. `MatterOnOffLight` and `MatterDimmableLight` `#include <Preferences.h>`,
   a header the `esp32` core ships and `arduino-pico` does not. A gap in
   the RP2040/RP2350 core, not in this library.
2. `MatterTemperatureSensor` references the bare macro `BOOT_PIN`, which no
   `arduino-pico` board variant defines (`esp32` boards define it; RP2040/
   RP2350 boards have no standard equivalent). Also a core gap.
3. All three failed earlier still, on a gap that **was** this library's:
   `Matter.h` (via `Hearth.h`) did not `#include` the endpoint class
   headers (`MatterEndPoint.h` and the four headers under
   `MatterEndpoints/`) the way upstream's `Matter.h` aggregates all twenty
   of its own. A sketch that does nothing but `#include <Matter.h>` and
   then declares `MatterOnOffLight light;` at file scope, exactly as every
   upstream example does, failed with `'MatterOnOffLight' does not name a
   type`.

Item 3 was a real parity gap found by this compile attempt, in the same
vein as two gaps found earlier in this project by the same method (a union
member set that was too narrow, and an attribute type that was being
flattened). **It has since been fixed**: `Hearth.h` now includes
`MatterEndPoint.h` and the four `MatterEndpoints/*.h` headers this library
implements, the same aggregation upstream's own `Matter.h` does. A
regression test (`test/host/test_matter_umbrella.cpp`) compiles a
translation unit whose only `iLabs Hearth` include is `Matter.h`,
declaring all four device types at file scope; it fails to build if this
aggregation ever regresses.

**All three now compile and link with no build flags at all**, on
`pico:rp2040:challenger_2350_wifi6_ble5`, still byte-identical to upstream.
Items 1 and 2 were closed by filling the core's gaps underneath the sketches
rather than editing them:

- **`Preferences`** is implemented in `src/Preferences.{h,cpp}` over
  arduino-pico's EEPROM. See "Preferences" below.
- **`BOOT_PIN`** resolves to the BOOTSEL button. See "BOOT_PIN" below.

Full commands and output for the original three-blocker analysis are in
`iLabs_AT_Hearth`'s Task 9 report.

### FullAPI references (`examples/FullAPI/`)

One sketch per concrete endpoint class this library implements, fifty-four
folders in all. Folder name equals sketch name throughout, and equals the
class name in four places: `HearthEvse` and `HearthUtilityMeter` hold the
`MatterEvse` and `MatterElectricalUtilityMeter` sketches (named for the
Hearth-side surface they spend most of their lines on),
`MatterCooktopComposed` is a second `MatterCooktop` sketch, the one with
surfaces attached, and `HearthThreadRole` has no class behind it at all
(see the note at the end of this subsection). The two typed owned
children have no folder of their own: `MatterOvenCavity` is exercised
inside `MatterOven`'s sketch and `MatterCookSurface` inside
`MatterCooktopComposed` (the zero-surface `MatterCooktop` folder is
unchanged), since an owned child only exists through its owner. Where the tiers
above prove "an unmodified upstream sketch builds" and "several classes
compose into something demo-able", this tier proves "every public member of
this one class actually works", one class at a time.

Every sketch opens with the same banner convention: a comment block listing
every public member of the class, from its header, minus the standing
implementation-detail exclusions (`attributeChangeCB`, `hearthAttrTypeFor`
and the like), and where in the sketch it is exercised. The banner is the
reviewer's checklist, not narrative: cross it off against the class header
in one direction and against the sketch's `setup()`/menu in the other, and
any gap is the sketch's bug, not the class's.

They are bring-up firmware for real hardware, not copies of anything:
`Hearth.poll()` runs first in every `loop()` with a comment on why, a
single-character CDC menu over `Serial` drives every writable member, `?`
prints help, and every controller-observable effect prints the equivalent
`chip-tool` command so a bench session can be checked against a live
commissioned device without guessing cluster and attribute names. See
`iLabs_AT_Hearth`'s Task E1-E4 reports for the round that built this tier,
and its Task C9 report for the eight added when the seven-type batch's
device types reached the library surface.

**`HearthThreadRole/` (Task 4, 0.11.0) is the one folder in this tier with
no device type behind it.** The Thread role surface lives on the Hearth
global, not on any `Matter*` class (see "Thread role and mesh identity"
above), so its sketch exercises `Hearth.threadInfo()`/`threadRole()`/
`onThreadRoleChange()`/`hearthThreadRoleName()` directly and, deliberately,
never calls `Matter.begin()` -- the sketch's own banner explains why
(declaring zero endpoints and reconciling would wipe a real device's
composition). Everything else about the tier's conventions above still
applies: the banner-as-checklist, `Hearth.poll()` first, the menu, and the
`chip-tool` cross-reference (`threadnetworkdiagnostics read ...` in place of
a cluster/attribute pair, since this command decodes six of that cluster's
attributes at once rather than wrapping one).

## Preferences

`arduino-pico` ships no `Preferences` and nothing equivalent, so every
arduino-esp32 sketch that remembers state across a reboot fails to compile on
a Challenger. This library supplies one, claiming the global `Preferences.h`
include name so an unmodified `#include <Preferences.h>` resolves. The full
upstream API is implemented, including the typed put/get pairs, `putBytes`/
`getBytes`, `isKey`, `getType`, `clear`, `remove` and `freeEntries`.

**It is backed by EEPROM, not a filesystem.** arduino-pico always reserves a
4 KB flash sector for EEPROM whatever the board's flash menu says, whereas the
default flash option for every Challenger is "8MB (no FS)", so a LittleFS
store would fail to mount on a default install and every `get` would silently
return its default.

Where it differs from NVS on an ESP32:

- The whole store is 4 KB for all namespaces together. A `put` that does not
  fit fails and returns 0 rather than evicting anything.
- No wear levelling: every `put` rewrites the sector. At the rate the examples
  write (a light being switched) that is decades of flash life. A sketch that
  writes in a loop will destroy the sector, exactly as it would using EEPROM
  directly.
- `partition_label` is accepted and ignored. One sector has no partitions.
- Namespace and key names are capped at 15 characters, as NVS caps them, so a
  name that would fail there fails here rather than working by accident.

Move or shrink the region with `-DHEARTH_PREFS_OFFSET=` and
`-DHEARTH_PREFS_SIZE=` if the sketch also uses EEPROM directly.

## BOOT_PIN

The upstream examples read `BOOT_PIN` to offer decommission-by-long-press. On
RP2040 and RP2350 that button is BOOTSEL, which is not a GPIO: it is sampled
by momentarily driving the QSPI chip select to Hi-Z. `arduino-pico` exposes it
as a `BOOTSEL` object an unmodified sketch cannot reach.

So `HearthCompat.h` defines `BOOT_PIN` as a reserved pin number outside any
variant's GPIO range, and `HearthBootPin.cpp` defines `pinMode()` and
`digitalRead()` to recognise it and route it to `BOOTSEL`, forwarding every
real pin to the core's own implementation unchanged. No macros are involved:
`arduino-pico` declares both functions as weak aliases, which is exactly the
hook this needs.

**Verified on hardware 2026-07-28**, not only at link time: an unmodified
`MatterOnOffLight` on a Challenger toggles its light from the BOOTSEL button
and reports the change to the fabric, so the override, the polarity and the
cache interval all hold up against the example's real 250 ms debounce.

Reading BOOTSEL is expensive, unlike a GPIO read: the core disables
interrupts, idles the other core and busy-waits. The examples poll it twice
per `loop()` with no delay, so the shim caches the reading for
`HEARTH_BOOTSEL_CACHE_MS` (default 5 ms, far below every example's 250 ms
debounce). Set it to 0 to read through on every call.

`-DBOOT_PIN=<gpio>` still wins, for a board with a real user button wired to
one.

## Limitations

- **Parity on controller-driven callbacks is conditional on the sketch
  calling into the library.** There is no background task here as there is
  on an ESP32; a `loop()` that never touches `Matter.*` or an endpoint
  object must call `Hearth.poll()`. See "Driving the event loop" above,
  including the callback-latency and re-entrancy consequences.
- **`createSecondaryNetworkInterface()` and
  `getSecondaryNetworkEndPointId()` are not implemented.** They exist
  upstream for devices with more than one network interface; the C6 image
  has exactly one.
- **`MatterEndPoint::getAttribute()` is not implemented.** Upstream returns
  an `esp_matter::attribute_t *`, a handle into ESP-IDF's live data model
  that only exists on the device actually running esp-matter. A host on
  RP2350 has no such data model to hand a handle into, and there is no
  host-side type this could plausibly return. Use `getAttributeVal`,
  `setAttributeVal` and `updateAttributeVal` instead.
- **`AT+MTATTRX`, the firmware's opaque-attribute-type command, is
  specified but unimplemented.** The attribute surface this library can
  read or write is integers and booleans only; string, array and float
  attributes are unsupported and any attempt reports `+MTERR:5`. A generic
  transport on this SDK pin could in principle serve string/octet
  attributes, but every list attribute this library has actually needed
  (cabinet levels, occupancy HoldTimeLimits) is CHIP-delegate-served and
  unreachable through it regardless, so it stays reserved rather than built.
- **All twenty of arduino-esp32's endpoint classes are implemented**, with
  two narrower deferrals inside otherwise-complete classes
  (`MatterOccupancySensor` HoldTime, `MatterWindowCovering` absolute
  position). See "Parked" under "Supported device types" above.
  Command-forwarding for app-adjudicated commands (the door-lock family) is
  no longer deferred: `MatterDoorLock` implements it as a Hearth original,
  see "Hearth originals" above, not as an addition to this twenty.
- **A sketch that uses core 1 AND EVSE charging targets ran on as little as
  56 bytes of stack margin before 0.12.1, and on 616 to 804 after it.**
  Measured on hardware 2026-08-19, by painting core 0's stack and scanning
  for the deepest word touched, so libc, arduino-core and interrupt frames
  are all counted. Not a probe reading and not a compiler estimate. The
  chain is the `+MTCMD` dispatch -> `hearthOnDeferredWork()` -> your
  `onSetTargets()` -> `hearthMergeByDay()`, and up to 0.12.0 the last of
  those built a whole second `HearthChargingSchedule` (1224 bytes of frame:
  1192 of locals plus 32 of saved registers) on top of the `proposed` one
  your callback already holds. 56 bytes is an order of magnitude under this
  library's own 512-byte "act" floor, and 56 bytes from core 1's stack,
  which sits immediately below core 0's with no protection. Nothing
  overflowed in any run, on a 70-target proposal (the largest the wire
  allows) or a 1-target one.
  **0.12.1 merges in place**, validating the whole merge before it touches
  the cache instead of building a second schedule and swapping it in, and
  took essentially all of the available headroom: an improvement of 560 to
  648 bytes against a ceiling of 656. That ceiling is measured, not
  argued. A run that DENIES the proposal never executes the merge at all,
  and 0.12.1-allow, 0.12.1-deny and 0.12.0-deny all measure 2004 bytes free
  on a minimal sketch, to the byte, so after 0.12.1 the merge is not
  detectable on the stack and the peak is the proposal fetch and verdict
  wire path (716 bytes deep), which that change never touched.
  **The biggest single term was the example sketch, not the library.** A
  `HearthChargingSchedule` declared inside a `switch` case of `loop()` is
  hoisted into the function prologue by GCC and is live across
  `Hearth.poll()` on every iteration, whether or not that branch ever runs:
  1216 bytes, on the deepest path, always. The FullAPI `HearthEvse` example
  did exactly that and no longer does (`loop()`'s prologue is 8 bytes now,
  and the schedule lives in a `noinline` helper). If you copy from these
  examples, keep large value types out of any `loop()` that calls into this
  library. The figures above predate that fix, so a sketch without such a
  local sits near the minimal shape's 1984 to 2004 instead.
  **Reading stack figures on this platform, three corrections:**
  `rp2040.getFreeStack()` measures against `__scratch_x_start__`, core 1's
  stack base, unless the sketch defines `setup1` or `loop1`
  (`RP2040Support.h`), so a single-core sketch's printed figure is 4096
  bytes higher than its real core 0 margin. A point probe inside
  `onSetTargets()` **overstates** the true margin in every version, because
  it is never taken at the peak (1512 printed in every run, against 56-156
  free at the 0.12.0 peak and 616-804 at the 0.12.1 one). And run-to-run
  jitter is 50 to 100 bytes because interrupt frames share this stack, so
  apply any band to the worst of several runs rather than to one.
  A last one for anyone predicting instead of measuring: `-fstack-usage`
  over library sources alone undercounts real depth by a factor of about
  2.2 here (the fetch chain counts 312 iLabs bytes and measures 716; the
  `setChargingSchedule` push chain counts 448 and measures 996). The push
  chain being the deeper of the two still matters, since the deferred drain
  deliberately permits wire traffic from inside `onSetTargets()`: a sketch
  that pushes a schedule from its own SetTargets callback becomes the peak
  itself, bottoming out near 520 free on the shipped shape. That is the
  tightest case on this path now, so keeping `onSetTargets()` shallow (no
  string formatting, no nested library calls, record the request and act on
  it from `loop()`) is not a formality.
- **`MatterDoorLock`'s 1000 ms verdict window is a real latency budget, not
  a formality.** A sketch whose `loop()` blocks for a meaningful fraction of
  a second can miss `onLock`/`onUnlock` entirely and the lock fails closed.
  See "Hearth originals" above.
- **The automatic co-processor reset has been exercised on hardware.** Verified
  during C4 end-to-end tests (2026-07-28 commissioning cycle, 2026-08-03
  transport smoke check against both single-stack and combined firmware). See
  "Wiring".
- **This library claims two names it does not own: `Preferences.h` and the
  weak `pinMode`/`digitalRead` symbols.** Both are what make an unmodified
  sketch compile, and neither is scoped to Matter. A sketch that installs
  another `Preferences` library gets the usual "Multiple libraries were
  found" notice and one of the two wins; a library that also defines
  `pinMode` or `digitalRead` strongly would be a duplicate-symbol link error.
  Nothing in the ecosystem does the latter today, but it is a real cost of
  keeping the examples byte-identical, and it belongs in a general
  arduino-pico compatibility library rather than here if one ever exists.
- **The `Preferences` store is 4 KB with no wear levelling**, against NVS's
  tens of kilobytes with. See "Preferences" above for the full list of where
  it diverges.
- **Known release blocker: which `esp_matter` revision is normative is
  undecided.** `arduino-esp32` 3.3.8, whose class surface this library
  mirrors, bundles `esp_matter` 1.4.1. The Hearth firmware pins `v1.5.1`.
  Several device-type namespaces were renamed between those two revisions,
  and three of arduino-esp32's classes (`MatterColorLight`,
  `MatterEnhancedColorLight`, `MatterThermostat`) call namespaces present in
  **neither**, in upstream's own implementation. All three are implemented
  here anyway: this library never calls `esp_matter` directly (it drives
  the C6 over `AT+MTATTR`), so their cluster/attribute IDs are plain
  integers, verified directly against connectedhomeip's zap-generated
  headers rather than against either `esp_matter` revision's namespace
  names, and the firmware independently confirms all three device types on
  the wire (`AT_MT_SPEC.md` §3.9). The underlying
  question this bullet is really about is still open: a host library whose
  class surface is fixed to one `esp_matter` revision, talking to firmware
  pinned to a different one, means a future core or SDK bump can silently
  rename a namespace out from under a device type that used to work, for
  whichever class is next ported this way rather than by verifying plain
  IDs against connectedhomeip source directly. The naming design settles the
  mechanism (host class names are frozen to arduino-esp32's surface
  regardless of what any `esp_matter` revision calls the underlying
  namespace; the mapping becomes a versioned table), but not the
  revision-drift question itself. See
  `superpowers/specs/2026-07-26-at-mt-full-api-design.md` §6.3, in the
  private `iLabs_Hearth_docs` repository, for the full namespace table.

## Status

Host-side (`test/host/`) coverage exercises the transport, the attribute
codec, endpoint declaration and reconciliation, and the composition apply
sequence without any hardware. Hardware verification: the automatic reset
path and the transport API were verified during C4 end-to-end (2026-07-28)
and transport smoke tests (2026-08-03). See `HARDWARE-BRINGUP.md` for
additional commissioning flows and coverage.

**1.1.0** adds the two `ArduinoMatter` calls the AT surface had and the
library did not: `openCommissioningWindow()` (`AT+MTCOMMISSION`) and
`deviceState()` (`AT+MTSTATE?`), see [Reopening the pairing
window](#reopening-the-pairing-window). Both were on the wire from the
first release; the library never wrapped them and the 1.0.0 review did not
notice. Firmware 1.1.0 ships alongside with the matching images in `fw/`;
its one host-visible change is that `AT+CGMM` now names the co-processor's
real chip on the nRF port (a C6 still answers `ESP32-C6 Hearth`). The
device-type surface is unchanged.

**1.0.0** is the first release a newcomer can follow end to end without
being handed anything privately: the library carries the Hearth firmware
images and their flasher in [`fw/`](fw/), `HearthFirstLight` is the named
entry point, and the firmware pairing is a
[table](#firmware-and-library-versions) rather than a paragraph that has to
be remembered. The device-type surface is unchanged from 0.12.1.

**0.11.0** adds the Thread role and mesh identity surface (`threadInfo()`/
`threadRole()`/`onThreadRoleChange()`/`hearthThreadRoleName()`). A review
round added `HEARTH_THREAD_UNKNOWN` as its own sentinel, distinct from
`HEARTH_THREAD_UNSPECIFIED`, and corrected `partitionId`'s nullability
claim (`hasPartitionId` can be `true` with a genuine `0` while detached
with a dataset installed; `attached` is the only reliable "on a network"
predicate): see "Thread role and mesh identity" above for both.

Bench E2E on real hardware confirmed `threadInfo()` (all seven fields
against the live link), `threadRole()`'s zero-wire-traffic cached read,
and the role decoding, but found the change callback never fired at all:
`onThreadRoleChange()` stored the function pointer with no wire effect,
so bit 28 (opt-in, off by the firmware's default event mask) was never
actually subscribed to and the device, correctly, never sent it. Every
host test had passed regardless, because they all inject `+MTEVT:28`
straight into dispatch, which proves the handler and cannot prove
anything ever asks the device to send it -- recorded here as a test
design lesson, not only a bug. Fixed by making registration itself
subscribe (a background `AT+MTEVT?`/`AT+MTEVT=` read-modify-write,
re-armed on every co-processor reboot and for a registration made before
the link exists): see "Thread role and mesh identity" above.
