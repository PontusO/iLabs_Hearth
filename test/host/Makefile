# Host unit tests for the transport and logic layers of iLabs Hearth. No
# Arduino core and no hardware: ArduinoShim.h stands in for Arduino.h.
# Run with `make -C test/host run`.

CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -g -I. -I../../src
SRC = ../../src/HearthLink.cpp
TESTS = test_hearthlink test_attrval test_endpoint test_hearth test_reconcile test_onofflight test_dimmable test_onoffplugin test_dimmableplugin test_colortemp_sensor test_matter_umbrella test_no_global_instances test_preferences test_transport

all: $(TESTS)

test_hearthlink: test_hearthlink.cpp ArduinoShim.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_attrval: test_attrval.cpp ArduinoShim.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_endpoint: test_endpoint.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Hearth.cpp now dispatches +MTATTR/+MTIDENT URCs and reconciles endpoint
# composition through the MatterEndPoint registry, so every binary that links
# Hearth.cpp needs MatterEndPoint.cpp too, not just the ones whose tests call
# it directly.
test_hearth: test_hearth.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_reconcile: test_reconcile.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_onofflight: test_onofflight.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffLight.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_dimmable: test_dimmable.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterDimmableLight.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_onoffplugin: test_onoffplugin.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffPlugin.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_dimmableplugin: test_dimmableplugin.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterDimmablePlugin.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_colortemp_sensor: test_colortemp_sensor.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Regression test for the Task 9 fix round: a translation unit whose only
# iLabs Hearth include is "Matter.h", exactly what an unmodified upstream
# sketch includes, must be enough to declare all four implemented device
# types at file scope. Needs every endpoint .cpp linked in, since it
# exercises all four.
test_matter_umbrella: test_matter_umbrella.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffLight.cpp ../../src/MatterEndpoints/MatterDimmableLight.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Regression test for the final-review MINOR 1 fix: the Matter and Hearth
# globals must honour NO_GLOBAL_INSTANCES / NO_GLOBAL_MATTER /
# NO_GLOBAL_HEARTH. Mostly a compile-time assertion (see the file's own
# comment), so a failure here shows up as a build error, not a failed check.
#
# -DNO_GLOBAL_INSTANCES is on the command line, applying to *every* source in
# this recipe, not #define'd inside the test .cpp. That is the whole point of
# this target after the re-review: a sketch (or a board variant, or a
# platform.local.txt) that sets the macro sets it build-wide, so the library's
# own translation units are compiled with it too, and those are the ones that
# used not to build. Defining it in the test TU alone compiled the library
# normally and linked the result, which could never see the failure.
test_no_global_instances: test_no_global_instances.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffLight.cpp ../../src/MatterEndpoints/MatterDimmableLight.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp
	$(CXX) $(CXXFLAGS) -DNO_GLOBAL_INSTANCES -o $@ $^

# The Preferences shim. PrefsStoreFake.cpp replaces PreferencesEEPROM.cpp,
# which is the only part of the shim that needs flash; everything interesting
# (the record codec) is in Preferences.cpp and is plain logic. Note that
# PreferencesEEPROM.cpp is deliberately NOT in this recipe: linking both would
# define the store twice.
test_preferences: test_preferences.cpp ArduinoShim.cpp PrefsStoreFake.cpp ../../src/Preferences.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_transport: test_transport.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
