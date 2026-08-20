#!/usr/bin/env python3
#
#    Copyright (c) 2026 P. Oldberg <pontus@ilabs.se>
#    SPDX-License-Identifier: LGPL-2.1-or-later
#
#    This library is free software; you can redistribute it and/or
#    modify it under the terms of the GNU Lesser General Public
#    License as published by the Free Software Foundation; either
#    version 2.1 of the License, or (at your option) any later version.
#
#    This library is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#    Lesser General Public License for more details.
#
#    NOTE ON LINKAGE. This file is the one part of the library that is not MIT.
#    It imports esptool (GPL-2.0-or-later) in-process and calls its API
#    directly, rather than invoking it as a subprocess. A combined work
#    distributed with esptool is therefore covered by GPL-2.0-or-later;
#    LGPL-2.1 section 3 permits this file to be taken under GPL-2 for that
#    purpose. Recipients may still use this file alone under the LGPL.
#
#    The firmware images under fw/images/ are unaffected. esptool merely writes
#    them to flash and contributes no code to them.
#
"""
iLabs Hearth firmware flasher: writes a prebuilt image from fw/images/.

The Matter co-processor is the ESP32-C6 on a Challenger RP2350 WiFi6/BLE5
board. The C6 has no USB of its own, so this drives the full two-stage flash:

  Stage 1  copy the USB2Serial bridge UF2 (fw/bridge/) onto the RP2350
           mass-storage device so the RP2350 becomes a USB-to-serial bridge
           for the C6.
  Stage 2  write the Hearth firmware (bootloader + partition table + app) for
           the chosen variant onto the C6 over that serial link, using
           esptool's Python API so we render our own progress bar.

Three variants ship, and they differ only in which Matter transport the C6
speaks. Pick one:

  1) WiFi only     the default. Commission over BLE, run over WiFi.
  2) Thread only   for a Thread network with a border router.
  3) WiFi + Thread one image that can do either, at the cost of RAM.

Every file written is recorded in fw/manifest.json with its SHA-256, and this
script prints those hashes and re-checks them against the files on disk BEFORE
it writes anything. A shipped image that does not match its record is refused,
because the whole point of shipping images is that they are the ones that were
qualified on hardware.

The ESP chip has no USB of its own (the RP2350 bridges it), so a FORKED esptool
is required: it adds an `RP2040Reset` strategy that leaves IO0/DTR asserted low,
keeping the ESP in its download bootloader while the RP2350 bridges. Stock pip
esptool will NOT flash these boards. This script refuses to run without the fork.

Usage:
    python3 flash.py                     # interactive menu
    python3 flash.py --variant wifi      # non-interactive
    python3 flash.py --list              # verify esptool, list variants, exit
    python3 flash.py --variant thread --port /dev/serial/by-id/usb-iLabs_...-if00
    python3 flash.py --skip-bridge       # bridge already running: flash the C6 only
    python3 flash.py --dry-run           # show what would happen, no copy/flash

There is deliberately NO default serial port. A device node number is not an
identity: /dev/ttyACM0 is whatever enumerated first, which on a developer's
bench is as likely to be a radio dongle as the board, and writing AT traffic
into the wrong one can take down whatever is using it. When --port is omitted
the port is either the one that newly appears after the bridge UF2 is copied,
or a port whose USB descriptors identify it as an iLabs Challenger. Never a
guess by device number.

Set ILABS_ESPTOOL_PATH to point at the forked esptool checkout if it is not on
sys.path already. `rich` is used for nicer output if installed (pip install rich).
"""

import argparse
import getpass
import hashlib
import json
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# --------------------------------------------------------------------------- #
# Optional pretty output (rich). Everything degrades gracefully without it.
# --------------------------------------------------------------------------- #
try:
    from rich.console import Console

    _con = Console()

    def cprint(msg="", style=None):
        # markup=False so literal brackets like [dry-run] / [===>] aren't parsed.
        # soft_wrap=True so rich never re-flows a line: a SHA-256 broken across
        # two lines at a width rich picked cannot be compared against
        # manifest.json by eye or pasted in one go.
        _con.print(msg, style=style, highlight=False, markup=False,
                   soft_wrap=True)

    def cinput(prompt):
        return _con.input(prompt)

    HAVE_RICH = True
except Exception:  # rich not installed
    HAVE_RICH = False

    _ANSI = {
        "green": "\033[1;32m",
        "red": "\033[1;31m",
        "yellow": "\033[0;33m",
        "cyan": "\033[1;36m",
        "bold": "\033[1m",
        "dim": "\033[2m",
    }
    _RST = "\033[0m"
    _TTY = hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

    def cprint(msg="", style=None):
        if style and _TTY:
            code = "".join(_ANSI.get(s, "") for s in str(style).split())
            print(f"{code}{msg}{_RST}" if code else msg)
        else:
            print(msg)

    def cinput(prompt):
        return input(prompt)


def die(msg, code=1):
    cprint(f"ERROR: {msg}", style="red")
    sys.exit(code)


# --------------------------------------------------------------------------- #
# Variants. This is the axis the user chooses on, and the only one: every board
# this firmware runs on carries the same ESP32-C6 on the same pinout, so the
# board is a constant and the personality is the variable. (The sibling
# iLabs_ESP-NOW flasher is the other way round: one personality, three boards.)
# --------------------------------------------------------------------------- #
VARIANTS = [
    {
        "id": "wifi",
        "label": "WiFi only",
        "blurb": "commission over BLE, run over WiFi. Most users want this.",
    },
    {
        "id": "thread",
        "label": "Thread only",
        "blurb": "for a Thread network with a border router.",
    },
    {
        "id": "combined",
        "label": "WiFi + Thread",
        "blurb": "either transport from one image, at the cost of RAM.",
    },
]

