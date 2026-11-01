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

The sensors follow the datasheet form `F = K × Q + offset`, where `F` is the
pulse frequency in Hz and `Q` the flow in L/min — so `Q = (F − offset) / K`.

| `connection_size` | K | offset | Pulses per litre |
|---|---|---|---|
| `3/4` | 6.6 | 0 | 396 |
| `1` | 8.0 | −4 | ~476 |

The exact constant is printed on the sensor body and differs between YF-B
variants, so both numbers can be overridden per device without reflashing:

| Key | Default | Meaning |
|---|---|---|
| `flow_k_factor` | `0` | `K`. `0` means "use the default for `connection_size`". |
| `flow_k_offset` | `0` | The constant term, in Hz. Only read when `flow_k_factor > 0`. |

### Flow input noise

The flow input is counted by a GPIO interrupt, so anything that makes the line
oscillate is counted as water. A signal edge that crosses the ESP32's input
threshold slowly will do exactly that: bursts of ~730 kHz have been observed in
the field, against ~130 Hz for a real 20 L/min — the count then measures how
fast the ESP32 can service interrupts, not how much water is flowing.

Two settings bound the damage:

| Key | Default | Meaning |
|---|---|---|
| `flow_debounce_us` | `500` | Edges closer together than this are ignored. 500 µs allows up to ~133 L/min on a 3/4" connection. |
| `max_flow_lpm` | `150` | A sample above this is discarded and the previous reading kept. |

`flow_glitches` (edges rejected) and `flow_rejected` (whole samples discarded)
are published in the diagnostics, so a noisy install is visible rather than
silently wrong.

If those counters keep climbing, the cause is electrical, not firmware:

- The pin is configured as plain `INPUT`. **Never `INPUT_PULLDOWN`** — the YF-B
  output is push-pull and usually reaches the ESP32 through a 5 V → 3.3 V level
  converter that already pulls the line up through ~10 kΩ. The ESP32's internal
  ~45 kΩ pull-down divides that to ~2.7 V against a 2.475 V input threshold,
  leaving ~0.2 V of margin — close enough to oscillate.
- An RC low-pass at the pin (1 kΩ in series, 100 nF to GND) cuts off around
  1.6 kHz: it passes the sensor's full scale comfortably and kills the ringing.
  Unlike the software debounce, it also stops the interrupts from reaching the
  CPU at all.
- Use shielded cable for the sensor, routed away from pump and mains wiring.

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
