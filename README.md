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

Call `Hearth.begin(serial)` in `setup()`, before `Matter.begin()`, to name
the `Stream` the AT link runs over:

```cpp
Hearth.begin(Serial1);
```

If a sketch skips `Hearth.begin()` entirely (true of every unmodified
upstream example, since upstream has no such call), the library lazily
starts a default link the first time it is needed. That default is
`Serial1` at 115200 baud, `#define`d as `HEARTH_DEFAULT_SERIAL` in
`src/Hearth.h` behind an `#ifndef` a board variant can override before that
header is first included.

**This default is a documented assumption, not a verified fact.** It is
what the Challenger's host-to-co-processor UART is believed to be wired to;
it has not yet been confirmed against real hardware. Until it is, an
unmodified sketch that relies on the zero-configuration path should be
treated as unverified on this point specifically, and a sketch that cares
should call `Hearth.begin()` explicitly with whichever `Serial` it has
confirmed reaches the C6.

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
  Matter.begin();  // harmless once commissioned; drives the link's poll loop
}
```

Endpoint objects' `begin()` only declares the endpoint locally; nothing goes
over the wire until `Matter.begin()` reconciles the sketch's declared
composition against the C6's live one. This is why every upstream example,
and this rule, calls `Matter.begin()` last, after every endpoint's own
`begin()`.

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

**They call `WiFi.begin()`.** On an ESP32 board that starts the same radio
Matter's own network stack uses. On a Challenger it does not: the RP2350's
`WiFi` (where present) is a wholly separate radio from the ESP32-C6's, and
the C6 owns the Matter network connection on its own, independent of
whatever the RP2350's `WiFi.begin()` call does or does not connect to. The
call is inert for Matter's purposes on this platform. This is left as-is in
the copied sketches rather than patched out, for the same byte-identity
reason above.

**They do not currently compile against `arduino-pico` as shipped.**
Compiling all three with `arduino-cli` against a Challenger RP2350 board
surfaced three distinct blockers, none of them hidden by patching a sketch:

1. `MatterOnOffLight` and `MatterDimmableLight` `#include <Preferences.h>`,
   a header the `esp32` core ships and `arduino-pico` does not. This is a
   gap in the RP2040/RP2350 core, not in this library.
2. `MatterTemperatureSensor` references the bare macro `BOOT_PIN`, which no
   `arduino-pico` board variant defines (`esp32` boards define it; RP2040/
   RP2350 boards have no standard equivalent). Also a core gap, not this
   library's.
3. All three fail earlier still on a gap that **is** this library's: this
   library's `Matter.h` (via `Hearth.h`) does not `#include` the endpoint
   class headers (`MatterEndPoint.h` and the four headers under
   `MatterEndpoints/`) the way upstream's `Matter.h` aggregates all twenty
   of its own. A sketch that does nothing but `#include <Matter.h>` and then
   declares `MatterOnOffLight light;` at file scope, exactly as every
   upstream example does, currently fails with `'MatterOnOffLight' does not
   name a type`.

Item 3 is a real parity gap found by this compile attempt, in the same vein
as two gaps found earlier in this project by the same method (a union
member set that was too narrow, and an attribute type that was being
flattened). It is reported here rather than silently patched, per this
project's own rule for compile failures surfaced by the upstream examples.
Full commands and output are in `iLabs_AT_Hearth`'s Task 9 report.

## Limitations

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
- **The default link (`Serial1`) is an assumption, not a verified fact.**
  See "Wiring" above.
- **The upstream examples do not currently compile against arduino-pico.**
  See "Examples" above for the three specific blockers, one of which is a
  gap in this library rather than in the sketches or the RP2040/RP2350 core.
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
examples above, and a real commissioning flow) has not happened yet.