# The one board. Kept as a dict so the flash call reads the same as the sibling
# flasher's, and so a future board revision is a table entry rather than a diff
# through the whole file.
BOARD = {
    "label": "Challenger RP2350 WiFi6/BLE5  (ESP32-C6)",
    "chip": "esp32c6",
    "mount_label": "RP2350",
    "bridge_uf2": os.path.join("bridge", "RP2350USB2Serial.ino.uf2"),
    # "keep" for all three, deliberately. The shipped images are hashed
    # artifacts: fw/manifest.json records the SHA-256 of the exact bytes that
    # were qualified on hardware. esptool rewrites the mode/freq/size nibbles in
    # the image header when told a value, so anything other than "keep" would
    # write bytes the manifest never saw. The headers already encode dio / 80m /
    # 4MB (mode byte 0x02, size/freq byte 0x20), which is what the qualifying
    # bench flashes used, so "keep" is identical in effect and honest in method.
    "flash_mode": "keep",
    "flash_freq": "keep",
    "flash_size": "keep",
}

# Where partitions.csv puts each image. These are NOT the generic IDF defaults:
# this project puts the partition table at 0xC000 (not 0x8000) and the app at
# 0x20000 (0x10000 is `nvs` here, and holds the Matter fabric and the endpoint
# composition). Writing the app at the IDF default would land it on top of NVS.
BOOTLOADER_BIN = "bootloader.bin"
PARTITION_BIN = "partition-table.bin"
BOOTLOADER_OFFSET = 0x0000
PARTITION_OFFSET = 0xC000
APP_OFFSET = 0x20000

ROM_BAUD = 115200               # ESP ROM download-loader boot baud
FLASH_BAUD = 921600             # fast baud we try to flash at
BAUD_RETRIES = 3                # attempts at FLASH_BAUD before falling back
CONNECT_MODE = "default-reset"  # forked esptool auto-picks RP2040Reset first
RESET_AFTER = "hard-reset"
PORT_SETTLE_S = 2.0             # settle after the bridge port appears, before
                                # flashing: the freshly-booted CDC needs a
                                # moment or the 921600 baud switch fails and
                                # forces the slow 115200 fallback.
BRIDGE_RETRIES = 3              # whole-flash retries if the bridge watchdog-resets
                                # (hangs, reboots, re-enumerates) mid-operation.
BAUD_SETTLE_S = 0.5             # quiet pause after change_baud(). The RP2350
                                # bridge services the host's SET_LINE_CODING
                                # (the new baud) from its main loop, so it needs
                                # a moment with NO command traffic to apply it.

# USB identity of a Challenger host MCU running an arduino-pico sketch.
#
# The identity is the DESCRIPTOR STRINGS, not the vendor ID. The vendor ID is
# useless for this in both directions: it is Raspberry Pi's 0x2E8A on a sketch
# built against the pico-sdk USB stack, which is shared with every other RP2040
# and RP2350 board and with the Pi Debug Probe, and it is Adafruit's 0x239A on
# a sketch built against TinyUSB, which the bridge shipped in fw/bridge/ is.
# Both were measured on the same board, one bridge each. Gating on 0x2E8A
# therefore rejected the bridge this library ships.
#
# "Challenger" or "iLabs" in the manufacturer, product or description string is
# what actually names the board, and it excludes the two devices most likely to
# be sharing a bench with it (a Thread radio dongle and a debug probe).
_ID_HINTS = ("ilabs", "challenger")


# --------------------------------------------------------------------------- #
# esptool discovery + fork verification.
#
# esptool is NOT vendored here. It is GPL-2.0-or-later, and duplicating a copy
# of it inside an Arduino library that is otherwise MIT turns a clear licence
# story into an argument someone else has to have later. fw/README.md says how
# to install the fork.
# --------------------------------------------------------------------------- #
ESPTOOL_MISSING_MSG = (
    "could not import esptool.\n"
    "  The iLabs FORK of esptool is required (it carries the RP2040Reset\n"
    "  strategy needed to flash the ESP through the RP2350 bridge).\n"
    "  Point ILABS_ESPTOOL_PATH at the fork checkout, e.g.:\n"
    "    export ILABS_ESPTOOL_PATH=~/bin/esptool"
)


def import_esptool(explicit_path=None):
    """Locate and import the FORKED esptool, or exit with guidance.

    Discovery order: --esptool-path, then $ILABS_ESPTOOL_PATH, then
    ~/bin/esptool, then whatever `import esptool` already finds. The explicit
    paths go on sys.path AHEAD of site-packages on purpose: a stock pip esptool
    is a common thing to have installed, and it must not win over the fork.
    """
    candidates = []
    if explicit_path:
        candidates.append(explicit_path)
    env = os.environ.get("ILABS_ESPTOOL_PATH")
    if env:
        candidates.append(env)
    candidates.append(os.path.expanduser("~/bin/esptool"))

    # Insert in reverse so the earliest (highest-priority) candidate ends up
    # first on sys.path. A candidate already on sys.path is MOVED to the front
    # rather than skipped: leaving it where it was would make an explicit
    # --esptool-path silently lose to a lower-priority candidate, which is the
    # opposite of what asking for a specific checkout means.
    for path in reversed(candidates):
        if path and os.path.isdir(path):
            path = os.path.abspath(os.path.expanduser(path))
            sys.path = [p for p in sys.path if p != path]
            sys.path.insert(0, path)

    try:
        import esptool  # noqa: E402
        import esptool.reset  # noqa: E402
    except ImportError as e:
        die(f"{ESPTOOL_MISSING_MSG}\n  (import error: {e})")

    if not hasattr(esptool.reset, "RP2040Reset"):
        die(
            "the esptool that was imported is NOT the iLabs fork.\n"
            f"  Imported: esptool {getattr(esptool, '__version__', '?')} from\n"
            f"    {os.path.dirname(esptool.__file__)}\n"
            "  It lacks the RP2040Reset strategy and cannot flash these boards.\n"
            "  Set ILABS_ESPTOOL_PATH to the forked esptool checkout."
        )
    return esptool


# --------------------------------------------------------------------------- #
# The manifest: what we ship, and proof it is what we shipped.
# --------------------------------------------------------------------------- #
def load_manifest():
    path = os.path.join(HERE, "manifest.json")
    if not os.path.isfile(path):
        die(f"missing {path}. Run fw/make_manifest.py after adding images.")
    try:
        with open(path) as fh:
            return json.load(fh)
    except ValueError as e:
        die(f"could not parse {path}: {e}")


