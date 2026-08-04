/*
 * Hearth.h - the Hearth global (link configuration, diagnostics and the
 * link-event surface for the AT+MT link to the ESP32-C6 co-processor), and
 * matterEvent_t / ArduinoMatter / Matter, the Matter-named surface.
 *
 * The HearthClass parts below have no arduino-esp32 counterpart and live on
 * Hearth rather than on the Matter-named class because the naming rule bars
 * adding members to a Matter-named class: that surface must stay exactly
 * what upstream defines, nothing more. See CLAUDE.md, "Naming and legal
 * constraint".
 *
 * ArduinoMatter and matterEvent_t, further down, ARE that Matter-named
 * surface: this is the file src/Matter.h's `#include "Hearth.h"` shim
 * resolves to, so an unmodified sketch's `Matter.*` and `matterEvent_t`
 * calls land here. It gets exactly the members upstream's Matter.h defines,
 * nothing more; see that class's own comment below for the two exceptions
 * and why they are not really exceptions.
 *
 * This header is also the umbrella upstream's own Matter.h is: upstream
 * pulls in all twenty of its MatterEndpoints/Matter*.h headers so that a
 * sketch's bare `#include <Matter.h>` is enough to declare a device object
 * at file scope, without the sketch including any endpoint header itself.
 * Task 9's compile pass found this file did not do the same (a gap the
 * task's own file scope did not cover, since this file predates it), so a
 * sketch built from nothing but `#include <Matter.h>` plus a file-scope
 * `MatterOnOffLight light;` failed to compile: exactly the pattern every
 * upstream example uses. Fixed by including MatterEndPoint.h and, below,
 * every endpoint header this library implements: the original four, plus
 * the thirteen the devtype expansion (Tasks L1-L6) added, seventeen of
 * upstream's twenty in total. None of those headers includes this one back
 * (checked: only the endpoint .cpp files and MatterEndPoint.cpp include
 * Hearth.h, and .cpp files are leaves nothing else includes), so this is a
 * one-way dependency, not a cycle broken only by #pragma once appearing to
 * work.
 */
#pragma once

#include <functional>
#include <Arduino.h>
#include "HearthLink.h"
#include "HearthCompat.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterOnOffLight.h"
#include "MatterEndpoints/MatterDimmableLight.h"
#include "MatterEndpoints/MatterColorTemperatureLight.h"
#include "MatterEndpoints/MatterTemperatureSensor.h"
#include "MatterEndpoints/MatterOnOffPlugin.h"
#include "MatterEndpoints/MatterDimmablePlugin.h"
#include "MatterEndpoints/MatterContactSensor.h"
#include "MatterEndpoints/MatterRainSensor.h"
#include "MatterEndpoints/MatterWaterFreezeDetector.h"
#include "MatterEndpoints/MatterWaterLeakDetector.h"
#include "MatterEndpoints/MatterHumiditySensor.h"
#include "MatterEndpoints/MatterPressureSensor.h"
#include "MatterEndpoints/MatterOccupancySensor.h"
#include "MatterEndpoints/MatterFan.h"
#include "MatterEndpoints/MatterWindowCovering.h"
#include "MatterEndpoints/MatterThermostat.h"
#include "MatterEndpoints/MatterEnhancedColorLight.h"

/*
 * The board variant is the single source of truth for the link: which UART
 * reaches the co-processor and which pins drive its reset and boot-mode
 * lines. A sketch never names a serial port, exactly as in the sibling
 * iLabs_ESP-NOW library, and for the same two reasons: the arduino-esp32
 * Matter API this library mirrors has nowhere to name one, and the wiring
 * is a property of the board rather than of the sketch.
 *
 * On a Challenger RP2350 WiFi6/BLE5 the variant supplies ESP_SERIAL_PORT =
 * Serial2 (GP4 TX, GP5 RX), PIN_ESP_MODE = GP14 and PIN_ESP_RST = GP15.
 * Serial1 there is the board's *external* UART on GP12/13 and is not the
 * co-processor link; earlier revisions of this library assumed it was.
 *
 * A board that wires the C6 elsewhere overrides HEARTH_SERIAL_PORT with a
 * build flag. Without either macro the library refuses to compile for the
 * target: see hearthEnsureLink() in Hearth.cpp.
 */
