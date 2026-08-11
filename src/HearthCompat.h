/*
 * HearthCompat.h - the esp_matter attribute-value types the parity surface
 * leaks onto a host that has no ESP-IDF.
 *
 * MatterEndPoint's public helpers take esp_matter_attr_val_t *, and
 * attributeChangeCB is part of the surface a sketch may override. Those
 * types come from ESP-IDF, which does not exist on RP2350. Same shape as
 * iLabs_ESP-NOW's ilabs_espnow_compat.h: reproduce only the types, constants
 * and macros the public API and the official examples actually touch.
 *
 * Per the naming rule the file is ours (Hearth-named); the types inside keep
 * their upstream esp_matter_* names, because they are interop symbols. Do
 * not rename them.
 *
 * The type list is short by design: CLAUDE.md records a census of all 20
 * arduino-esp32 endpoint classes finding only u8, u16, bool, i16, u32 and
 * one array, and AT+MTATTR carries integers and booleans only. Energy
 * round A widened that carrier to a full 64 bits (the electrical
 * measurement clusters' power-mW and energy-mWh attributes are int64/
 * uint64), so INT64/UINT64 joined the list; the census statement above
 * stays true for the original twenty classes.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

/*
 * BOOT_PIN: the strapping-button pin every arduino-esp32 example reads for
 * its decommission-by-long-press. RP2040 and RP2350 have no such GPIO; the
 * equivalent button is BOOTSEL, which arduino-pico exposes as an object
 * rather than a pin. HEARTH_BOOTSEL_PIN is a reserved number outside any
 * variant's GPIO range (RP2350B, the widest, has 48), and HearthBootPin.cpp
 * teaches pinMode()/digitalRead() to route it to BOOTSEL.
 *
 * Only claimed if nothing else defined BOOT_PIN first, so -DBOOT_PIN=<gpio>
 * still wins for a board with a real user button wired to one.
 */
/*
 * CONFIG_ENABLE_CHIPOBLE: this device is commissioned over BLE, so a sketch
 * must not try to join a WiFi network itself.
 *
 * Upstream's examples all guard their WiFi bring-up with
 * `#if !CONFIG_ENABLE_CHIPOBLE`, because on an ESP32 the same chip runs both
 * the sketch and the Matter stack: a build without BLE commissioning needs
 * the sketch to supply credentials and connect. Undefined evaluates to 0, so
 * leaving it unset compiles that block IN, and the sketch spins in
 * `while (WiFi.status() != WL_CONNECTED)` forever, never reaching
 * Matter.begin().
 *
 * On this platform that loop can never finish. The RP2350 has no radio; its
 * WiFi library drives the C6 over either esp-at (UART) or esp-hosted (SPI),
 * both selected by the board's "ESP WiFi type" menu, and the C6 is running
 * Hearth instead of either of them. One co-processor, one personality. The
 * host and the Matter stack want the same chip, so the host cannot have it.
 *
 * Nor does it need it. The Hearth firmware ships as three variants:
 * WiFi-only, Thread-only, and a combined image that carries both stacks
 * but runs exactly one transport per boot, selected by AT+MTTRANSPORT.
 * BLE commissioning is resident on all three: whichever transport is
 * active, the C6 advertises over BLE, the commissioner hands it that
 * transport's credentials during commissioning (WiFi credentials, or
 * a Thread operational dataset), and the C6 joins the network on its
 * own. So setting this to 1 is not a workaround, it is the accurate
 * description of the build on the other side of the link regardless of
 * which transport that build speaks, and it makes upstream's own switch
 * do exactly what upstream meant by it.
 *
 * A sketch that genuinely wants host-side WiFi (with the C6 reflashed to
 * esp-at or esp-hosted, and therefore no Matter) can set this to 0.
 */
#ifndef CONFIG_ENABLE_CHIPOBLE
#define CONFIG_ENABLE_CHIPOBLE 1
#endif

#define HEARTH_BOOTSEL_PIN 200

#if (defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)) && !defined(BOOT_PIN)
#define BOOT_PIN HEARTH_BOOTSEL_PIN
#endif