def resolve_images(manifest, variant_id):
    """Return (version, [(offset, path, name, sha256, size), ...]) for a variant.

    Every shipped file has to appear in the manifest, and the app has to be the
    one named for this variant and version. A file present on disk but absent
    from the manifest is an error rather than something to flash: it means an
    image was dropped in without regenerating the record, which is exactly the
    "flashed something that was never qualified" failure the manifest exists to
    stop.
    """
    version = manifest.get("version")
    if not version:
        die("manifest.json has no version")
    entry = manifest.get("images", {}).get(variant_id)
    if not entry:
        die(f"manifest.json has no images for variant '{variant_id}'")
    files = entry.get("files", {})

    app_name = f"hearth-{variant_id}-{version}.bin"
    wanted = [
        (BOOTLOADER_OFFSET, BOOTLOADER_BIN),
        (PARTITION_OFFSET, PARTITION_BIN),
        (APP_OFFSET, app_name),
    ]
    known = {name for _, name in wanted}
    extra = sorted(set(files) - known)
    if extra:
        die(f"manifest lists files for '{variant_id}' this flasher does not "
            f"know where to write: {extra}")

    out = []
    for offset, name in wanted:
        rec = files.get(name)
        if rec is None:
            die(f"manifest has no record for {variant_id}/{name}")
        path = os.path.join(HERE, "images", variant_id, name)
        if not os.path.isfile(path):
            die(f"missing firmware image: {path}")
        out.append((offset, path, name, rec["sha256"], rec["size"]))
    return version, out


def resolve_bridge(manifest):
    """Return (offset, path, name, sha256, size) for the bridge UF2.

    The bridge is not written to the ESP's flash, so it has no offset; None
    stands in that column so it travels through the same print and verify path
    as the images rather than getting a shortcut of its own. It is the first
    file written to the board and the one whose corruption strands a user
    earliest, which is why it is covered at all.

    The manifest names the file, not this code, so a bridge renamed or replaced
    in fw/bridge/ is picked up by regenerating the manifest.
    """
    files = manifest.get("bridge", {}).get("files", {})
    if not files:
        die("manifest.json records no bridge. Run fw/make_manifest.py.")
    want = os.path.basename(BOARD["bridge_uf2"])
    rec = files.get(want)
    if rec is None:
        die(f"manifest has no record for bridge/{want} "
            f"(it records {sorted(files)})")
    path = os.path.join(HERE, BOARD["bridge_uf2"])
    if not os.path.isfile(path):
        die(f"missing bridge UF2: {path}")
    return (None, path, want, rec["sha256"], rec["size"])


def print_and_verify_plan(variant, version, images, bridge):
    """Print exactly what is about to be written, then prove it on disk.

    The print happens before the verify so that a mismatch is reported against
    a list the user has already seen, and so the hashes are on screen even when
    the run is aborted for some other reason.
    """
    cprint("")
    cprint(f"About to flash: {variant['label']} ({variant['id']})", style="bold")
    cprint(f"  firmware version {version}", style="bold")
    cprint(f"  board            {BOARD['label']}", style="dim")
    cprint("")
    everything = [bridge] + list(images)
    for offset, path, name, sha, size in everything:
        where = "RP2350   " if offset is None else f"0x{offset:06x}"
        cprint(f"  {where}  {name}  ({size} bytes)")
        # Two-space indent, not aligned under the name: a 64-hex-digit hash
        # plus any deeper indent no longer fits an 80-column terminal, and a
        # wrapped hash is one nobody checks.
        cprint(f"  sha256    {sha}", style="dim")
    cprint("")

    bad = []
    for offset, path, name, sha, size in everything:
        with open(path, "rb") as fh:
            blob = fh.read()
        got = hashlib.sha256(blob).hexdigest()
        if got != sha or len(blob) != size:
            bad.append((name, got, len(blob)))
    if bad:
        for name, got, size in bad:
            cprint(f"  {name}: on disk sha256 {got} ({size} bytes)", style="red")
        die("a shipped file does not match fw/manifest.json. Nothing was "
            "written.\n"
            "  This is not a warning to click past: the manifest records the "
            "files\n"
            "  that were qualified on hardware, so a mismatch means the file "
            "on disk\n"
            "  is not one of them. Re-clone the library, or regenerate the "
            "manifest\n"
            "  with fw/make_manifest.py if you deliberately replaced a file.")
    cprint(f"  all {len(everything)} files match fw/manifest.json",
           style="green")


# --------------------------------------------------------------------------- #
# Serial port helpers (pyserial).
# --------------------------------------------------------------------------- #
def _comports():
    try:
        from serial.tools import list_ports
    except ImportError:
        die("pyserial is required (pip install pyserial).")
    return list(list_ports.comports())


def list_serial_ports():
    return {p.device for p in _comports()}


def is_challenger(port_info):
    """True if this port's USB descriptors identify an iLabs Challenger.

    The sibling ESP-NOW flasher matched any /dev/ttyACM*, which is a positional
    guess, not an identity. On a bench that also carries a Thread radio dongle
    or a debug probe those are ttyACM devices too, and a 1200-baud touch or an
    esptool handshake sent into the wrong one takes down whatever was using it.
    So: match the descriptors the board actually publishes, and if nothing
    matches, ask for --port rather than picking.
    """
    if port_info.vid is None:
        return False        # a legacy /dev/ttyS*, not a USB device at all
    text = " ".join(str(x or "") for x in
                    (port_info.manufacturer, port_info.product,
                     port_info.description)).lower()
    return any(h in text for h in _ID_HINTS)


def challenger_ports():
    return sorted(p.device for p in _comports() if is_challenger(p))


def describe_port(dev):
    for p in _comports():
        if p.device == dev:
            who = " ".join(str(x) for x in (p.manufacturer, p.product) if x)
            return f"{dev} ({who})" if who else dev
    return dev


