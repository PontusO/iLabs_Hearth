# Hardware bring-up checklist

Short, factual notes for the first session against a real C6 running the
Hearth firmware. Each item names what to check and what a mismatch would
mean, so a wrong assumption is caught here rather than chased as a bug in
the sketch.

## Mode 0 is expected to echo a `+MTATTR` URC

A write through `setAttributeVal()` (`AT+MTATTR=...,0`) should be followed
by a `+MTATTR:` line and then `OK`, the same shape as a mode-1 write through
`updateAttributeVal()`. This was re-verified directly against the firmware
and the SDK: `main.cpp`'s mode-0 path calls
`esp_matter::attribute::set_val()`, whose `call_callbacks` parameter
defaults to `true`, so `set_val_internal` still fires `POST_UPDATE`, which
is what raises the URC. The two modes differ only in whether the change is
reported to the fabric, not in whether the host sees an echo.

**If a mode-0 write on the bench answers with a bare `OK` and no
`+MTATTR:` line, the library's model of the firmware is wrong again** and
`test_mode_zero_write_also_echoes` (`test/host/test_onofflight.cpp`) and
the mode-0 leg of `test_write_modes` (`test/host/test_endpoint.cpp`) need
to be reverted, along with the README and `HearthLink.cpp` text this
correction pass touched.

## `Serial1` at 115200 is an unverified assumption

The library's default link (used when a sketch never calls
`Hearth.begin()`) is `Serial1` at 115200 baud. This is what the Challenger's
host-to-co-processor UART is believed to be wired to; it has not been
confirmed against real hardware before this bring-up. If nothing arrives,
or garbage arrives, confirm the pin mapping and baud before suspecting the
protocol layer, and pass `Hearth.begin()` an explicit, confirmed `Serial`
rather than relying on the default.

## A mode-0 write of an unchanged value is expected to fail today

Writing a value equal to the attribute's current value through mode 0 is
expected to come back `ERROR` on the wire, not `OK`. The esp-matter SDK's
`set_val()` returns early when the value is unchanged, and the firmware
maps that early return onto `ERROR` rather than treating it as a no-op
success. This library's own setters (`setOnOff()` and siblings) already
guard against sending an unchanged value, so it will not surface through
normal use of those; it can still surface through a direct
`setAttributeVal()` call, or if the host's cached state has drifted from
the device's actual state. This is a firmware behaviour, not a library bug,
and is recorded here rather than worked around silently.