/*
 * log_e(): the `esp32` core's `esp32-hal-log.h` gives every sketch this
 * printf-style error-logging macro (and its log_w/log_i/log_d/log_v
 * siblings), and `arduino-pico` has no equivalent at all. Found the same way
 * `Preferences` and `BOOT_PIN` were (see the README's "Compiling them
 * against arduino-pico" section): the devtype expansion's `arduino-cli`
 * compile pass over `examples/MatterThermostat` failed on
 * `'log_e' was not declared in this scope`, an unmodified upstream example
 * calling a macro this platform never had a reason to define before.
 *
 * Only log_e is reproduced, not the full log_w/log_i/log_d/log_v family:
 * a survey of the thirteen examples this task added found log_e as the only
 * one any of them calls (`MatterThermostat.ino`'s default case in its
 * mode-name switch). Upstream's own macro is far more elaborate
 * (component/task/line tagging, `ARDUHAL_LOG_COLORS`, a choice of three
 * backends selected by `CORE_DEBUG_LEVEL`); this reproduces only its
 * observable behaviour an example can depend on, printf-formatted output on
 * the console `Serial`, not its exact wire format. `Serial.begin()` is a
 * precondition, exactly as it is for every other console print an example
 * makes; a sketch that has not called it yet loses the message the same way
 * a plain Serial.printf() would.
 *
 * The Serial.println() call is gated on ARDUINO, matching Hearth.cpp's own
 * guard around its Serial.println() warnings: this header is unconditionally
 * pulled in by every host test binary through Hearth.h, and test/host's
 * ArduinoShim.h carries no Serial. Left unguarded, hearthLogE()'s body still
 * has to type-check even when never called (it is not a template), so every
 * one of test/host's binaries failed with "'Serial' was not declared in this
 * scope" before this guard existed. On host, log_e() formats into the buffer
 * and drops it: parity with the message never reaching a real Serial
 * console, not a missing feature to add here.
 */
#ifndef log_e
#include <Arduino.h>
#include <stdarg.h>
inline void hearthLogE(const char *format, ...) {
  char buf[160];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
#ifdef ARDUINO
  Serial.println(buf);
#else
  (void)buf;
#endif
}
#define log_e(format, ...) hearthLogE(format, ##__VA_ARGS__)
#endif

typedef enum {
  ESP_MATTER_VAL_TYPE_INVALID = 0,
  ESP_MATTER_VAL_TYPE_BOOLEAN,
  ESP_MATTER_VAL_TYPE_INTEGER,
  ESP_MATTER_VAL_TYPE_INT8,
  ESP_MATTER_VAL_TYPE_UINT8,
  ESP_MATTER_VAL_TYPE_INT16,
  ESP_MATTER_VAL_TYPE_UINT16,
  ESP_MATTER_VAL_TYPE_INT32,
  ESP_MATTER_VAL_TYPE_UINT32,
  ESP_MATTER_VAL_TYPE_ENUM8,
  ESP_MATTER_VAL_TYPE_BITMAP8,
  /* Energy round A: appended after BITMAP8, not slotted next to INT32/
   * UINT32 where upstream keeps them, so every pre-existing enumerator
   * keeps the numeric value it has always had. Nothing should depend on
   * those values, but "should" is not a reason to churn them. */
  ESP_MATTER_VAL_TYPE_INT64,
  ESP_MATTER_VAL_TYPE_UINT64,
} esp_matter_val_type_t;

/*
 * The .i8/.u8/.i16/.u16/.i32/.u32 members exist for parity with upstream's
 * own union, whose endpoint implementations (e.g. MatterColorTemperatureLight
 * ::attributeChangeCB) read val->val.u8, val->val.u16, val->val.i16 directly.
 * attributeChangeCB is a virtual a sketch may override with upstream's own
 * body, so without these members present a sketch carrying that body fails
 * to compile: the premise of this library is that an unmodified sketch
 * builds. Some upstream implementations read a wide member (val->val.u32)
 * unconditionally before ever branching on the attribute's real type (e.g.
 * MatterColorTemperatureLight.cpp's opening log_d call), so a narrow write
 * is not enough on its own.
 *
 * Invariant: every esp_matter_bool()/esp_matter_uint8()/.../hearthAttrValFromLong()
 * write below fills the full width of the union (through .i64 or .u64 since
 * energy round A added those members; it was .i/.u while 32 bits was the
 * widest there was, including the boolean case, which writes the wide
 * member rather than the single-byte .b), never a member narrower than
 * that. Given that invariant, and two's-complement plus little-endian byte
 * order (true of both the RP2350 target and every host this test suite
 * builds on), any narrower member -- .b, .u8, .u16, .i16, .u32, and so on --
 * reads back the correct value: its bytes are a prefix of the wide write's
 * own bytes. Do not "fix" a future write to target only the member matching
 * its type instead; that direction is strictly worse, since a narrow write
 * leaves every *wider* member's upper bytes indeterminate instead.
 * test/host/test_attrval.cpp asserts the invariant directly (including a
 * boolean write read back through .u16 and .u32, the case that broke before
 * .val.b was widened to a full-width write), and test_attr64.cpp
 * re-asserts it through the 64-bit members, rather than trusting this
 * comment alone.
 */