def wait_for_new_serial_port(before, timeout=30.0):
    """Poll for a serial port that appeared after `before`, return its device.

    A port that was not there a moment ago and is there now is the board that
    just rebooted, so this is an identity by construction and does not need the
    descriptor check. The check still runs, to warn if the newcomer is not what
    we expected.
    """
    deadline = time.time() + timeout
    # A port node appears a moment before its USB descriptors are readable, so
    # a newcomer that does not identify yet is given a grace window to before
    # we say so. Without it the flasher warns that the board is not a
    # Challenger and then, one line later, prints its Challenger product string.
    grace_ends = None
    while time.time() < deadline:
        fresh = sorted((p for p in _comports() if p.device not in before),
                       key=lambda p: p.device)
        if fresh:
            named = [p for p in fresh if is_challenger(p)]
            if named:
                return named[0].device
            if grace_ends is None:
                grace_ends = time.time() + 2.0
            elif time.time() >= grace_ends:
                pick = fresh[0]
                cprint(f"  (the new port {pick.device} does not identify as a "
                       f"Challenger; using it because it is the one that "
                       f"appeared)", style="yellow")
                return pick.device
        time.sleep(0.3)
    return None


def wait_for_port_present(port, timeout=30.0):
    """Wait until the serial device node actually exists.

    With an explicit --port we would otherwise try to use the device the instant
    the bridge UF2 is copied, before the RP2350 has rebooted and re-enumerated
    as a serial bridge, so the connect fails outright.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(port):
            return True
        time.sleep(0.3)
    return False


def find_existing_bridge_port():
    """Pick an already-running bridge port by identity (for --skip-bridge).

    With the bridge already running no new port appears, so there is no
    before/after diff to lean on and the descriptors are the only evidence
    available. One match is an answer; zero or several are a question for the
    user.
    """
    ports = challenger_ports()
    if not ports:
        die("no iLabs Challenger serial port found. Is the bridge firmware "
            "running?\n"
            "  Pass --port <device>, preferably a /dev/serial/by-id/ path.")
    if len(ports) > 1:
        die(f"several Challenger ports are attached {ports}; pick one with "
            f"--port.")
    return ports[0]


def wait_for_bridge_back(explicit_port, prev_port, timeout=30.0):
    """Wait for the bridge port to come back after a watchdog reset.

    The watchdog reboot re-enumerates USB, so the port briefly disappears and
    returns, sometimes with a different name. Prefer the explicit/previous name
    if it reappears; otherwise take the sole Challenger port.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        ports = challenger_ports()
        for pref in (explicit_port, prev_port):
            if pref and (pref in ports or os.path.exists(pref)):
                return pref
        if len(ports) == 1:
            return ports[0]
        time.sleep(0.5)
    return None


# --------------------------------------------------------------------------- #
# RP2350 mass-storage mount detection.
#
# Linux and macOS ONLY, and it says so rather than pretending. Automount paths
# are the whole mechanism here and Windows has none of these: it assigns a
# drive letter, which needs a different search entirely (enumerate the volumes,
# look for the one holding INFO_UF2.TXT). Writing that blind, with no Windows
# machine to run it on, would produce a path nobody has ever executed, and the
# earlier "cross-platform" comment on Linux-only code is exactly how a claim
# like that survives. So: supported platforms are named, and an unsupported one
# fails immediately with the manual route instead of spending 60 seconds
# waiting for a directory that can never appear.
# --------------------------------------------------------------------------- #
SUPPORTED_MOUNT_PLATFORMS = ("linux", "darwin")


def mount_detection_supported():
    return sys.platform.startswith(SUPPORTED_MOUNT_PLATFORMS)


def unsupported_platform_message(uf2):
    return (
        f"automatic bridge installation is not supported on "
        f"'{sys.platform}'.\n"
        f"  Finding the RP2350's mass-storage drive is done through Linux and "
        f"macOS automount\n"
        f"  paths, and this platform has neither. Everything else works, so do "
        f"stage 1 by hand:\n"
        f"    1. put the board in BOOTSEL (hold BOOTSEL, tap RESET)\n"
        f"    2. copy this file onto the drive that appears:\n"
        f"         {uf2}\n"
        f"    3. re-run with --skip-bridge --port <the board's serial port>")


def candidate_mount_paths(label):
    user = getpass.getuser()
    if sys.platform == "darwin":
        return [os.path.join("/Volumes", label)]
    # Linux desktop automount locations.
    return [
        os.path.join("/media", user, label),
        os.path.join("/run/media", user, label),
        os.path.join("/media", label),
    ]


def find_mount(label):
    for p in candidate_mount_paths(label):
        if os.path.isdir(p):
            return p
    return None


def wait_for_mount(label, timeout=60.0):
    cprint(f"Waiting for '{label}' mass-storage device (hold BOOTSEL and tap "
           f"RESET if it does not appear)...", style="cyan")
    deadline = time.time() + timeout
    while time.time() < deadline:
        p = find_mount(label)
        if p:
            cprint(f"  found at {p}", style="dim")
            return p
        time.sleep(0.5)
    return None


def touch_reset_to_bootsel(port):
    """Reboot the RP2350 into BOOTSEL by opening `port` at 1200 baud.

    arduino-pico implements the standard touch reset: a 1200-baud open with DTR
    deasserted reboots into the mass-storage bootloader. That is what makes the
    whole flash a single command with no button press.
    """
    try:
        import serial
    except ImportError:
        die("pyserial is required for the 1200-baud touch reset")

    cprint(f"Rebooting the board into BOOTSEL (1200-baud touch on {port})...",
           style="cyan")
    try:
        s = serial.Serial(port=None, baudrate=1200)
        s.port = port
        s.dtr = False
        s.open()
        time.sleep(0.15)
        s.close()
    except OSError as e:
        # The port frequently vanishes the instant the reboot takes effect,
        # which is success, not failure.
        cprint(f"  (port closed as the board rebooted: {e})", style="dim")
    time.sleep(1.0)


def try_auto_bootsel(port_hint=None):
    """Put the board into BOOTSEL without the button, when we can prove which
    device to touch.

    A sketch built with arduino-pico reboots into mass storage on a 1200-baud
    open, so the button press is avoidable. The risk is touching the wrong
    device, so this acts only on a port that identifies itself as a Challenger
    (or on an explicit --port), and otherwise falls back to asking for the
    button, which always works.
    """
    if find_mount(BOARD["mount_label"]):
        cprint("Board is already in BOOTSEL.", style="dim")
        return

    port = port_hint
    if not port:
        candidates = challenger_ports()
        if len(candidates) == 1:
            port = candidates[0]
        elif not candidates:
            cprint("  (no Challenger serial port to reset; put the board in "
                   "BOOTSEL by hand, or pass --port.)", style="dim")
            return
        else:
            cprint(f"  ({len(candidates)} Challenger ports attached; not "
                   f"guessing which to reset. Pass --port, or press BOOTSEL.)",
                   style="dim")
            return

    touch_reset_to_bootsel(port)


