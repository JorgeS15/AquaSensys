# AquaSensys
Intelligent Water Supply Monitor and Controller Module

## Configuration (`config.json` on the SD card)

Two keys describe the unit itself. Both are optional — if a key is missing or
unreadable, the default below is used.

| Key | Values | Default | Meaning |
|---|---|---|---|
| `connection_size` | `"3/4"`, `"1"` | `"3/4"` | Water connection size, in inches. Selects the flow sensor K-factor. |
| `device_type` | `"M"`, `"C"` | `"C"` | `M` = Monitor, `C` = Controller. |

Both can also be set from the web UI, under **Settings → Device Settings**.

### Flow rate

Both supported sensors follow the datasheet form `F = K × Q`, where `F` is the
pulse frequency in Hz and `Q` the flow in L/min — so `Q = (pulses per second) / K`.

| `connection_size` | K | Pulses per litre |
|---|---|---|
| `3/4` | 6.6 | 396 |
| `1` | 8.0 | 480 |

### Device type

A **Controller** (`"C"`) is the full module: it reads the sensors and drives the
pump motor from the pressure thresholds, the main switch and the manual override.

A **Monitor** (`"M"`) only measures. All motor control logic is disabled:

- The motor output stays de-energised; `controlMotor()` makes no decision.
- No dry-run watchdog, no overcurrent or phase-imbalance trip, no runtime counter.
- `/command` returns HTTP 409, and the dashboard hides the motor controls.
- MQTT publishes sensors only — no motor, main-power or override switches. The
  error state is still reported, as a read-only binary sensor.
- The status LED shows only healthy (green heartbeat) or error (red blink).

Changing `device_type` reboots the device, since it changes the MQTT entities
that are advertised.
