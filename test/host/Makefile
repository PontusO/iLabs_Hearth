# Host unit tests for the transport and logic layers of iLabs Hearth. No
# Arduino core and no hardware: ArduinoShim.h stands in for Arduino.h.
# Run with `make -C test/host run`.

CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -g -I. -I../../src
SRC = ../../src/HearthLink.cpp
TESTS = test_hearthlink test_attrval test_endpoint test_hearth test_reconcile

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

run: all
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