#if !defined(HEARTH_SERIAL_PORT) && defined(ESP_SERIAL_PORT)
#define HEARTH_SERIAL_PORT ESP_SERIAL_PORT
#endif

/*
 * Baud for the AT link. Matches the firmware's boot baud, AT_UART_BAUD in
 * at_core (shared with the ESP-NOW firmware).
 *
 * The firmware can be retuned at runtime with AT+MTBAUD, but this library
 * deliberately does not: the rate is not persisted, so every co-processor
 * reset returns the link to 115200, and hearthEnsureLink() resets the C6 on
 * every host start. Nothing in the Matter parity surface needs a faster
 * link. A sketch that wants one can drive AT+MTBAUD through
 * Hearth.hearthCommand() and restart the host UART itself.
 */
#ifndef HEARTH_LINK_BAUD
#define HEARTH_LINK_BAUD 115200
#endif

/*
 * Max wait for +MTREADY after releasing the co-processor's reset.
 *
 * Deliberately far more generous than the 3000 ms the ESP-NOW library
 * allows its own +ENREADY. Hearth raises the marker only after app_main has
 * rebuilt the stored endpoint composition and run esp_matter::start(), a
 * much heavier boot than a bare ESP-NOW image. The real figure has not been
 * measured on hardware yet; tighten this once it has.
 */
#ifndef HEARTH_READY_TIMEOUT_MS
#define HEARTH_READY_TIMEOUT_MS 10000
#endif

/*
 * Safety-net ceiling on hearthArmExpectedReboot(): how long an arm may sit
 * unconsumed before it self-clears. Not a tuned value, just generous enough
 * to cover a full co-processor reboot and Matter stack reinit; its only job
 * is to stop a caller that forgets to call hearthDisarmExpectedReboot() on
 * its own timeout path from leaving the arm live to swallow an unrelated,
 * later spontaneous reboot as if it were the expected one.
 */
#ifndef HEARTH_REBOOT_ARM_TIMEOUT_MS
#define HEARTH_REBOOT_ARM_TIMEOUT_MS 20000
#endif

#define HEARTH_ERR_NOT_SUPPORTED 8  /* the wire's +MTERR:8, named */

typedef enum {
  HEARTH_TRANSPORT_WIFI = 0,
  HEARTH_TRANSPORT_THREAD = 1,
} HearthTransport;

enum hearthEvent_t {
  HEARTH_LINK_UP = 0,
  HEARTH_LINK_DOWN,
  HEARTH_COPROCESSOR_REBOOTED,
  HEARTH_PROTOCOL_ERROR,
  /* Firmware +MTEVT:27 (AT_MT_SPEC.md S3.12.1): the device holds a fabric
   * but its active transport is not provisioned, so it opened a
   * commissioning window. Hearth-specific, so it arrives here and not on
   * the upstream-parity ArduinoMatter::onEvent() surface. Poll
   * Hearth.transportMismatch() for the same state on demand. */
  HEARTH_TRANSPORT_MISMATCH,
};

class HearthClass {
public:
  using hearthEventCB = std::function<void(hearthEvent_t)>;

  HearthClass();

  /*
   * Escape hatch: run the link on a caller-supplied Stream instead of the
   * variant's UART. For a board no variant describes, or a bench rig that
   * puts something else in the middle. The caller owns that stream
   * completely: it must already be started at the intended baud (Stream
   * itself has no begin(), so `baud` is accepted for interface parity and
   * ignored), and no automatic co-processor reset is performed, because the
   * pins that would drive it are not implied by an arbitrary Stream. Call
   * hearthResetCoprocessor() afterwards if the variant does define them.
   *
   * The normal path is not to call this at all. The first use of the link
   * then brings up HEARTH_SERIAL_PORT at HEARTH_LINK_BAUD and resets the
   * co-processor into a known state on its own. See hearthEnsureLink().
   */
  void begin(Stream &serial, unsigned long baud = HEARTH_LINK_BAUD);

