# Host unit tests for the transport and logic layers of iLabs Hearth. No
# Arduino core and no hardware: ArduinoShim.h stands in for Arduino.h.
# Run with `make -C test/host run`.

CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -g -I. -I../../src
SRC = ../../src/HearthLink.cpp
TESTS = test_hearthlink test_attrval test_endpoint test_hearth test_reconcile test_onofflight test_dimmable test_onoffplugin test_dimmableplugin test_colortemp_sensor test_boolsensors test_measuresensors test_matter_umbrella test_no_global_instances test_preferences test_transport test_fan test_windowcovering test_thermostat test_enhancedcolor test_genericswitch test_colorlight test_cabinet test_doorlock test_lightsensor test_flowsensor test_airquality test_mountedonoffcontrol test_mounteddimmableloadcontrol test_airpurifier test_extractorhood test_cooktop test_roomac test_pump test_watervalve test_modeselect test_chime test_smokecoalarm test_powersource test_laundrywasher test_dishwasher test_laundrydryer test_cmdfields test_roboticvacuum test_microwaveoven test_composition_parent

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

# C3 added MatterDoorLock reconcile-push tests (adopt and rebuild paths)
# here, so this recipe needs that endpoint .cpp linked in too, not just the
# generic TestEndPoint the rest of this file uses.
test_reconcile: test_reconcile.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterDoorLock.cpp
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

test_boolsensors: test_boolsensors.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterContactSensor.cpp ../../src/MatterEndpoints/MatterRainSensor.cpp ../../src/MatterEndpoints/MatterWaterFreezeDetector.cpp ../../src/MatterEndpoints/MatterWaterLeakDetector.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_measuresensors: test_measuresensors.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterHumiditySensor.cpp ../../src/MatterEndpoints/MatterPressureSensor.cpp ../../src/MatterEndpoints/MatterOccupancySensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Regression test for the Task 9 fix round: a translation unit whose only
# iLabs Hearth include is "Matter.h", exactly what an unmodified upstream
# sketch includes, must be enough to declare a device object at file scope.
# Needs every endpoint .cpp linked in: the original four are brought up and
# exercised over AT+MTATTR, the devtype expansion's other thirteen (final-
# review finding M1) plus Task S4's MatterGenericSwitch/MatterColorLight,
# Task C5's MatterTemperatureControlledCabinet, Task C3's MatterDoorLock
# (added at C4, per C3's own note that C4 owns this file), the ten-type
# swoop's own ten classes (Tasks C2-C4, wired in here at C5), and the
# seven-type batch's eight classes (Tasks C7-C8, wired in here at C9) are
# declared file-scope only, a compile/link guard against a dropped umbrella
# include. The OperationalState trio (MatterLaundryWasher, MatterDishwasher,
# MatterLaundryDryer) additionally needs MatterOperationalStateEndpoint.cpp,
# their shared implementation core, linked once.
test_matter_umbrella: test_matter_umbrella.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffLight.cpp ../../src/MatterEndpoints/MatterDimmableLight.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp ../../src/MatterEndpoints/MatterOnOffPlugin.cpp ../../src/MatterEndpoints/MatterDimmablePlugin.cpp ../../src/MatterEndpoints/MatterContactSensor.cpp ../../src/MatterEndpoints/MatterRainSensor.cpp ../../src/MatterEndpoints/MatterWaterFreezeDetector.cpp ../../src/MatterEndpoints/MatterWaterLeakDetector.cpp ../../src/MatterEndpoints/MatterHumiditySensor.cpp ../../src/MatterEndpoints/MatterPressureSensor.cpp ../../src/MatterEndpoints/MatterOccupancySensor.cpp ../../src/MatterEndpoints/MatterFan.cpp ../../src/MatterEndpoints/MatterWindowCovering.cpp ../../src/MatterEndpoints/MatterThermostat.cpp ../../src/MatterEndpoints/MatterEnhancedColorLight.cpp ../../src/MatterEndpoints/MatterGenericSwitch.cpp ../../src/MatterEndpoints/MatterColorLight.cpp ../../src/MatterEndpoints/MatterTemperatureControlledCabinet.cpp ../../src/MatterEndpoints/MatterDoorLock.cpp ../../src/MatterEndpoints/MatterLightSensor.cpp ../../src/MatterEndpoints/MatterFlowSensor.cpp ../../src/MatterEndpoints/MatterAirQualitySensor.cpp ../../src/MatterEndpoints/MatterMountedOnOffControl.cpp ../../src/MatterEndpoints/MatterMountedDimmableLoadControl.cpp ../../src/MatterEndpoints/MatterAirPurifier.cpp ../../src/MatterEndpoints/MatterExtractorHood.cpp ../../src/MatterEndpoints/MatterCooktop.cpp ../../src/MatterEndpoints/MatterRoomAirConditioner.cpp ../../src/MatterEndpoints/MatterPump.cpp ../../src/MatterEndpoints/MatterWaterValve.cpp ../../src/MatterEndpoints/MatterModeSelect.cpp ../../src/MatterEndpoints/MatterChime.cpp ../../src/MatterEndpoints/MatterSmokeCOAlarm.cpp ../../src/MatterEndpoints/MatterPowerSource.cpp ../../src/MatterEndpoints/MatterOperationalStateEndpoint.cpp ../../src/MatterEndpoints/MatterLaundryWasher.cpp ../../src/MatterEndpoints/MatterDishwasher.cpp ../../src/MatterEndpoints/MatterLaundryDryer.cpp ../../src/MatterEndpoints/MatterRoboticVacuum.cpp ../../src/MatterEndpoints/MatterMicrowaveOven.cpp
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
test_no_global_instances: test_no_global_instances.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOnOffLight.cpp ../../src/MatterEndpoints/MatterDimmableLight.cpp ../../src/MatterEndpoints/MatterColorTemperatureLight.cpp ../../src/MatterEndpoints/MatterTemperatureSensor.cpp ../../src/MatterEndpoints/MatterContactSensor.cpp ../../src/MatterEndpoints/MatterRainSensor.cpp ../../src/MatterEndpoints/MatterWaterFreezeDetector.cpp ../../src/MatterEndpoints/MatterWaterLeakDetector.cpp ../../src/MatterEndpoints/MatterHumiditySensor.cpp ../../src/MatterEndpoints/MatterPressureSensor.cpp ../../src/MatterEndpoints/MatterOccupancySensor.cpp
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

