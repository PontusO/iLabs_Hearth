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

typedef struct {
  esp_matter_val_type_t type;
  union {
    bool b;
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

/* Rebuild a typed value from an AT integer, given the target type. */
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
    default:
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