typedef struct {
  esp_matter_val_type_t type;
  union {
    bool b;
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    int32_t i;
    uint32_t u;
  } val;
} esp_matter_attr_val_t;

inline esp_matter_attr_val_t esp_matter_bool(bool v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
  a.val.u64 = v ? 1 : 0;  // full 64-bit width; see the union's header comment
  return a;
}

inline esp_matter_attr_val_t esp_matter_uint8(uint8_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_UINT8;
  a.val.u64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_uint16(uint16_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_UINT16;
  a.val.u64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_int16(int16_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_INT16;
  a.val.i64 = v;
  return a;
}

/*
 * The rest of esp_matter_val_type_t's constructors. None of the four
 * endpoint classes this library implements needs them: the census in
 * CLAUDE.md found only u8, u16, bool, i16 and u32 across all twenty of
 * upstream's classes. They exist because a sketch can call
 * setAttributeVal()/updateAttributeVal()/getAttributeVal() on any cluster
 * and attribute it likes, including ones no implemented class wraps, and
 * without a constructor for the type it would simply fail to compile. That
 * is the same "an unmodified sketch must build" argument that widened the
 * union above.
 *
 * Signatures are read from esp-matter's own
 * components/esp_matter/data_model/esp_matter_attribute_utils.h (v1.5.1),
 * not transcribed from memory: enum8 and bitmap8 both take a uint8_t there,
 * which is not guessable from their names.
 *
 * Every one writes the full 32-bit width through .i or .u, upholding the
 * write-widest/read-narrowest invariant the union's comment above states
 * and test_attrval.cpp asserts.
 */
inline esp_matter_attr_val_t esp_matter_int8(int8_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_INT8;
  a.val.i64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_int32(int32_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_INT32;
  a.val.i64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_uint32(uint32_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_UINT32;
  a.val.u64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_enum8(uint8_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_ENUM8;
  a.val.u64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_bitmap8(uint8_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_BITMAP8;
  a.val.u64 = v;
  return a;
}

/*
 * Energy round A: the 64-bit pair, for the electrical measurement clusters
 * whose power-mW and energy-mWh attributes are int64/uint64. Signatures
 * match esp-matter v1.5.1's esp_matter_attribute_utils.h, same as the
 * eight above.
 */
inline esp_matter_attr_val_t esp_matter_int64(int64_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_INT64;
  a.val.i64 = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_uint64(uint64_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_UINT64;
  a.val.u64 = v;
  return a;
}

/*
 * Whether a type renders unsigned on the wire. The firmware (0.8.0) emits
 * and accepts AT+MTATTR values signed per the attribute's own type: an
 * unsigned attribute carries 0 up to its type's maximum and a leading
 * minus on it is rejected with +MTERR:1, so the serializer must pick %llu
 * or %lld from this, not print everything signed. Boolean is grouped with
 * the unsigned types; it renders 0/1 identically either way.
 */
inline bool hearthAttrValTypeIsUnsigned(esp_matter_val_type_t t) {
  switch (t) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_UINT32:
    case ESP_MATTER_VAL_TYPE_UINT64:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_BITMAP8:
      return true;
    default:
      return false;
  }
}

/*
 * Parse one wire decimal into its 64-bit two's-complement bit pattern.
 * Sign-agnostic on purpose: a +MTATTR line's value field is signed per the
 * attribute's type, but the parser often runs before the type is known
 * (Hearth.cpp's URC dispatch looks the endpoint up by id first), and the
 * bit pattern is identical either way. A leading minus takes strtoll
 * (INT64_MIN parses exactly); anything else takes strtoull, so an
 * unsigned value above INT64_MAX (UINT64_MAX included) round-trips
 * instead of saturating. The int64_t cast of an out-of-range uint64_t is
 * two's-complement wrapping on every toolchain this library builds with
 * (gcc/clang, and C++20 requires it); the typed rebuild in
 * hearthAttrValFromLong puts the pattern back in the right member.
 */
inline int64_t hearthParseWireValue(const char *s, char **end) {
  if (*s == '-') {
    return (int64_t)strtoll(s, end, 10);
  }
  return (int64_t)strtoull(s, end, 10);
}

/*
 * Flatten to the single integer AT+MTATTR carries, now a full 64-bit one
 * (energy round A; it was a long, which is 32 bits on the RP2350 target
 * and silently truncated the electrical measurement range). Signed types
 * read val.i / val.i64, unsigned and enum/bitmap types read val.u /
 * val.u64, boolean reads val.b; a naive codec that reads one union member
 * for everything gets the signed types wrong (see TemperatureMeasurement's
 * MeasuredValue, an int16 that goes negative). A UINT64 above INT64_MAX
 * flattens as its raw bit pattern; the type tag, via
 * hearthAttrValTypeIsUnsigned(), is what tells the serializer to render it
 * unsigned. Returns false for ESP_MATTER_VAL_TYPE_INVALID and for any type
 * not in the enum; that false is what later becomes the wire's +MTERR:5.
 */
inline bool hearthAttrValToLong(const esp_matter_attr_val_t &v, int64_t *out) {
  switch (v.type) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
      *out = v.val.b ? 1 : 0;
      return true;
    case ESP_MATTER_VAL_TYPE_INTEGER:
    case ESP_MATTER_VAL_TYPE_INT8:
    case ESP_MATTER_VAL_TYPE_INT16:
    case ESP_MATTER_VAL_TYPE_INT32:
      *out = v.val.i;
      return true;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_UINT32:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_BITMAP8:
      *out = v.val.u;
      return true;
    case ESP_MATTER_VAL_TYPE_INT64:
      *out = v.val.i64;
      return true;
    case ESP_MATTER_VAL_TYPE_UINT64:
      *out = (int64_t)v.val.u64;
      return true;
    case ESP_MATTER_VAL_TYPE_INVALID:
    default:
      return false;
  }
}

/*
 * Rebuild a typed value from an AT integer, given the target type. An
 * unknown t (ESP_MATTER_VAL_TYPE_INVALID, or a value outside the enum, which
 * a caller can get to via an unchecked cast) is not silently accepted as
 * whatever t happened to be: the result's .type is forced to
 * ESP_MATTER_VAL_TYPE_INVALID so a caller inspecting it afterwards can tell
 * the rebuild did not actually know the type, rather than trusting a value
 * that was only ever read out of the union's unsigned member by accident.
 */
inline esp_matter_attr_val_t hearthAttrValFromLong(esp_matter_val_type_t t, int64_t v) {
  esp_matter_attr_val_t a;
  a.type = t;
  switch (t) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
      a.val.u64 = (v != 0) ? 1 : 0;  // full 64-bit width; see the union's header comment
      break;
    case ESP_MATTER_VAL_TYPE_INTEGER:
    case ESP_MATTER_VAL_TYPE_INT8:
    case ESP_MATTER_VAL_TYPE_INT16:
    case ESP_MATTER_VAL_TYPE_INT32:
    case ESP_MATTER_VAL_TYPE_INT64:
      a.val.i64 = v;
      break;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_UINT32:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_BITMAP8:
    case ESP_MATTER_VAL_TYPE_UINT64:
      a.val.u64 = (uint64_t)v;
      break;
    case ESP_MATTER_VAL_TYPE_INVALID:
    default:
      a.type = ESP_MATTER_VAL_TYPE_INVALID;
      a.val.u64 = (uint64_t)v;
      break;
  }
  return a;
}

/*
 * ChipDeviceEvent is a stub: ArduinoMatter::matterEventCB takes a pointer to
 * it, and sketches pass it through without dereferencing. We populate the
 * bit and detail we actually have from +MTEVT.
 */
namespace chip {
namespace DeviceLayer {
struct ChipDeviceEvent {
  uint8_t bit;
  int detail;
};
}  // namespace DeviceLayer
}  // namespace chip
