/*
 * test_gpio_reset.cpp - U1: with HEARTH_HAS_GPIO defined and no Arduino
 * core (the iLabs_Hearth_C build), hearthResetCoprocessor() drives the
 * lines stored by coprocessorPins() in the Arduino build's order: the strap
 * released first (B670), then the reset pulse. With no stored pins it
 * drives nothing. Built with -DHEARTH_HAS_GPIO; see the Makefile.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static bool logIs(const std::vector<ShimGpioEvent> &want) {
  if (g_gpioLog.size() != want.size()) {
    return false;
  }
  for (size_t i = 0; i < want.size(); i++) {
    if (g_gpioLog[i].kind != want[i].kind || g_gpioLog[i].pin != want[i].pin || g_gpioLog[i].value != want[i].value) {
      return false;
    }
  }
  return true;
}

/* No +MTREADY is scripted, so waitReady() runs to its timeout; yield()
 * advancing the fake clock is what lets it end. */
static void resetWith(int reset, int strap, bool resetActiveLow, bool strapActiveLow) {
  MockStream s;
  Hearth.coprocessorPins(reset, strap, resetActiveLow, strapActiveLow);
  Hearth.begin(s);
  g_gpioLog.clear();
  g_yieldAdvanceMs = 50;
  Hearth.hearthResetCoprocessor();
  g_yieldAdvanceMs = 0;
}

static void test_active_low_lines_in_order(void) {
  resetWith(15, 14, true, true);
  check("strap released high, then reset pulsed low and released high",
        logIs({{'m', 14, OUTPUT}, {'w', 14, HIGH}, {'m', 15, OUTPUT}, {'w', 15, LOW}, {'w', 15, HIGH}}));
}

static void test_active_high_lines_inverted(void) {
  resetWith(7, 6, false, false);
  check("active-high lines: strap released low, reset pulsed high then released low",
        logIs({{'m', 6, OUTPUT}, {'w', 6, LOW}, {'m', 7, OUTPUT}, {'w', 7, HIGH}, {'w', 7, LOW}}));
}

static void test_reset_without_strap(void) {
  resetWith(15, -1, true, true);
  check("no strap line: only the reset pulse", logIs({{'m', 15, OUTPUT}, {'w', 15, LOW}, {'w', 15, HIGH}}));
}

static void test_no_pins_drives_nothing(void) {
  resetWith(-1, -1, true, true);
  check("no stored pins: no GPIO activity", g_gpioLog.empty());
}

int main() {
  printf("test_gpio_reset\n");
  test_active_low_lines_in_order();
  test_active_high_lines_inverted();
  test_reset_without_strap();
  test_no_pins_drives_nothing();
  printf("===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
