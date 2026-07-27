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
 * one array, and AT+MTATTR carries integers and booleans only.
 */

#pragma once

#include <stdint.h>

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
} esp_matter_val_type_t;

/*
 * The .i8/.u8/.i16/.u16/.i32/.u32 members exist for parity with upstream's
 * own union, whose endpoint implementations (e.g. MatterColorTemperatureLight
 * ::attributeChangeCB) read val->val.u8, val->val.u16, val->val.i16 directly.
 * attributeChangeCB is a virtual a sketch may override with upstream's own
 * body, so without these members present a sketch carrying that body fails
 * to compile: the premise of this library is that an unmodified sketch
 * builds. This port's codec (hearthAttrValFromLong below) still only ever
 * writes .b, .i or .u; the narrower members are not separately written, they
 * alias the same bytes as .i/.u. That is safe here because every value this
 * codec carries fits within the width the caller's type claims (a UINT8
 * never exceeds 255, an INT16 never exceeds its range), and two's-complement
 * plus little-endian byte order (true of both the RP2350 target and every
 * host this test suite builds on) means the low bytes of a correctly-signed
 * 32-bit write already equal the narrower type's own bit pattern. See
 * test_attrval.cpp's union member coverage tests, which assert this directly
 * rather than assuming it.
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
    int32_t i;
    uint32_t u;
  } val;
} esp_matter_attr_val_t;

inline esp_matter_attr_val_t esp_matter_bool(bool v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
  a.val.b = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_uint8(uint8_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_UINT8;
  a.val.u = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_uint16(uint16_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_UINT16;
  a.val.u = v;
  return a;
}

inline esp_matter_attr_val_t esp_matter_int16(int16_t v) {
  esp_matter_attr_val_t a;
  a.type = ESP_MATTER_VAL_TYPE_INT16;
  a.val.i = v;
  return a;
}

/*
 * Flatten to the single integer AT+MTATTR carries. Signed types read val.i,
 * unsigned and enum/bitmap types read val.u, boolean reads val.b; a naive
 * codec that reads one union member for everything gets the signed types
 * wrong (see TemperatureMeasurement's MeasuredValue, an int16 that goes
 * negative). Returns false for ESP_MATTER_VAL_TYPE_INVALID and for any type
 * not in the enum; that false is what later becomes the wire's +MTERR:5.
 */
inline bool hearthAttrValToLong(const esp_matter_attr_val_t &v, long *out) {
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
inline esp_matter_attr_val_t hearthAttrValFromLong(esp_matter_val_type_t t, long v) {
  esp_matter_attr_val_t a;
  a.type = t;
  switch (t) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
      a.val.b = (v != 0);
      break;
    case ESP_MATTER_VAL_TYPE_INTEGER:
    case ESP_MATTER_VAL_TYPE_INT8:
    case ESP_MATTER_VAL_TYPE_INT16:
    case ESP_MATTER_VAL_TYPE_INT32:
      a.val.i = (int32_t)v;
      break;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_UINT32:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_BITMAP8:
      a.val.u = (uint32_t)v;
      break;
    case ESP_MATTER_VAL_TYPE_INVALID:
    default:
      a.type = ESP_MATTER_VAL_TYPE_INVALID;
      a.val.u = (uint32_t)v;
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