# --------------------------------------------------------------------------- #
# Custom esptool logger: a single in-place "frame", one compound per-file
# coloured progress bar plus a couple of live stat lines underneath it.
#
# The overall percentage is TRUE byte-based progress across every file. The
# coloured segments are byte-weighted so the app (which is ~98% of the bytes)
# dominates the bar, but each file keeps a small minimum width so a tiny one
# (bootloader / partition table) still shows its colour. While a flash is active
# esptool's own chatter is silenced by flipping verbosity to "silent" so the
# frame stays a stable block; real errors still get through.
#
# NOTE on installation: EsptoolLogger is a __new__-based singleton, so calling a
# subclass constructor just hands back the same instance and log.set_logger() is
# a no-op for it. The only thing that actually swaps in our methods is rebinding
# the singleton's class directly (log.__class__ = ProgressLogger), which keeps
# every base method (print/stage/set_verbosity/...) intact.
# --------------------------------------------------------------------------- #
_PART_PALETTE = (45, 213, 82, 214, 141, 208, 39, 220)  # 256-colour fg codes
_BAR_CELLS = 46          # width of the compound bar, in cells
_MIN_SEG = 3             # min coloured cells per file so its hue is visible
_ADDR_RE = re.compile(r"0x([0-9a-fA-F]+)")


def _human_bytes(n):
    n = float(n)
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024.0 or unit == "GB":
            return f"{int(n)} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0