  /*
   * Drive the co-processor's boot-mode and reset lines to give it a clean,
   * deterministic boot into run mode, then block until its +MTREADY (up to
   * HEARTH_READY_TIMEOUT_MS), discarding the boot ROM chatter that shares
   * this UART. The resulting boot marker is consumed as expected, so it
   * raises no HEARTH_COPROCESSOR_REBOOTED.
   *
   * A no-op unless the board variant defines PIN_ESP_MODE and PIN_ESP_RST,
   * and on the host test build. Called automatically by hearthEnsureLink()
   * on the variant-driven path; public so a sketch can recover a wedged
   * co-processor, and so the begin(Stream&) escape hatch has a way to ask
   * for the same treatment.
   *
   * Safe with a device already commissioned: this pin reset does not touch
   * the fabric or the stored composition (the AT+MTRESET and AT+MTFRESET
   * commands do that; the latter also erases the composition).
   */
  void hearthResetCoprocessor();

  /* True if a bare AT probe round-trips OK. */
  bool linkUp();

  /* The S3.12.1 transport-mismatch flag from a live AT+MTNET? round-trip:
   * true when the device holds a fabric but its active transport is not
   * provisioned. Always a fresh query, like every other network predicate
   * in this library. Older firmware never reports it, so this is false
   * there. */
  bool transportMismatch();

  /*
   * Stage the network transport for the NEXT boot (combined-image
   * firmware, AT_MT_SPEC.md S3.12.2). Returns true when the firmware
   * confirms the setting is stored; nothing changes until the
   * co-processor reboots, and the host owns that reboot. On
   * single-transport firmware the command does not exist and this
   * returns false with lastError() == HEARTH_ERR_NOT_SUPPORTED.
   */
  bool setTransport(HearthTransport t);

  /*
   * Read the active and stored transport. A pending switch shows as
   * active != stored. Same not-supported behavior as setTransport().
   */
  bool transport(HearthTransport *active, HearthTransport *stored);

  /* Last +MTERR code any layer above HearthLink reported; 0 if none. */
  int lastError() const {
    return _lastError;
  }

  /* Register the link-event callback (co-processor reboot, protocol
   * trouble). At most one; a later call replaces the previous one. */
  void onLinkEvent(hearthEventCB cb);

  /* Drain any pending URCs without blocking. Call from loop(). */
  void poll();

  /* AT+MTVER?. Empty String on failure. */
  String firmwareVersion();

  /*
   * True once the Matter-named layer (Task 5) has warned the host's Serial
   * that applying the sketch's declared endpoint composition will
   * invalidate controller caches on a device that already belongs to a
   * fabric. Lives here, not on Matter, because the naming rule bars adding
   * members to Matter-named classes, including for test introspection.
   */
  bool warnedAboutRecommission() const {
    return _warnedAboutRecommission;
  }
  /* For the Matter-named layer to call once it has issued that warning. */
  void hearthSetWarnedAboutRecommission(bool warned = true) {
    _warnedAboutRecommission = warned;
  }

  /*
   * True once a composition reconcile (ArduinoMatter::begin()) has failed,
   * and therefore must not be attempted again for the rest of this boot.
   *
   * kHearthMaxReconcileAttempts (Hearth.cpp) bounds one call; this bounds
   * the boot. Upstream's own examples poll the library from loop(), and
   * this library's README used to suggest calling Matter.begin() there too,
   * so without a latch a composition the C6 rejects (unknown device type,
   * or past its 16-endpoint cap) would run AT+MTEPCLEAR, the writes,
   * AT+MTEPAPPLY and a co-processor reboot on every single loop iteration,
   * forever: unbounded NVS wear on the C6 and a device that never finishes
   * booting. Nothing about the inputs changes between those iterations, so
   * a retry can only repeat the damage.
   *
   * The latch is cleared by begin() (a new link is a new start) and by
   * MatterEndPoint::hearthClearDeclarations() (a new composition is a new
   * set of inputs, so it deserves a real attempt), which are also the two
   * hooks the host tests use to start clean.
   *
   * Lives here rather than on ArduinoMatter because the naming rule bars
   * adding members to a Matter-named class.
   */
  bool hearthReconcileFailed() const {
    return _reconcileFailed;
  }
  /* For the Matter-named layer, which cannot hold this state itself. */
  void hearthSetReconcileFailed(bool failed = true) {
    _reconcileFailed = failed;
  }

