# Hearth firmware images and flasher

This directory ships the **iLabs Hearth firmware** for the ESP32-C6
co-processor, plus `flash.py`, which writes it to a board. It is here so that
getting a working board needs an Arduino install and a Python interpreter,
not the 15 GB two-SDK toolchain the firmware is built with.

Contents:

| Path | What |
|---|---|
| `images/wifi/`, `images/thread/`, `images/combined/` | one prebuilt image set per variant: bootloader, partition table, application |
| `bridge/RP2350USB2Serial.ino.uf2` | the USB-to-serial bridge sketch for the RP2350 host MCU |
| `manifest.json` | the SHA-256 and size of every file written to a board, images and bridge alike |
| `flash.py` | the flasher |
| `make_manifest.py`, `test_manifest.py` | regenerate and check `manifest.json` |

## Which variant

The three images differ only in which transport the Matter stack speaks. They
are the same firmware and the same AT protocol otherwise.

| # | Variant | Choose it when |
|---|---|---|
| 1 | **WiFi only** | your network is WiFi. **Most users want this one.** Commissioning still happens over BLE; the C6 joins WiFi on the credentials it is handed. |
| 2 | **Thread only** | you have a Thread network and a border router. |
| 3 | **WiFi + Thread** | you want one image that can do either. It carries both stacks, so it has less free RAM than the single-transport images, which caps how many endpoints it can serve when WiFi is the active transport: see [How many endpoints fit](#how-many-endpoints-fit). |

You can reflash between variants at any time. A device that is already
commissioned keeps its fabric across a reflash (the Matter fabric and the
endpoint composition live in NVS, which the flasher does not erase), but a
fabric commissioned over one transport is not reachable over the other, so
after switching you will want to commission again.

## How many endpoints fit

Most sketches declare one endpoint, or a handful, and never meet this
limit. Skip this section until you build something large.

Every endpoint costs RAM on the C6, and the three variants do not start
with the same amount, because a linked network stack holds memory whether
it is the active transport or not. So the limit is not one number:

| Image | Active transport | Endpoints of a typical mix | Endpoints of an energy-heavy mix |
|---|---|---|---|
| WiFi only | WiFi | 28 (the firmware's own maximum) | 28 |
| Thread only | Thread | 28 | 28 |
| WiFi + Thread | Thread | 28 | 28 |
| **WiFi + Thread** | **WiFi** | **about 20** | **about 12** |

Only one row is constrained: the combined image with WiFi as the active
transport. Everything else reaches `MT_COMP_MAX_ENDPOINTS`, the firmware's
own ceiling of 28, with room left over. (This library's own
`HEARTH_MAX_ENDPOINTS` stops a sketch at 24 before the firmware ever sees
it. Raise it with `-DHEARTH_MAX_ENDPOINTS=28` if you need the last four.)

**Do not read "about 20" as a number you can just spend.** Endpoints are
not interchangeable. Measured on this firmware, a simple type (a light, a
sensor, a switch) costs about **1,166 bytes** and an energy type (electrical
sensor or meter, water heater, heat pump, solar, battery, device energy
management, EVSE) costs about **2,210**. One light plus nineteen energy
endpoints is a legal-looking 20 that lands inside the failure band.

### The rule underneath the table

**Keep free heap at startup at or above 24,000 bytes.** That is the real
limit; the endpoint counts above are proxies for it at particular mixes.

To predict your own composition, start from the WiFi-active combined
image's single-endpoint figure of **48,360 bytes** and subtract about 1,166
per simple endpoint and about 2,210 per energy endpoint. If the result is
below 24,000, use fewer endpoints or flash a single-transport image.

The firmware measures the same thing and logs it on every boot:

```
mt_main: free heap at startup: 24204 (BLE resident)
```

That line goes to the **C6's own console UART (GPIO2)**, not to the AT link
and not to the USB port your sketch prints on, so reading it takes a wire
onto GPIO2 or a bridge that forwards that pin. Most people will use the
arithmetic above instead; the log line is there for when a number has to be
settled rather than estimated.

### What exceeding it looks like

Not an error. The composition is accepted, `AT+MTEPAPPLY` answers `OK`, and
the device often commissions successfully. It then fails under controller
traffic: lwIP runs out of memory on a send (CHIP error `0x3000001`),
retransmissions exhaust, and the session times out. It reads like "Matter
is broken" rather than like a documented limit, which is why the margin in
the table is as wide as it is.

The boundary is a band, not a step. In the measurements behind this table
one composition failed and then passed at an identical boot heap, so the
supported floor is set well above the last value that happened to work
rather than one endpoint below the first that failed.

### Re-measuring it

The figures date from 2026-08-20 and were measured on real hardware, both
transports, with the rig committed in the firmware repository as
`test/mt_endpoint_cap.py`. Heap moves with the SDK, with cluster
configuration and with anything that changes what gets linked, so after an
SDK bump or an `sdkconfig.defaults*` edit the cap is re-measured rather
than argued about. The firmware repository's own README carries the full
curve and the failure evidence.

## Prerequisites

### The iLabs fork of esptool (required)

**Stock `pip install esptool` cannot flash these boards.** This is not a
version problem that a newer release fixes. The ESP32-C6 on a Challenger has no
USB of its own: the RP2040/RP2350 host MCU bridges it, and the reset lines
esptool normally toggles (DTR/RTS to EN/IO0) reach the ESP only through that
bridge. The fork adds an `RP2040Reset` strategy that holds IO0/DTR asserted low
across the reset, which is what actually lands the ESP in its ROM download
loader. Without it, esptool resets a chip that then boots the application
instead of the loader, and the connect fails.

`flash.py` therefore checks for the fork and refuses to run without it. Two
refusals, depending on what it found. Nothing is imported that is not the fork:

```
ERROR: could not import esptool.
  The iLabs FORK of esptool is required (it carries the RP2040Reset
  strategy needed to flash the ESP through the RP2350 bridge).
  Point ILABS_ESPTOOL_PATH at the fork checkout, e.g.:
    export ILABS_ESPTOOL_PATH=~/bin/esptool
  (import error: No module named 'esptool')
```

```
ERROR: the esptool that was imported is NOT the iLabs fork.
  Imported: esptool 4.8.1 from
    /home/you/.local/lib/python3.12/site-packages/esptool
  It lacks the RP2040Reset strategy and cannot flash these boards.
  Set ILABS_ESPTOOL_PATH to the forked esptool checkout.
```

The second one is what a stock pip esptool looks like, and it is the common
case: having stock esptool installed is normal, and it is not enough.

The fork is **not vendored here**. It is GPL-2.0-or-later, and carrying a copy
of it inside an Arduino library that is otherwise MIT turns a clear licence
story into an argument somebody has to have later. Clone it and point the
flasher at it:

```sh
git clone https://github.com/PontusO/esptool ~/bin/esptool
export ILABS_ESPTOOL_PATH=~/bin/esptool
```

### Python packages

```sh
pip install pyserial bitstring cryptography
```

Those three are the ones a stock Python is most likely to be missing. The
fork's full dependency set is larger (it also wants `reedsolo`, `PyYAML`,
`intelhex`, `rich_click` and `click`), so the simplest way to get all of them
right is to let pip read the fork's own metadata:

```sh
pip install -e ~/bin/esptool
```

`rich` is optional. `flash.py` uses it for nicer output if it is installed and
falls back to plain ANSI if it is not.

### How the flasher finds esptool

In this order, first hit wins:

1. `--esptool-path <dir>`
2. `$ILABS_ESPTOOL_PATH`
3. `~/bin/esptool`
4. whatever `import esptool` already resolves to (a pip install)

The first three are put **ahead** of site-packages on `sys.path` on purpose: a
stock pip esptool is a common thing to have installed and must not win over the
fork. Whichever one is imported is then checked for `RP2040Reset`, so a wrong
pick is refused rather than silently used.

## Flashing

From the library directory:

```sh
python3 fw/flash.py
```

That prints the variant menu, then runs two stages:

1. **Bridge.** The RP2350 is rebooted into its mass-storage bootloader and
   `bridge/RP2350USB2Serial.ino.uf2` is copied onto it, turning the RP2350 into
   a USB-to-serial bridge for the C6. The reboot is automatic: a 1200-baud open
   on the board's serial port is the standard arduino-pico touch reset, so no
   button press is needed. If the board is not running an arduino-pico sketch
   that answers a touch reset, hold **BOOTSEL** and tap **RESET** instead and
   the flasher will pick the drive up when it appears.
2. **Flash.** The bootloader, partition table and application for the chosen
   variant are written to the C6 over that bridge at 921600 baud, with a
   progress bar. If the fast baud switch does not take (the RP2350 bridge
   occasionally loses the request or its ack), the flasher resets and retries,
   then falls back to 115200, which cannot hit the problem.

**Supported platforms: Linux and macOS.** Stage 2 is pure pyserial and works
anywhere, but stage 1 finds the RP2350's mass-storage drive through Linux and
macOS automount paths, and Windows has no equivalent. On any other platform the
flasher says so immediately, rather than waiting on a directory that cannot
appear, and tells you to copy `bridge/RP2350USB2Serial.ino.uf2` onto the drive
by hand and re-run with `--skip-bridge --port <device>`.

Non-interactive, one variant, no menu:

```sh
python3 fw/flash.py --variant wifi
python3 fw/flash.py --variant thread
python3 fw/flash.py --variant combined
```

Other options worth knowing:

| Option | What |
|---|---|
| `--port <device>` | name the bridge port instead of letting the flasher find it |
| `--list` | verify the esptool fork, list the variants, exit |
| `--dry-run` | print the plan and the hashes, write nothing |
| `--skip-bridge` | the bridge is already running: skip stage 1 |
| `--no-auto-bootsel` | do not touch-reset; wait for the BOOTSEL button |
| `--keep-baud` | flash at 115200 from the start |

### About the serial port

**There is no default port, deliberately.** A device node number is not an
identity: `/dev/ttyACM0` is whatever enumerated first, which on a workbench is
as likely to be a radio dongle or a debug probe as the board, and an esptool
handshake or a 1200-baud touch sent into the wrong one takes down whatever was
using it.

So the flasher never picks by number. Without `--port` it uses the port that
*newly appears* after the bridge UF2 is copied, which is the board that just
rebooted and nothing else. Where no such before/after diff exists (with
`--skip-bridge`, or when deciding what to touch-reset) it matches the USB
descriptors instead, and acts only when exactly one attached device identifies
itself as an iLabs Challenger. Otherwise it asks you for `--port`.

The descriptor it matches on is the manufacturer or product string, never the
vendor ID. The vendor ID does not identify this board: a bridge sketch built
against the pico-sdk USB stack enumerates under Raspberry Pi's `0x2E8A`, which
every RP2040 and RP2350 board and the Pi debug probe also use, while one built
against TinyUSB, as `bridge/RP2350USB2Serial.ino.uf2` is, enumerates under
Adafruit's `0x239A`. Same board, same cable, two vendor IDs.

When you do pass `--port`, prefer a stable name:

```sh
python3 fw/flash.py --variant wifi \
  --port /dev/serial/by-id/usb-iLabs_Challenger_2350_WiFi_BLE_XXXXXXXX-if00
```

## Confirming the flash

The command that answers is `AT+CGMR`. On the AT link at 115200 it replies with
the firmware version and then `OK`:

```
AT+CGMR
1.0.0
OK
```

That version is the one in `manifest.json`, which is also the library's
`version=` in `library.properties`. If the three disagree, the board is not
running the firmware this copy of the library ships.

**Where to send it from.** Not from a PC terminal on the bridge you just
flashed through. `bridge/RP2350USB2Serial.ino.uf2` exists to flash the C6, and
it puts the C6 into its ROM download loader at every boot and leaves it there,
so the application never runs while that bridge is installed and a terminal on
the port gets no answer. (The sketch does that deliberately: the RP2350 USB
stack does not deliver the CDC DTR/RTS line-state changes that would otherwise
let the host choose, so it forces download mode instead. Measured, not assumed:
every DTR/RTS combination answers with the ROM's `boot:0x4
(DOWNLOAD(USB/UART0/SDIO_FEI_FEO))` banner and nothing else.)

Send it from a sketch instead, which is the normal next step anyway. Loading
any sketch replaces the bridge on the RP2350, and the library resets the C6
into run mode on first use, so this prints the version over the AT link the
board wires between the two chips:

```cpp
#include <Matter.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(Hearth.firmwareVersion());   // sends AT+MTVER?
}

void loop() {}
```

The library's `HearthFirstLight` example prints exactly this as its second
line, so opening it (**File > Examples > iLabs Hearth > HearthFirstLight**)
confirms the flash and starts the next step at the same time. The library
README's [Start here](../README.md#start-here) walks through it.

`Hearth.firmwareVersion()` uses `AT+MTVER?`, which carries the same version as
`AT+CGMR` in a prefixed form (`+MTVER:1.0.0`) that a parser can tell apart from
an unsolicited line.

## The manifest

`manifest.json` records the SHA-256 and byte size of **every file this
directory writes to a board**: the nine files under `images/`, and the bridge
UF2 under `bridge/`. The bridge is covered because it is written first, before
any image, so it is the file whose corruption strands a user earliest. It is
not decoration:

- `flash.py` prints the variant, the version and every file's hash **before it
  writes anything**, then re-hashes those files on disk and refuses to write
  if any of them disagrees with the record. The bridge is checked in the same
  pass, before stage 1, and the copy uses the path that pass verified.
- `test_manifest.py` fails if a file and its record disagree, if a file on disk
  is **not** in the manifest at all, if a variant or the bridge is missing, if
  a variant does not ship an app named for it, or if the manifest version and
  `library.properties` have drifted apart.

```sh
python3 fw/test_manifest.py
```

The point is a specific failure: a file copied in from an unqualified build
directory. What ships here is exactly what was tested on hardware, and the hash
is the only thing that can tell one 1.8 MB binary from another. Both directions
matter, which is why there is a check for an unrecorded file as well as one for
a mismatched hash: a file the manifest has never heard of is a file nobody
checked.

The generator hashes directories whole rather than looking for filenames it
knows, so adding a second bridge sketch to `bridge/`, or a fourth variant to
`images/`, is covered without touching any code. If you deliberately replace or
add a file, regenerate the record:

```sh
python3 fw/make_manifest.py
```

### Tracing an image back to a commit

Each application image carries an ESP-IDF descriptor holding `git describe`
output from the tree it was built in. It is separate from the AT-visible
firmware version, so it is worth checking directly:

```sh
python3 -c "print(open('fw/images/wifi/hearth-wifi-1.0.0.bin','rb').read()[48:80].split(b'\0')[0].decode())"
```

The shipped images answer `0.12.0-9-g2bad13e`: a real commit, with no `-dirty`
suffix. A `-dirty` suffix would mean the image came from a tree with
uncommitted changes and cannot be tied to any commit, which is a thing worth
noticing before shipping rather than after a field failure.

## Licences

The library is MIT. Two things in this directory are not covered by that, and
both carry their own notice:

- **`flash.py` is LGPL-2.1-or-later.** It imports esptool (GPL-2.0-or-later)
  in-process rather than shelling out to it, so a combined work distributed
  with esptool falls under GPL-2.0-or-later; LGPL-2.1 section 3 permits this
  file to be taken under GPL-2 for that purpose. The file alone stays LGPL. The
  firmware images are unaffected: esptool only writes them and contributes no
  code to them.
- **The images under `images/` are built firmware**, not source. The Hearth
  application is MIT; the ESP-IDF and esp-matter code linked into it is
  Apache-2.0; the Espressif WiFi and Bluetooth libraries are proprietary
  binaries that Espressif permits to be redistributed on Espressif silicon. No
  copyleft licence reaches the image. The full SBOM is in the firmware
  repository's README.