def install_progress_logger(esptool):
    from esptool.logger import log, EsptoolLogger

    tty = hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

    class ProgressLogger(EsptoolLogger):
        # set_logger() cannot swap this singleton's class (see note above), and
        # __init__ never runs on the rebind, so ALL state is class-level here and
        # (re)initialised in begin_plan(). Never rely on __init__.
        _plan = None
        _prev_verbosity = None
        _frame_open = False
        _last_draw = 0.0
        _idx = 0
        _local = 0.0
        _pb_prefix = None        # used only by the plain fallback bar

        # ---- plan lifecycle ------------------------------------------------ #
        def begin_plan(self, partitions):
            """partitions: list of {"name", "base", "size", "color"} in flash
            order. Brackets a write_flash() so progress_bar() can render the
            compound frame and esptool's own output stays out of the way."""
            cum = 0
            for p in partitions:
                p["cum_before"] = cum
                cum += p["size"]
            self._plan = partitions
            self._total = cum or 1
            self._t0 = time.time()
            self._frame_open = False
            self._last_draw = 0.0
            self._idx = 0
            self._local = 0.0
            self._prev_verbosity = self._verbosity
            self.set_verbosity("silent")

        def end_plan(self):
            if self._plan is None:
                return
            if tty and self._frame_open:
                self._render(force=True, finish=True)
                sys.stdout.write("\n")
                sys.stdout.flush()
            elif not tty:
                print(f"  flash complete: {_human_bytes(self._total)} written")
            if self._prev_verbosity is not None:
                self.set_verbosity(self._prev_verbosity)
            self._plan = None
            self._frame_open = False

        # ---- the hook esptool calls ---------------------------------------- #
        def progress_bar(self, cur_iter, total_iters, prefix="", suffix="",
                         bar_length=32):
            if self._plan is None:
                return self._simple_bar(cur_iter, total_iters, prefix, suffix,
                                        bar_length)
            # Which file are we in? Key off the "Writing at 0x...." address: it
            # is base + bytes_written, so the last file whose base it has
            # reached is the current one (files flash in ascending order).
            m = _ADDR_RE.search(prefix)
            addr = int(m.group(1), 16) if m else 0
            idx = 0
            for i, p in enumerate(self._plan):
                if addr >= p["base"]:
                    idx = i
            crossed = idx != self._idx        # file boundary: always paint
            self._idx = idx
            self._local = (max(0.0, min(1.0, cur_iter / total_iters))
                           if total_iters else 1.0)
            # Small files can flash faster than the redraw throttle; force a
            # frame at each boundary so every file's colour is seen.
            self._render(force=crossed)

        # ---- rendering ----------------------------------------------------- #
        def _render(self, force=False, finish=False):
            now = time.time()
            local = 1.0 if finish else self._local
            last = self._idx == len(self._plan) - 1
            complete = finish or (last and local >= 1.0)
            # Throttle to ~20 fps; always draw the very first and last frames.
            if (not force and not complete and self._frame_open
                    and (now - self._last_draw) < 0.05):
                return
            self._last_draw = now

            lines = self._frame_lines(now, finish)
            if not tty:
                # Dumb terminal: emit one line only at each file boundary.
                if finish or local >= 1.0:
                    p = self._plan[self._idx]
                    gfrac = (p["cum_before"] + local * p["size"]) / self._total
                    print(f"  {p['name']}: 100%   overall {100 * gfrac:5.1f}%")
                return
            self._paint(lines)

        def _frame_lines(self, now, finish):
            """Build the fixed block of display lines (with ANSI). Pure: no I/O,
            so it can be unit-tested by stripping the escape codes."""
            plan = self._plan
            idx = self._idx
            local = 1.0 if finish else self._local
            cur = plan[idx]
            done = self._total if finish else (cur["cum_before"] + local * cur["size"])
            gfrac = max(0.0, min(1.0, done / self._total))
            elapsed = max(1e-6, now - self._t0)
            rate = done / elapsed
            cur_bytes = cur["size"] if finish else int(local * cur["size"])

            seg_w = self._segment_widths()
            bar = self._compound_bar(seg_w, idx, local, finish)
            rate_s = f"{_human_bytes(rate)}/s" if elapsed > 0.4 else "..."

            return [
                f"  Flashing Hearth firmware  •  {len(plan)} files  •  "
                f"{_human_bytes(self._total)} total",
                f"  [{bar}]  {100 * gfrac:5.1f}%",
                "   " + self._legend(idx, finish),
                f"  ● {cur['name']}  0x{cur['base']:08x}  "
                f"{_human_bytes(cur_bytes)} / {_human_bytes(cur['size'])}",
                f"  elapsed {elapsed:4.1f}s  •  {rate_s}  •  "
                f"file {idx + 1}/{len(plan)}",
            ]

        def _paint(self, lines):
            out = sys.stdout
            if not self._frame_open:
                out.write("\n".join(lines) + "\n")
                self._frame_open = True
            else:
                out.write(f"\033[{len(lines)}A")          # cursor up N lines
                for ln in lines:
                    out.write("\r\033[2K" + ln + "\n")     # clear + rewrite
            out.flush()

        def _segment_widths(self):
            """Cells per file: byte-weighted, but at least _MIN_SEG each so
            small files stay visible. Always sums to _BAR_CELLS."""
            plan = self._plan
            n = len(plan)
            cells = _BAR_CELLS
            if cells <= _MIN_SEG * n:
                w = [cells // n] * n
                w[-1] += cells - sum(w)
                return w
            rem = cells - _MIN_SEG * n
            w = [_MIN_SEG + int(round(rem * p["size"] / self._total)) for p in plan]
            # Absorb rounding drift into the biggest (app) segment.
            biggest = max(range(n), key=lambda i: plan[i]["size"])
            w[biggest] += cells - sum(w)
            return w

        def _compound_bar(self, seg_w, idx, local, finish):
            parts = []
            for i, w in enumerate(seg_w):
                c = self._plan[i]["color"]
                if finish or i < idx:
                    filled = w
                elif i > idx:
                    filled = 0
                else:
                    filled = max(0, min(w, int(round(w * local))))
                if filled:
                    parts.append(f"\033[1;38;5;{c}m" + "█" * filled)
                if w - filled:
                    parts.append(f"\033[2;38;5;{c}m" + "░" * (w - filled))
            parts.append("\033[0m")
            return "".join(parts)

        def _legend(self, idx, finish):
            chips = []
            for i, p in enumerate(self._plan):
                c = p["color"]
                mark = "✓" if finish or i < idx else "►" if i == idx else "·"
                chips.append(f"\033[38;5;{c}m●\033[0m {p['name']} {mark}")
            return "   ".join(chips)

        # ---- plain fallback bar (stray progress calls outside a plan) ------- #
        def _simple_bar(self, cur_iter, total_iters, prefix, suffix, bar_length):
            if prefix != self._pb_prefix and self._pb_prefix is not None:
                sys.stdout.write("\n")
            self._pb_prefix = prefix
            frac = max(0.0, min(1.0, (cur_iter / total_iters) if total_iters else 1.0))
            filled = int(bar_length * frac)
            if filled >= bar_length:
                bar = "=" * bar_length
            elif filled == 0:
                bar = " " * bar_length
            else:
                bar = "=" * (filled - 1) + ">" + " " * (bar_length - filled)
            line = f"  {prefix}[{bar}] {100 * frac:5.1f}% {suffix}"
            if tty:
                sys.stdout.write("\r\033[K" + line)
                if cur_iter >= total_iters:
                    sys.stdout.write("\n")
                    self._pb_prefix = None
                sys.stdout.flush()
            elif cur_iter >= total_iters:
                print(line)
                self._pb_prefix = None

    # Rebind the singleton's class directly: the only install that actually
    # takes effect (set_logger() is a no-op for this singleton).
    log.__class__ = ProgressLogger
    return log


# --------------------------------------------------------------------------- #
# The flash procedure.
# --------------------------------------------------------------------------- #
def _close_port(esp):
    try:
        esp._port.close()
    except Exception:
        pass


def _connect_and_prepare(esptool, port, target_baud):
    """Fresh reset -> stub -> (verified) baud change -> attach flash.

    Returns a ready-to-flash esp object. Raises on ANY failure; the caller
    resets and retries. A fresh detect_chip() re-toggles RTS/DTR through the
    fork's RP2040Reset, rebooting the ESP into its ROM download loader at
    115200, a clean resync no matter what state a botched baud change left.
    """
    esp = esptool.detect_chip(port, connect_mode=CONNECT_MODE)
    try:
        esp = esptool.run_stub(esp)
        if target_baud and target_baud > esp.ESP_ROM_BAUD:
            esp.change_baud(target_baud)
            # change_baud() switches the ESP stub and the host port; the latter
            # sends the bridge a SET_LINE_CODING for the new baud, which the
            # bridge applies from its main loop. Give it a quiet moment before
            # we talk to the ESP again, then let the normal flash flow (whose
            # commands esptool retries) be the real check. Do NOT probe here: an
            # un-retried read at the new baud races the bridge's switch and
            # forces the slow 115200 fallback for no reason.
            time.sleep(BAUD_SETTLE_S)
        esptool.attach_flash(esp)
        return esp
    except Exception:
        _close_port(esp)
        raise


def _connect_with_recovery(esptool, port):
    """Establish a working link, recovering from lost baud-change requests.

    Tries FLASH_BAUD a few times (each attempt hardware-resets and resyncs),
    then falls back to ROM_BAUD with no baud switch at all: slower, but it
    cannot hit the baud-change bug. Returns (esp, baud_used).
    """
    last_err = None
    for attempt in range(1, BAUD_RETRIES + 1):
        try:
            cprint(f"Connecting to {BOARD['chip']} on {port} at {FLASH_BAUD} "
                   f"(attempt {attempt}/{BAUD_RETRIES})...", style="cyan")
            esp = _connect_and_prepare(esptool, port, FLASH_BAUD)
            return esp, FLASH_BAUD
        except esptool.FatalError as e:
            last_err = e
            cprint(f"  link not stable at {FLASH_BAUD}: {e}", style="yellow")
            # Let the OS release the port; the next detect_chip re-resets the ESP.
            time.sleep(0.8)

    cprint(f"Falling back to {ROM_BAUD} baud (no baud switch, slower but "
           f"reliable)...", style="yellow")
    try:
        esp = _connect_and_prepare(esptool, port, None)
        return esp, ROM_BAUD
    except esptool.FatalError as e:
        raise esptool.FatalError(
            f"could not establish a link at any baud. "
            f"Last {ROM_BAUD} error: {e}; last {FLASH_BAUD} error: {last_err}")


def do_flash(esptool, images, port, keep_baud=False):
    from esptool.logger import log

    addr_data = [(offset, path) for offset, path, _, _, _ in images]
    plan = [{
        "name": name,
        "base": offset,
        "size": size,
        "color": _PART_PALETTE[i % len(_PART_PALETTE)],
    } for i, (offset, path, name, sha, size) in enumerate(images)]

    if keep_baud:
        cprint(f"\nConnecting to {BOARD['chip']} on {port} at {ROM_BAUD} "
               f"(--keep-baud)...", style="cyan")
        esp = _connect_and_prepare(esptool, port, None)
        baud = ROM_BAUD
    else:
        esp, baud = _connect_with_recovery(esptool, port)

    try:
        cprint(f"Flashing at {baud} baud...", style="cyan")
        # Bracket write_flash so our compound frame owns the terminal for the
        # duration; end_plan() restores esptool's normal output for reset_chip.
        if hasattr(log, "begin_plan"):
            log.begin_plan(plan)
        try:
            esptool.write_flash(
                esp,
                addr_data,
                flash_mode=BOARD["flash_mode"],
                flash_freq=BOARD["flash_freq"],
                flash_size=BOARD["flash_size"],
            )
        finally:
            if hasattr(log, "end_plan"):
                log.end_plan()
        esptool.reset_chip(esp, RESET_AFTER)
    finally:
        _close_port(esp)


def stage1_copy_bridge(uf2, dry_run=False, port_hint=None, auto_bootsel=True):
    """Copy the bridge UF2 onto the RP2350. `uf2` has already been checked
    against the manifest by print_and_verify_plan; this never re-derives the
    path, so there is no way to write a file the verify pass did not see."""
    if dry_run:
        cprint(f"[dry-run] would wait for mount '{BOARD['mount_label']}' and "
               f"copy", style="yellow")
        cprint(f"[dry-run]   {uf2}", style="dim")
        return list_serial_ports(), None

    if not mount_detection_supported():
        die(unsupported_platform_message(uf2))

    if auto_bootsel:
        try_auto_bootsel(port_hint)

    mount = wait_for_mount(BOARD["mount_label"])
    if not mount:
        die(f"timed out waiting for '{BOARD['mount_label']}'. Is the board in "
            f"BOOTSEL mode?")

    # Snapshot the ports HERE, not before the touch reset. In BOOTSEL the board
    # is a mass-storage device with no serial port at all, so anything that
    # turns up after the copy is the bridge and nothing else. Taken any earlier
    # the board's own pre-reset port is in the set, and a board that
    # re-enumerates onto the same device node would never look new.
    ports_before = list_serial_ports()

    time.sleep(0.5)

    cprint("Copying the USB-to-serial bridge firmware...", style="cyan")
    try:
        shutil.copy(uf2, mount)
        try:
            os.sync()
        except (AttributeError, OSError):
            pass
    except OSError as e:
        # The RP2350 often reboots mid-copy; that is expected once the UF2 is in.
        cprint(f"  (device rebooted during copy: {e})", style="dim")

    return ports_before, mount


# --------------------------------------------------------------------------- #
# Variant selection UI.
# --------------------------------------------------------------------------- #
def print_variants(manifest=None):
    cprint("Firmware variants:", style="bold")
    for i, v in enumerate(VARIANTS, 1):
        cprint(f"  {i}) {v['label']:<14} {v['blurb']}")
    if manifest:
        cprint(f"\n  version {manifest.get('version', '?')} "
               f"(fw/manifest.json)", style="dim")


def resolve_variant(selector):
    """Accept a 1-based menu index or a variant id."""
    if selector is None:
        return None
    s = str(selector).strip().lower()
    if s.isdigit():
        idx = int(s)
        if 1 <= idx <= len(VARIANTS):
            return VARIANTS[idx - 1]
        die(f"variant index out of range: {idx}")
    for v in VARIANTS:
        if v["id"] == s:
            return v
    die(f"unknown variant: {selector!r} (use --list to see choices)")


def choose_variant_interactive(manifest):
    print_variants(manifest)
    while True:
        try:
            sel = cinput("\nSelect variant [1-%d] (q to quit): "
                         % len(VARIANTS)).strip()
        except (EOFError, KeyboardInterrupt):
            cprint("\nAborted.", style="yellow")
            sys.exit(130)
        if sel.lower() in ("q", "quit", "exit"):
            sys.exit(0)
        if sel.isdigit() and 1 <= int(sel) <= len(VARIANTS):
            return VARIANTS[int(sel) - 1]
        cprint("Invalid selection.", style="red")


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def main():
    global BAUD_RETRIES, PORT_SETTLE_S
    ap = argparse.ArgumentParser(
        description="iLabs Hearth firmware flasher (prebuilt images in "
                    "fw/images/).")
    ap.add_argument("--variant", help="wifi, thread or combined (or a menu "
                    "index 1-3). Omit for an interactive menu.")
    ap.add_argument("--port", help="serial port of the RP2350 bridge. There is "
                    "no default: pass a /dev/serial/by-id/ path, or leave it "
                    "off and let the flasher use the port that appears after "
                    "the bridge UF2 is copied.")
    ap.add_argument("--esptool-path", help="path to the forked esptool checkout")
    ap.add_argument("--keep-baud", action="store_true",
                    help="flash at the ROM baud (%d) with no baud switch at all"
                    % ROM_BAUD)
    ap.add_argument("--baud-retries", type=int, default=BAUD_RETRIES,
                    help="attempts at %d before falling back to %d (default %d)"
                    % (FLASH_BAUD, ROM_BAUD, BAUD_RETRIES))
    ap.add_argument("--settle", type=float, default=PORT_SETTLE_S,
                    help="seconds to wait after the bridge port appears before "
                    "flashing (default %.1f)" % PORT_SETTLE_S)
    ap.add_argument("--skip-bridge", action="store_true",
                    help="skip stage 1 (the bridge UF2 copy) and flash the C6 "
                    "over an already-running bridge")
    ap.add_argument("--no-auto-bootsel", action="store_true",
                    help="do not try the 1200-baud touch reset; wait for the "
                    "BOOTSEL button instead")
    ap.add_argument("--list", action="store_true",
                    help="verify the esptool fork, list variants, and exit")
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would happen without copying or flashing")
    args = ap.parse_args()

    if args.baud_retries is not None and args.baud_retries >= 0:
        BAUD_RETRIES = args.baud_retries
    if args.settle is not None and args.settle >= 0:
        PORT_SETTLE_S = args.settle

    manifest = load_manifest()

    esptool = import_esptool(args.esptool_path)
    ver = getattr(esptool, "__version__", "?")
    cprint(f"esptool fork OK (v{ver}, RP2040Reset present)", style="green")

    if args.list:
        print_variants(manifest)
        return 0

    variant = resolve_variant(args.variant) or choose_variant_interactive(manifest)
    version, images = resolve_images(manifest, variant["id"])
    bridge = resolve_bridge(manifest)
    print_and_verify_plan(variant, version, images, bridge)

    if args.dry_run:
        if args.skip_bridge:
            cprint("[dry-run] would skip the bridge UF2 copy (--skip-bridge)",
                   style="yellow")
        else:
            stage1_copy_bridge(bridge[1], dry_run=True)
        port = args.port or "<the port that appears after the UF2 copy>"
        cprint(f"[dry-run] would flash on {port}:", style="yellow")
        for offset, path, name, sha, size in images:
            cprint(f"[dry-run]   0x{offset:06x}  images/{variant['id']}/{name}",
                   style="dim")
        cprint(f"[dry-run]   chip={BOARD['chip']} mode={BOARD['flash_mode']} "
               f"freq={BOARD['flash_freq']} size={BOARD['flash_size']} "
               f"after={RESET_AFTER}", style="dim")
        cprint(f"[dry-run]   baud: try {FLASH_BAUD} x{BAUD_RETRIES}, else fall "
               f"back to {ROM_BAUD}", style="dim")
        cprint(f"[dry-run]   settle: wait for port, then {PORT_SETTLE_S:.1f}s "
               f"before flashing", style="dim")
        return 0

    if args.skip_bridge:
        # Bridge already running: skip the UF2 copy and just find its port. No
        # settle needed since the CDC has long since come up. esptool's own
        # reset (default-reset / RP2040Reset) still drops the C6 into the
        # download loader at connect time.
        cprint("Skipping the bridge UF2 copy (--skip-bridge); using the "
               "already-running bridge.", style="cyan")
        if args.port:
            port = args.port
            if not wait_for_port_present(port):
                die(f"serial port {port} is not present. Is the bridge running?")
        else:
            port = find_existing_bridge_port()
        cprint(f"Using {describe_port(port)}", style="dim")
    else:
        # Stage 1: bridge UF2 -> RP2350 mass storage.
        ports_before, _ = stage1_copy_bridge(
            bridge[1], port_hint=args.port,
            auto_bootsel=not args.no_auto_bootsel)

        # Stage 2a: find the serial bridge port.
        if args.port:
            port = args.port
            cprint(f"Waiting for serial port {port} to appear...", style="cyan")
            if not wait_for_port_present(port):
                die(f"serial port {port} never appeared after the UF2 copy. "
                    f"Is the bridge firmware running?")
            cprint(f"Using {describe_port(port)}", style="dim")
            time.sleep(0.5)
        else:
            cprint("Waiting for the serial bridge to enumerate...", style="cyan")
            port = wait_for_new_serial_port(ports_before)
            if not port:
                # The diff is the preferred evidence, but it is not the only
                # evidence: a board that re-enumerates while something else on
                # the bus also changes can defeat it. Fall back to the
                # descriptor check, which is still an identity, and still
                # refuses to choose between two candidates.
                named = challenger_ports()
                if len(named) == 1:
                    port = named[0]
                    cprint("  (no new port appeared, but exactly one attached "
                           "device identifies as a Challenger)", style="dim")
            if not port:
                die("no new serial port appeared after the UF2 copy. Re-run "
                    "with --port <device>.")
            cprint(f"  bridge appeared at {describe_port(port)}", style="dim")
            time.sleep(0.5)
        # The device node appears a beat before the freshly-booted bridge CDC is
        # actually ready to talk; flashing too early makes the 921600 baud switch
        # lose its request/ack and forces the slow 115200 fallback. Let it settle.
        if PORT_SETTLE_S > 0:
            cprint(f"Letting the bridge settle ({PORT_SETTLE_S:.1f}s)...",
                   style="dim")
            time.sleep(PORT_SETTLE_S)

    # Stage 2b: flash the C6, recovering across bridge watchdog resets. If the
    # bridge hangs its watchdog reboots it (re-forcing the C6 into the download
    # loader and re-enumerating USB); wait for the port to come back and retry
    # the whole flash rather than dying.
    install_progress_logger(esptool)
    for attempt in range(1, BRIDGE_RETRIES + 1):
        try:
            do_flash(esptool, images, port, keep_baud=args.keep_baud)
            cprint(f"\nHearth {version} ({variant['label']}) flashed OK.",
                   style="green")
            # Do not suggest checking with a terminal on this port: the
            # flashing bridge parks the C6 in its ROM download loader and
            # leaves it there, so nothing answers until a sketch replaces the
            # bridge. See "Confirming the flash" in fw/README.md.
            cprint("Load your sketch next. The library resets the C6 into run "
                   "mode on first use;", style="dim")
            cprint("Hearth.firmwareVersion() then reports the version "
                   f"({version}).", style="dim")
            return 0
        except KeyboardInterrupt:
            cprint("\nAborted.", style="yellow")
            return 130
        except Exception as e:  # esptool FatalError, serial disconnect, etc.
            if attempt >= BRIDGE_RETRIES:
                die(f"flash failed after {attempt} attempt(s): {e}")
            cprint(f"\nFlash attempt {attempt}/{BRIDGE_RETRIES} failed ({e}).",
                   style="yellow")
            cprint("Waiting for the bridge to re-enumerate (watchdog reset?)...",
                   style="cyan")
            newport = wait_for_bridge_back(args.port, port)
            if not newport:
                die("the bridge did not come back after a reset; aborting.")
            if newport != port:
                cprint(f"  bridge is back at {newport} (was {port})", style="dim")
            port = newport
            if PORT_SETTLE_S > 0:
                time.sleep(PORT_SETTLE_S)   # let the freshly-rebooted CDC settle
    return 0


if __name__ == "__main__":
    sys.exit(main())
