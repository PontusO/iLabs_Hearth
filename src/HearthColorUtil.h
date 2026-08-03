/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 *
 * The notice block above is copied verbatim from the top of the upstream
 * file this header ports from (see below); it is not this library's own
 * copyright statement, and is kept as-is rather than folded into Hearth's
 * own file-header convention.
 */
/*
 * HearthColorUtil.h - the RGB<->HSV color types and conversions
 * MatterEnhancedColorLight needs.
 *
 * Ported from arduino-esp32's core (not the Matter library):
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/cores/esp32/ColorFormat.h
 * and ColorFormat.c (Copyright (c) 2021 Project CHIP Authors, Apache-2.0).
 * arduino-pico carries no equivalent: ColorFormat.{h,c} is part of the
 * ESP32 Arduino core, and MatterEnhancedColorLight.h/.cpp include and call
 * it directly (espRgbColor_t, espHsvColor_t, espRgbColorToHsvColor(),
 * espHsvColorToRgbColor()) with no forwarding through the Matter library
 * itself. Same shape as HearthCompat.h hosting the esp_matter_attr_val_t
 * family: reproduce only what the class this library ports actually calls.
 *
 * Ported: the two struct definitions (RgbColor_t/espRgbColor_t and
 * HsvColor_t/espHsvColor_t; XyColor_t and CtColor_t are NOT ported, nothing
 * in MatterEnhancedColorLight's API touches xy chromaticity or
 * color-temperature-to-RGB conversion) and the two conversion functions
 * MatterEnhancedColorLight.cpp actually calls: espHsvColorToRgbColor()
 * (from getColorRGB()) and espRgbColorToHsvColor() (from setColorRGB()).
 * The scalar-argument overloads (espHsvToRgbColor()/espRgbToHsvColor()) and
 * every XY/CT function and constant (espXYToRgbColor, espCTToRgbColor,
 * HSV_BLACK, RGB_WHITE, WARM_WHITE_COLOR_TEMPERATURE, ...) are not ported:
 * the class never calls them.
 *
 * Names and signatures match upstream exactly, including HsvColor_t's h
 * field being a uint16_t despite CurrentHue being a wire uint8: that is
 * upstream's own struct, transcribed as-is, not narrowed. The function
 * bodies are transcribed as-is too, so the arithmetic (and any of its
 * edge-case behaviour, such as a case/default fallthrough for region 5, or
 * assigning a possibly-negative int expression into hsv.h) matches
 * upstream bit for bit; this is a port, not a rewrite.
 *
 * Kept as inline functions in this header, matching HearthCompat.h's own
 * esp_matter_bool()/esp_matter_uint8()/etc. pattern, so no new .cpp (and no
 * new test/host/Makefile compilation unit) is needed for it.
 */

#pragma once

#include <stdint.h>

struct RgbColor_t {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct HsvColor_t {
  uint16_t h;
  uint8_t s;
  uint8_t v;
};

typedef struct RgbColor_t espRgbColor_t;
typedef struct HsvColor_t espHsvColor_t;

inline espRgbColor_t espHsvColorToRgbColor(espHsvColor_t hsv) {
  espRgbColor_t rgb;

  uint8_t region, p, q, t;
  uint32_t h, s, v, remainder;

  if (hsv.s == 0) {
    rgb.r = rgb.g = rgb.b = hsv.v;
  } else {
    h = hsv.h;
    s = hsv.s;
    v = hsv.v;

    region = h / 43;
    remainder = (h - (region * 43)) * 6;
    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
      case 0:  rgb.r = v, rgb.g = t, rgb.b = p; break;
      case 1:  rgb.r = q, rgb.g = v, rgb.b = p; break;
      case 2:  rgb.r = p, rgb.g = v, rgb.b = t; break;
      case 3:  rgb.r = p, rgb.g = q, rgb.b = v; break;
      case 4:  rgb.r = t, rgb.g = p, rgb.b = v; break;
      case 5:
      default: rgb.r = v, rgb.g = p, rgb.b = q; break;
    }
  }
  return rgb;
}

inline espHsvColor_t espRgbColorToHsvColor(espRgbColor_t rgb) {
  espHsvColor_t hsv;
  uint8_t rgbMin, rgbMax;

  rgbMin = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b) : (rgb.g < rgb.b ? rgb.g : rgb.b);
  rgbMax = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) : (rgb.g > rgb.b ? rgb.g : rgb.b);

  hsv.v = rgbMax;
  if (hsv.v == 0) {
    hsv.h = 0;
    hsv.s = 0;
    return hsv;
  }

  hsv.s = 255 * (rgbMax - rgbMin) / hsv.v;
  if (hsv.s == 0) {
    hsv.h = 0;
    return hsv;
  }
  if (rgbMax == rgb.r) {
    hsv.h = 0 + 43 * (rgb.g - rgb.b) / (rgbMax - rgbMin);
  } else if (rgbMax == rgb.g) {
    hsv.h = 85 + 43 * (rgb.b - rgb.r) / (rgbMax - rgbMin);
  } else {
    hsv.h = 171 + 43 * (rgb.r - rgb.g) / (rgbMax - rgbMin);
  }
  return hsv;
}
