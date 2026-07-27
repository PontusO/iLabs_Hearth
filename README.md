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

- Mode 0 (`setAttributeVal()`) echoes nothing and fires no handler. That is
  what it is for: a host reflecting a change that came *from* a controller
  uses mode 0 so it does not bounce back at the fabric.
- Upstream calls `attributeChangeCB` on `PRE_UPDATE`, so a handler returning
  `false` there vetoes the write. Over the AT link the co-processor reports
  the change on `POST_UPDATE`, after it has already been applied, so the
  return value cannot veto anything. Returning `false` still suppresses the
  library's own cache update, as upstream's does.

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

With the fix in place, `MatterTemperatureSensor` (the one example that
does not depend on `Preferences.h`) was recompiled. The `does not name a
type` cascade is gone; only the pre-existing, external `BOOT_PIN` error
remains. Supplying `BOOT_PIN` via a compiler flag rather than editing the
sketch (`arduino-cli compile --build-property
"compiler.cpp.extra_flags=-DBOOT_PIN=0" ...`, which is the same mechanism
a board variant's own `pins_arduino.h` would use, not a sketch patch)
makes `MatterTemperatureSensor` **compile and link cleanly**: zero errors
attributable to this library. `MatterOnOffLight` and `MatterDimmableLight`
remain blocked at their `#include <Preferences.h>` line, upstream of any
Hearth-specific code, so that experiment does not extend to them; a real
`Preferences.h` implementation for `arduino-pico`, not a define, would be
needed to test them the same way, and none exists to test against. Full
commands and output are in `iLabs_AT_Hearth`'s Task 9 report.

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
- **The default link (`Serial1`) is an assumption, not a verified fact.**
  See "Wiring" above.
- **Two of the three upstream examples still do not compile against
  arduino-pico, for reasons outside this library.** `MatterOnOffLight` and
  `MatterDimmableLight` both `#include <Preferences.h>`, which does not
  exist anywhere in the `arduino-pico` core. `MatterTemperatureSensor`
  compiles and links cleanly once the RP2040/RP2350 core's missing
  `BOOT_PIN` macro is supplied externally; see "Examples" above for detail
  and for the parity gap this project did find and fix here (`Matter.h`
  not aggregating the endpoint headers).
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