  /*
   * Arm the link for a reboot the host itself is about to trigger, e.g.
   * immediately before sending AT+MTEPAPPLY. While armed, the next
   * +MTREADY completes the arm silently instead of raising
   * HEARTH_COPROCESSOR_REBOOTED: see hearthExpectedRebootSeen().
   *
   * The arm cannot outlive its wait unattended: `timeout_ms` after arming,
   * poll() and hearthCommand() self-clear it even if no +MTREADY ever
   * arrives (the co-processor crashed or wedged mid-apply), so a later,
   * unrelated spontaneous reboot is not silently swallowed as if it were
   * this one. A caller that detects its own timeout first should still call
   * hearthDisarmExpectedReboot() immediately rather than wait for this
   * ceiling; the deadline is a backstop, not the primary mechanism.
   */
  void hearthArmExpectedReboot(uint32_t timeout_ms = HEARTH_REBOOT_ARM_TIMEOUT_MS);

  /*
   * Clear an arm from hearthArmExpectedReboot() without waiting for its
   * +MTREADY or its deadline. Call this on the caller's own timeout path,
   * so the very next spontaneous reboot (which is now what is happening,
   * since the expected one did not show up in time) is reported rather than
   * swallowed. A no-op if not currently armed.
   */
  void hearthDisarmExpectedReboot();

  /*
   * True once the +MTREADY armed by hearthArmExpectedReboot() has arrived.
   * Does not clear itself; call hearthArmExpectedReboot() again before
   * arming the next wait. The intended use (Task 5's AT+MTEPAPPLY
   * sequence) is a non-blocking poll loop the caller drives itself, the
   * same pattern HearthLink::poll() already uses for URCs in general:
   *   Hearth.hearthArmExpectedReboot();
   *   Hearth.hearthCommand("AT+MTEPAPPLY");
   *   uint32_t start = millis();
   *   while (!Hearth.hearthExpectedRebootSeen()) {
   *     Hearth.poll();
   *     if (millis() - start > OWN_TIMEOUT_MS) {
   *       Hearth.hearthDisarmExpectedReboot();  // do not leave the arm live
   *       break;
   *     }
   *     yield();
   *   }
   */
  bool hearthExpectedRebootSeen() const {
    return _expectedRebootSeen;
  }

  /*
   * Send one AT+MT command through the link and remember its outcome as the
   * last error: a +MTERR code on failure, 0 otherwise. Returns the
   * HearthLink::command() result unchanged (0 OK, >0 +MTERR code, -1 plain
   * ERROR, -2 timeout / link not started).
   */
  int hearthCommand(const char *cmd, HearthLink::LineCb onLine = nullptr, void *arg = nullptr);

  /* Record an error code for a caller that fails before reaching the wire,
   * e.g. an attribute value type the AT protocol cannot carry. */
  void hearthSetError(int code);

  /* The underlying transport, for callers that need it directly. */
  HearthLink &link() {
    return _link;
  }

  /*
   * Report protocol-level trouble the Matter-named layer detected itself,
   * e.g. when the +MTREADY that AT+MTEPAPPLY promises never arrives. That
   * layer cannot hold the event callback itself (the naming rule bars
   * adding anything, including private state, to a Matter-named class), and
   * cannot be a friend of HearthClass without HearthClass naming a
   * Matter-named class back, which is the same rule working the other way.
   * Deliberately narrower than a general "raise any event" method: it only
   * ever raises HEARTH_PROTOCOL_ERROR, so an external caller cannot
   * synthesize HEARTH_LINK_UP or HEARTH_COPROCESSOR_REBOOTED, which this
   * class alone should ever assert really happened.
   */
  void hearthReportProtocolError();

private:
  void hearthEnsureLink();
  void hearthRaiseEvent(hearthEvent_t e);
  void hearthCheckExpectedRebootExpiry();
  static void hearthOnURCLine(const char *line, void *arg);
  static void hearthOnVerLine(const char *line, void *arg);
  static void hearthDispatchEvt(const char *rest, HearthClass *self);

