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
| `manifest.json` | the SHA-256 and size of every shipped file |
| `flash.py` | the flasher |
| `make_manifest.py`, `test_manifest.py` | regenerate and check `manifest.json` |

## Which variant

The three images differ only in which transport the Matter stack speaks. They
are the same firmware and the same AT protocol otherwise.

| # | Variant | Choose it when |
|---|---|---|
| 1 | **WiFi only** | your network is WiFi. **Most users want this one.** Commissioning still happens over BLE; the C6 joins WiFi on the credentials it is handed. |
| 2 | **Thread only** | you have a Thread network and a border router. |
| 3 | **WiFi + Thread** | you want one image that can do either. It carries both stacks, so it has less free RAM than the single-transport images. |

You can reflash between variants at any time. A device that is already
commissioned keeps its fabric across a reflash (the Matter fabric and the
endpoint composition live in NVS, which the flasher does not erase), but a
fabric commissioned over one transport is not reachable over the other, so
after switching you will want to commission again.

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

`Hearth.firmwareVersion()` uses `AT+MTVER?`, which carries the same version as
`AT+CGMR` in a prefixed form (`+MTVER:1.0.0`) that a parser can tell apart from
an unsolicited line.

## The manifest

`manifest.json` records the SHA-256 and byte size of every file under
`images/`. It is not decoration:

- `flash.py` prints the variant, the version and each file's hash **before it
  writes anything**, then re-hashes the files on disk and refuses to flash if
  any of them disagrees with the record.
- `test_manifest.py` fails if an image and its record disagree, if a variant is
  missing, or if the manifest version and `library.properties` have drifted
  apart.

```sh
python3 fw/test_manifest.py
```

The point is a specific failure: an image copied in from an unqualified build
directory. The images shipped here are the exact ones that were tested on
hardware, and the hash is the only thing that can tell one 1.8 MB binary from
another. If you deliberately replace an image, regenerate the record:

```sh
python3 fw/make_manifest.py
```

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
