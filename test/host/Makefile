# Host unit tests for the transport and logic layers of iLabs Hearth. No
# Arduino core and no hardware: ArduinoShim.h stands in for Arduino.h.
# Run with `make -C test/host run`.

CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -g -I. -I../../src
SRC = ../../src/HearthLink.cpp
TESTS = test_hearthlink test_attrval test_endpoint test_hearth test_reconcile test_onofflight test_dimmable test_colortemp_sensor test_matter_umbrella

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

test_colortemp_sensor: test_colortemp_sensor.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Regression test for the Task 9 fix round: a translation unit whose only
# iLabs Hearth include is "Matter.h", exactly what an unmodified upstream
# sketch includes, must be enough to declare all four implemented device
# types at file scope. Needs every endpoint .cpp linked in, since it
# exercises all four.
test_matter_umbrella: test_matter_umbrella.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffLight.cpp ../../src/MatterEndpoints/MatterDimmableLight.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
