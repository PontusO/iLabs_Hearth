# iLabs Hearth

An Arduino library for RP2040/RP2350 hosts (the iLabs Challenger family) that
mirrors the `arduino-esp32` `Matter` library's class API closely enough that
an **unmodified** arduino-esp32 Matter sketch can build and run on a
Challenger. The Matter stack itself does not run on the RP2350: it runs on an
ESP32-C6 co-processor flashed with the iLabs Hearth firmware
([`iLabs_AT_Hearth`](https://github.com/PontusO/iLabs_AT_Hearth)), and this
library speaks that firmware's `AT+MT` protocol over a UART link. Every call
a sketch makes against `Matter.*` or a `Matter*Light`/`Matter*Sensor` object
is translated into an AT command sent to the C6 and back.

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
carries no trademark), is recorded in `iLabs_AT_Hearth`'s
`docs/superpowers/specs/2026-07-27-c4-host-library-naming-design.md`.

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
endpoint composition; only `AT+MTFRESET` does.

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
`AT+MTFLOW` accepts only mode `0`. See §3.14 of `docs/AT_MT_SPEC.md` in the
firmware repo.

`Hearth.begin(stream)` remains as an escape hatch for a bench rig that puts
something else in the middle. The caller then owns that stream completely:
it must already be started at the right baud, and no automatic reset is
performed. Call `Hearth.hearthResetCoprocessor()` afterwards if the variant
does define the control pins.

## Minimal example

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
README suggested it. It is not harmless: on a composition the C6 rejects
(an unimplemented device type, or more than sixteen endpoints) every call
runs a clear, the endpoint writes, an apply and a co-processor reboot, so
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

## Supported device types

Four of arduino-esp32's twenty `Matter*` endpoint classes exist today:

| Class | Device type ID |
|---|---|
| `MatterOnOffLight` | `0x0100` |
| `MatterDimmableLight` | `0x0101` |
| `MatterColorTemperatureLight` | `0x010C` |
| `MatterTemperatureSensor` | `0x0302` |

The remaining sixteen (`MatterColorLight`, `MatterFan`, `MatterThermostat`,
`MatterWindowCovering`, the various sensor and plug classes, and so on) are
not implemented. Declaring one of them in a sketch fails to link, the same
as any other undefined symbol.

## Examples

`examples/` holds three of `arduino-esp32`'s own Matter example sketches,
copied **byte-identical** from
`libraries/Matter/examples/` in the `esp32` Arduino core (3.3.8), and
verified with `diff` against that source. Byte-identity is the point: these
sketches are the actual evidence that an unmodified sketch is in scope, and
editing even one line to make it compile would remove that evidence. See
`iLabs_AT_Hearth`'s Task 9 report for the exact `diff` invocation and
result.

- `MatterOnOffLight`
- `MatterDimmableLight`
- `MatterTemperatureSensor`

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
  attributes are unsupported and any attempt reports `+MTERR:5`.
- **Sixteen of arduino-esp32's twenty endpoint classes do not exist yet.**
  Only `MatterOnOffLight`, `MatterDimmableLight`,
  `MatterColorTemperatureLight` and `MatterTemperatureSensor` are
  implemented; see "Supported device types" above.
- **The automatic co-processor reset has not been exercised on hardware.**
  The pins and the UART come from the board variant and the sequence is the
  one `iLabs_ESP-NOW` already runs on the same board, but the
  `HEARTH_READY_TIMEOUT_MS` default of 10 s is a guess: nobody has measured
  how long the C6 takes to reach `+MTREADY` when it also has to rebuild a
  stored endpoint composition and start the Matter stack. See "Wiring".
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
  **neither**. This is acceptable while prototyping and is not acceptable
  to ship: a host library whose class surface is fixed to one `esp_matter`
  revision, talking to firmware pinned to a different one, means a future
  core or SDK bump can silently rename a namespace out from under a device
  type that used to work. The naming design settles the mechanism (host
  class names are frozen to arduino-esp32's surface regardless of what any
  `esp_matter` revision calls the underlying namespace; the mapping becomes
  a versioned table), but not the revision-drift question itself. See
  `iLabs_AT_Hearth`'s `docs/superpowers/specs/2026-07-26-at-mt-full-api-design.md`
  §6.3 for the full namespace table.

## Status

Host-side (`test/host/`) coverage exercises the transport, the attribute
codec, endpoint declaration and reconciliation, and the composition apply
sequence without any hardware. Hardware verification (the default link, the
examples above, and a real commissioning flow) has not happened yet. See
`HARDWARE-BRINGUP.md` for what to check first once it does.