test_fan: test_fan.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterFan.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_windowcovering: test_windowcovering.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterWindowCovering.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_thermostat: test_thermostat.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterThermostat.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_enhancedcolor: test_enhancedcolor.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterEnhancedColorLight.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_genericswitch: test_genericswitch.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterGenericSwitch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_colorlight: test_colorlight.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterColorLight.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_cabinet: test_cabinet.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterTemperatureControlledCabinet.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_doorlock: test_doorlock.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterDoorLock.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task C2: three Hearth-original sensor classes, one test binary each per
# the brief's recipe minimum, rather than the combined test_measuresensors
# shape the older read-direction sensors used.
test_lightsensor: test_lightsensor.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterLightSensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_flowsensor: test_flowsensor.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterFlowSensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_airquality: test_airquality.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterAirQualitySensor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# C3: five actuator clones, one test binary each per the brief's recipe.
test_mountedonoffcontrol: test_mountedonoffcontrol.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterMountedOnOffControl.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_mounteddimmableloadcontrol: test_mounteddimmableloadcontrol.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterMountedDimmableLoadControl.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_airpurifier: test_airpurifier.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterAirPurifier.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_extractorhood: test_extractorhood.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterExtractorHood.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_cooktop: test_cooktop.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterCooktop.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# C4: the two classes with real surface design, an OnOff leg plus a second
# domain cluster each (Thermostat, PumpConfigurationAndControl).
test_roomac: test_roomac.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterRoomAirConditioner.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_pump: test_pump.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterPump.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task C7: the seven-type batch's first library task. Three classes with no
# arduino-esp32 counterpart (same shape as MatterDoorLock, C3), one test
# binary each per the brief's recipe.
test_watervalve: test_watervalve.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterWaterValve.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_modeselect: test_modeselect.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterModeSelect.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_chime: test_chime.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterChime.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task C8: the second library task of the seven-type batch. Two
# Hearth-original classes with no shared implementation
# (MatterSmokeCOAlarm, MatterPowerSource), plus the OperationalState trio,
# which DOES share one implementation core
# (MatterOperationalStateEndpoint.cpp) behind three thin per-devtype
# classes; every trio recipe below links that shared .cpp alongside its own
# thin subclass. test_laundrywasher additionally links MatterDishwasher.cpp,
# since its own test file carries the brief's cross-class shared-core proof
# (one endpoint of each device type, driven over the identical wire).
test_smokecoalarm: test_smokecoalarm.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterSmokeCOAlarm.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_powersource: test_powersource.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterPowerSource.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_laundrywasher: test_laundrywasher.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOperationalStateEndpoint.cpp ../../src/MatterEndpoints/MatterLaundryWasher.cpp ../../src/MatterEndpoints/MatterDishwasher.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_dishwasher: test_dishwasher.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOperationalStateEndpoint.cpp ../../src/MatterEndpoints/MatterDishwasher.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_laundrydryer: test_laundrydryer.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOperationalStateEndpoint.cpp ../../src/MatterEndpoints/MatterLaundryDryer.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task 6 (RVC + Microwave batch): the multi-field +MTCMD dispatch
# (HearthCmdFields), a probe-only test with no endpoint type of its own, so
# it needs Hearth.cpp/MatterEndPoint.cpp but no MatterEndpoints/*.cpp.
test_cmdfields: test_cmdfields.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task 7 (RVC + Microwave batch): MatterRoboticVacuum, the first consumer of
# Task 6's hearthOnForwardedCommandFields()/HearthCmdFields against a real
# endpoint type rather than a probe.
test_roboticvacuum: test_roboticvacuum.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterRoboticVacuum.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task 8 (RVC + Microwave batch, last library task): MatterMicrowaveOven.
# Subclasses MatterOperationalStateEndpoint (the same shared core the
# OperationalState trio uses), so its .cpp is linked too, the same pattern
# test_laundrywasher/test_dishwasher/test_laundrydryer establish.
test_microwaveoven: test_microwaveoven.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp ../../src/MatterEndpoints/MatterOperationalStateEndpoint.cpp ../../src/MatterEndpoints/MatterMicrowaveOven.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# Task 7 (composed-appliance round): parent-aware declaration and reconcile.
# Uses only the generic TestEndPoint, so no MatterEndpoints/*.cpp is linked,
# the same shape as test_cmdfields.
test_composition_parent: test_composition_parent.cpp ArduinoShim.cpp $(SRC) ../../src/Hearth.cpp ../../src/MatterEndPoint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
