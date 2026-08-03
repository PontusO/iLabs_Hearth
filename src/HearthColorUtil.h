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
 * HsvColor_t/espHsvColor_t; XyColor_t is NOT ported, nothing in
 * MatterEnhancedColorLight's API or any shipped example touches xy
 * chromaticity) and the three conversion functions actually called: the
 * class's own espHsvColorToRgbColor() (from getColorRGB()) and
 * espRgbColorToHsvColor() (from setColorRGB()), plus espCTToRgbColor() (and
 * the espCtColor_t struct and espCTColorToRgbColor() it delegates to),
 * needed not by the class itself but by the upstream
 * `examples/MatterEnhancedColorLight` sketch's own temperature simulation
 * (`espRgbColorToHsvColor(espCTToRgbColor(colorTemperature))`), found the
 * same way `Preferences` and `BOOT_PIN` were: the devtype expansion's
 * `arduino-cli` compile pass over that example failed on
 * `'espCTToRgbColor' was not declared in this scope`. The scalar-argument
 * overloads (espHsvToRgbColor()/espRgbToHsvColor()), espXYToRgbColor() and
 * its xy-space siblings, and every named color/temperature constant
 * (HSV_BLACK, RGB_WHITE, WARM_WHITE_COLOR_TEMPERATURE, ...) are still not
 * ported: neither the class nor any of the sixteen shipped examples calls
 * them.
 *
 * Names and signatures match upstream exactly, including HsvColor_t's h
 * field being a uint16_t despite CurrentHue being a wire uint8: that is
 * upstream's own struct, transcribed as-is, not narrowed. The function
 * bodies are transcribed as-is too, so the arithmetic (and any of its
 * edge-case behaviour, such as a case/default fallthrough for region 5, an
 * assigning a possibly-negative int expression into hsv.h, or
 * espCTColorToRgbColor()'s Tanner Helland approximation with its own
 * documented seams at centiKelvin 66 and 19) matches upstream bit for bit;
 * this is a port, not a rewrite.
 *
 * Kept as inline functions in this header, matching HearthCompat.h's own
 * esp_matter_bool()/esp_matter_uint8()/etc. pattern, so no new .cpp (and no
 * new test/host/Makefile compilation unit) is needed for it. <math.h>'s
 * pow()/log() (espCTColorToRgbColor() only) are the one new dependency this
 * adds over the two original conversions.
 */

#pragma once

#include <math.h>
#include <stdint.h>

/* Substitute for std::clamp, available from C++17 onwards; upstream's own
 * ColorFormat.c defines this same macro under this same name. */
#ifndef clamp
#define clamp(a, min, max) ((a) < (min) ? (min) : ((a) > (max) ? (max) : (a)))
#endif

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

struct CtColor_t {
  uint16_t ctMireds;
};

typedef struct RgbColor_t espRgbColor_t;
typedef struct HsvColor_t espHsvColor_t;
typedef struct CtColor_t espCtColor_t;

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

/* Transcribed as-is from upstream's ColorFormat.c; see this file's own
 * header comment for why it is here (an example, not the class, needs it)
 * and for the Tanner Helland credit upstream itself carries. */
inline espRgbColor_t espCTColorToRgbColor(espCtColor_t ct) {
  espRgbColor_t rgb = { 0, 0, 0 };
  float r, g, b;

  if (ct.ctMireds == 0) {
    return rgb;
  }
  /* Algorithm credits to Tanner Helland:
   * https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html */

  /* Convert Mireds to centiKelvins. k = 1,000,000/mired */
  float ctCentiKelvin = 10000 / ct.ctMireds;

  /* Red */
  if (ctCentiKelvin <= 66) {
    r = 255;
  } else {
    r = 329.698727446f * pow(ctCentiKelvin - 60, -0.1332047592f);
  }

  /* Green */
  if (ctCentiKelvin <= 66) {
    g = 99.4708025861f * log(ctCentiKelvin) - 161.1195681661f;
  } else {
    g = 288.1221695283f * pow(ctCentiKelvin - 60, -0.0755148492f);
  }

  /* Blue */
  if (ctCentiKelvin >= 66) {
    b = 255;
  } else {
    if (ctCentiKelvin <= 19) {
      b = 0;
    } else {
      b = 138.5177312231 * log(ctCentiKelvin - 10) - 305.0447927307;
    }
  }
  rgb.r = (uint8_t)clamp(r, 0, 255);
  rgb.g = (uint8_t)clamp(g, 0, 255);
  rgb.b = (uint8_t)clamp(b, 0, 255);

  return rgb;
}

inline espRgbColor_t espCTToRgbColor(uint16_t ct) {
  espCtColor_t ctColor = { ct };
  return espCTColorToRgbColor(ctColor);
}
