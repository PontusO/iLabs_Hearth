/*
 * Arduino.h - redirects the library's `#include <Arduino.h>` to the host
 * test stand-in. HearthLink.h includes <Arduino.h> unconditionally, as it
 * must on target (arduino-pico provides the real one); test/host's -I.
 * puts this file first on the search path so the host build sees the shim
 * instead.
 */
#pragma once
#include "ArduinoShim.h"