  HearthLink _link;
  int _lastError;
  bool _warnedAboutRecommission;
  bool _reconcileFailed;
  bool _expectingReboot;
  bool _expectedRebootSeen;
  uint32_t _expectedRebootArmedAt;
  uint32_t _expectedRebootTimeoutMs;
  hearthEventCB _linkEventCB;
};

/*
 * The same opt-out upstream's Matter.h gives its own global (see the guard
 * on `Matter` at the bottom of this file), under a Hearth-named macro
 * because this global is Hearth's own invention. A sketch that already has
 * a symbol called Hearth, or that simply does not want the library
 * declaring names it did not ask for, defines NO_GLOBAL_HEARTH, or
 * arduino-esp32's blanket NO_GLOBAL_INSTANCES, and this declaration goes
 * away.
 *
 * As upstream does, the *definition* in Hearth.cpp is left unguarded, so an
 * opted-out sketch still links against an object that exists.
 *
 * That alone is not enough here, and the earlier version of this comment got
 * the reason wrong: it reasoned about the definition disappearing, when what
 * actually disappears under the macro is the *declaration*, and the
 * declaration is what the library's own translation units need. Upstream can
 * get away with the bare guard because none of its own .cpp files ever names
 * its Matter global. This library's do: MatterEndPoint.cpp calls through the
 * Hearth object nine times, Hearth.cpp more. The macro is never scoped to
 * the sketch, either: it arrives as a build-wide -D and reaches every
 * library source. So a build-wide NO_GLOBAL_INSTANCES did not compile at
 * all, which is the opposite of an opt-out.
 *
 * The fix is HearthGlobal.h, an internal header carrying the same `extern`
 * with no guard, which every library source that touches the object includes
 * instead of this one. The guard below then does exactly, and only, what it
 * is for: keep the name out of the *sketch's* translation unit.
 */
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_HEARTH)
extern HearthClass Hearth;
#endif

/*
 * CHIP platform event identifiers. Names and values are read from source,
 * not transcribed from memory (CLAUDE.md's rule on device type IDs applies
 * here for the same reason: a wrong value here is a silent, hard-to-notice
 * bug for any sketch that switches on it):
 *
 *   - the base values: connectedhomeip's
 *     src/include/platform/CHIPDeviceEvent.h, kFlag_IsPublic = 0x8000,
 *     kRange_Public = kFlag_IsPublic, and the PublicEventTypes enum, whose
 *     first member kWiFiConnectivityChange = kRange_Public and the rest
 *     increment from there;
 *   - the four commissioning-session/window events and the four fabric
 *     events (0xD000-0xD007): esp-matter's own components/esp_matter/
 *     esp_matter.h, which extends DeviceEventType with
 *     kCommissioningSessionStarted = kRange_PublicPlatformSpecific + 0x1000,
 *     where kRange_PublicPlatformSpecific = kFlag_IsPublic |
 *     kFlag_IsPlatformSpecific = 0xC000, and the rest increment from there;
 *   - the enumerator names, MATTER_ESP32_SPECIFIC_EVENT's "previous + 1"
 *     value (upstream's own comment claims 0x9000, but the code has no
 *     explicit value there, so it takes MATTER_BLE_DEINITIALIZED + 1 by
 *     ordinary C++ enum rules), and MATTER_ESP32_PUBLIC_SPECIFIC_EVENT =
 *     kRange_PublicPlatformSpecific (0xC000): arduino-esp32 3.3.8's
 *     libraries/Matter/src/Matter.h, lines 56-160.
 */
enum matterEvent_t {
  MATTER_WIFI_CONNECTIVITY_CHANGE = 0x8000,
  MATTER_THREAD_CONNECTIVITY_CHANGE,
  MATTER_INTERNET_CONNECTIVITY_CHANGE,
  MATTER_SERVICE_CONNECTIVITY_CHANGE,
  MATTER_SERVICE_PROVISIONING_CHANGE,
  MATTER_TIME_SYNC_CHANGE,
  MATTER_CHIPOBLE_CONNECTION_ESTABLISHED,
  MATTER_CHIPOBLE_CONNECTION_CLOSED,
  MATTER_CLOSE_ALL_BLE_CONNECTIONS,
  MATTER_WIFI_DEVICE_AVAILABLE,
  MATTER_OPERATIONAL_NETWORK_STARTED,
  MATTER_THREAD_STATE_CHANGE,
  MATTER_THREAD_INTERFACE_STATE_CHANGE,
  MATTER_CHIPOBLE_ADVERTISING_CHANGE,
  MATTER_INTERFACE_IP_ADDRESS_CHANGED,
  MATTER_COMMISSIONING_COMPLETE,
  MATTER_FAIL_SAFE_TIMER_EXPIRED,
  MATTER_OPERATIONAL_NETWORK_ENABLED,
  MATTER_DNSSD_INITIALIZED,
  MATTER_DNSSD_RESTART_NEEDED,
  MATTER_BINDINGS_CHANGED_VIA_CLUSTER,
  MATTER_OTA_STATE_CHANGED,
  MATTER_SERVER_READY,
  MATTER_BLE_DEINITIALIZED,
  MATTER_ESP32_SPECIFIC_EVENT, /* = previous + 1, see the comment above */
  MATTER_COMMISSIONING_SESSION_STARTED = 0xD000,
  MATTER_COMMISSIONING_SESSION_STOPPED,
  MATTER_COMMISSIONING_WINDOW_OPEN,
  MATTER_COMMISSIONING_WINDOW_CLOSED,
  MATTER_FABRIC_WILL_BE_REMOVED,
  MATTER_FABRIC_REMOVED,
  MATTER_FABRIC_COMMITTED,
  MATTER_FABRIC_UPDATED,
  MATTER_ESP32_PUBLIC_SPECIFIC_EVENT = 0xC000,
};

/*
 * ArduinoMatter: the Matter-named surface. Gets exactly the members
 * upstream's Matter.h (arduino-esp32 3.3.8, line ~165 on) defines, nothing
 * more; anything this port needs to invent lives on HearthClass above
 * instead, per CLAUDE.md's naming rule. Two things upstream has are handled
 * differently here, neither of which is really an exception:
 *
 *   - _matterEventCB is upstream's own public static member, not something
 *     this port added. It stays public, as upstream has it, which is what
 *     lets HearthClass's URC dispatcher (Hearth.cpp) invoke it directly
 *     without either class naming the other as a friend.
 *   - the friend class list (MatterOnOffLight, MatterTemperatureSensor,
 *     ...) and the protected _init() upstream has exist so concrete
 *     endpoint classes can reach into ArduinoMatter's private esp_matter
 *     setup. This port's endpoint types (Task 6 on) talk to the C6 over
 *     AT+MTEP / AT+MTATTR instead of a local esp_matter data model, so
 *     there is no equivalent setup to reach into; both are omitted as not
 *     applicable rather than reproduced as dead code.
 *
 * getManualPairingCode() and getOnboardingQRCodeUrl() are declared here and
 * defined in Hearth.cpp rather than inline as upstream has them: upstream's
 * bodies return a hardcoded test credential; these query AT+MTCODES? for
 * the device's real one, which does not fit in a one-line inline body. The
 * signatures match upstream exactly; only the implementation differs.
 */
class ArduinoMatter {
public:
  using matterEventCB = std::function<void(matterEvent_t, const chip::DeviceLayer::ChipDeviceEvent *)>;
  static matterEventCB _matterEventCB;

  static void onEvent(matterEventCB cb) {
    _matterEventCB = cb;
  }

  static void begin();

  static String getManualPairingCode();
  static String getOnboardingQRCodeUrl();

  static bool isWiFiStationEnabled();
  static bool isWiFiAccessPointEnabled();
  static bool isThreadEnabled();
  static bool isBLECommissioningEnabled();

  static bool isDeviceCommissioned();
  static bool isWiFiConnected();
  static bool isThreadConnected();
  static bool isDeviceConnected();
  static void decommission();
};

/* Upstream's own guard, verbatim (arduino-esp32 3.3.8, Matter.h:222). Not
 * an addition to the Matter-named surface: it is part of that surface. */
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MATTER)
extern ArduinoMatter Matter;
#endif
