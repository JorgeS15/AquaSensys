# AquaSensys
Intelligent Water Supply Monitor and Controller Module

See [CHANGELOG.md](CHANGELOG.md) for what changed in each firmware version.

## Configuration (`config.json` on the SD card)

Every setting lives in `config.json` on the SD card, and every key is optional:
a missing or unreadable key falls back to the default listed here. Most can also
be changed from the web UI, under **Settings**.

### The unit itself

| Key | Values | Default | Meaning |
|---|---|---|---|
| `device_type` | `"M"`, `"C"` | `"C"` | `M` = Monitor, `C` = Controller. |
| `connection_size` | `"3/4"`, `"1"` | `"3/4"` | Water connection size, in inches. Selects the flow sensor K-factor. |
| `device_id` | `[a-z0-9-]`, ≤ 32 chars | by type | Names the MQTT topics, the Home Assistant entities and the mDNS host. **Must differ on every unit.** |
| `device_name` | text, ≤ 48 chars | by type | Display name in Home Assistant and on the dashboard. |

### Device identity

Every unit runs the same firmware binary, so the identity comes from
`config.json` rather than from the build. Left blank, `device_id` and
`device_name` default by device type:

| `device_type` | `device_id` | `device_name` | Model | Dashboard header |
|---|---|---|---|---|
| `C` | `aquasensys` | AquaSensys C3 | AquaSensys C3 | AquaSensys C3 |
| `M` | `aquasensys-m` | AquaSensys M | AquaSensys Monitor | AquaSensys M |

So one Controller and one Monitor need no configuration. **Two units of the
same type must each be given their own `device_id`**: with a shared id they
publish into the same Home Assistant entities, and a Monitor will remove the
motor switches belonging to the Controller it is impersonating. A `device_name`
you set replaces the dashboard header too.

The MQTT client id is `<device_id>-<last 3 MAC bytes>`, so two boards never
knock each other off the broker, even if misconfigured with the same id.
Changing `device_id` creates a new device in Home Assistant; changing either
key reboots the unit, since topics and the mDNS name are built at startup.

### Sensor calibration

Offsets are added to the converted reading and are stored in `config.json`.

| Key | Unit | Default | Meaning |
|---|---|---|---|
| `pressure_offset` | bar | `0` | Added to both pressure sensors. |
| `pressure_in_offset`, `pressure_out_offset` | bar | `0` | Per-sensor zero, written by **Calibrate pressure** on the Diagnostics page (run it with the system depressurised). |
| `temp_offset` | °C | `0` | Added to the water temperature. Set under **Settings → System Parameters**. |
| `current_offset_l1` … `_l3` | mA | `0` | Per-phase zero, written by **Calibrate current** on the Diagnostics page (run it with the pump stopped). |

`temp_offset` only takes effect from v3.4.10; before that the setting existed
but was never applied. Units that still carried the old unused default of 0.16
have it reset to 0 the first time they boot v3.4.10, so their readings don't
shift. Any other value is kept.

Current is measured as the RMS over two whole mains cycles, around the measured
mean, so it is accurate without calibration; the residual noise (~40 mA) sits
below the 0.1 A display floor. Offsets saved before v3.4.6 were measured the old
way and are discarded once (`current_cal_method` records which method produced
the saved offsets). A Controller then recalibrates at boot, with its motor
output held off. A Monitor never calibrates at boot, because it doesn't drive the pump and can't know it is
stopped.

### Pressure sensors and filter alarm

Sensor 1 (inlet) sits on the mains, before the filter, and is the one the
pump control uses. An optional sensor 2 (outlet) sits after the filter; with
both fitted, the difference between them is the pressure drop across the
filter, which rises as it clogs. Set under **Settings → Pressure Sensors**.

| Key | Default | Meaning |
|---|---|---|
| `dual_pressure_sensors` | `false` | `true` when sensor 2 is fitted after the filter. |
| `filter_alarm_enabled` | `true` | Flag the filter as clogged when the drop gets too high. Only used with two sensors. |
| `filter_dp_warning` | `0.5` | Drop across the filter, in bar, that counts as clogged. |
| `filter_min_flow_lpm` | `5` | The drop is only judged above this flow, in L/min. |
| `filter_alarm_delay_s` | `30` | The drop must stay above the limit this many seconds before the alarm fires, and below it as long before it clears. |

With no water moving the filter shows no drop, clogged or not, so the alarm is
judged on the flow rather than on the pump state — which also makes it work on
Monitor units. Below the minimum flow the drop isn't judged: a filter already
marked clogged stays clogged, otherwise it reads "no flow". The delay keeps a
short pressure spike, such as the pump starting, from raising the alarm. The
alarm is a warning only: it never stops the pump.

With two sensors MQTT publishes `pressure_out`, `filter_dp` and, when the alarm
is on, `filter_clogged` (a "problem" binary sensor in Home Assistant). These
entities appear or disappear the next time the unit connects to the broker.

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

## Device type

A **Controller** (`"C"`) is the full module: it reads the sensors and drives the
pump motor from the pressure thresholds, the main switch and the manual override.

A **Monitor** (`"M"`) only measures. All motor control logic is disabled:

- The motor output stays de-energised; `controlMotor()` makes no decision.
- No dry-run watchdog, no overcurrent or phase-imbalance trip, no runtime counter.
- `/command` returns HTTP 409, and the dashboard hides the motor controls.
- MQTT publishes sensors only — no motor, main-power or override switches. The
  error state is still reported, as a read-only binary sensor, and follows the
  live reading instead of latching.
- The status LED shows only healthy (green heartbeat) or error (red blink).

Changing `device_type` reboots the device, since it changes the MQTT entities
that are advertised.

## Pump protection (Controller)

In automatic mode the pump starts at `min_pressure` and stops at `max_pressure`
(defaults 2.5 and 3.5 bar). These faults stop it and latch the error:

| Fault | Trips when | Active in manual mode |
|---|---|---|
| Pressure out of range | below 1 bar or above 5 bar | no |
| Dry run | motor on with no flow for 10 s | no |
| Overcurrent | total current above `max_current` (default 15 A) | **yes** |
| Phase imbalance | phases differ by more than `max_phase_imbalance` (default 3 A) | **yes** |

Manual mode overrides the pressure logic, not the motor's own protection:
low pressure and no flow are expected while an operator primes or refills an
empty system, but an electrical fault stops the motor whoever is commanding it.

A latched fault is cleared by turning the main switch back on, or by switching
the error off from Home Assistant. If the cause is still there, it trips again.

On boot the motor output is held off from reset, and no fault is judged and no
control decision is made until the sensors have produced a first real reading.
A WiFi outage does not slow the control loop: reconnection runs in the
background, one short step per loop pass.

## Home Assistant (MQTT)

Each unit publishes its entities through MQTT discovery under its `device_id`,
with its readings on `homeassistant/<id>/state`. It connects with a last will on
`homeassistant/<id>/availability`: the broker publishes `offline` there once the
unit stops answering (within ~25 s), and every entity points at that topic, so Home
Assistant shows a dead unit as unavailable rather than frozen on its last state.

Flow is published as a volume flow rate in L/min with long-term statistics,
so it can be chosen as the **Water flow rate** of a water source in the Energy
dashboard. Pressure, temperature and the filter pressure drop also keep
long-term statistics. With two pressure sensors, they appear as **Pressure In**
(before the filter) and **Pressure Out** (after it); with one, as **Pressure**.

Each unit also has a **Firmware** update entity (state on
`homeassistant/<id>/update/state`), which appears under **Settings → Updates**
when a newer release is out; **Install** there runs the update described under
[Updating a unit](#updating-a-unit). The **Check for Firmware Update** button
checks GitHub straight away.

## Updating a unit

There are two halves: the **firmware**, in the ESP32's flash, and the **web UI**,
served from the SD card. A release carries both (`aquasensys-firmware-*.bin` and
`sd-assets.zip`).

### Automatic updates (v3.6.0 and later)

The unit asks GitHub for the latest release two minutes after it connects to
WiFi, and then every `update_check_hours`:

| Key | Default | Meaning |
|---|---|---|
| `update_check_hours` | `12` | Hours between checks, `1`–`720`. `0` checks only when asked. |

When a newer release is out, the dashboard shows **"Firmware vX is available"**,
and Home Assistant lists the unit under **Settings → Updates**. Nothing is
installed until someone presses **Install** in either place. The unit then:

1. downloads `sd-assets.zip` to the SD card, checking its size and SHA-256;
2. downloads the firmware into the spare OTA slot, checked against the
   release's SHA-256 — the running firmware is untouched until the restart;
3. replaces the web UI from the archive (staged and CRC-checked, as below);
4. restarts — a Controller's pump stops for the restart and then resumes on
   its own. The dashboard reloads itself on the new version.

A failure in step 1 or 2 is reported on the dashboard and leaves the unit
exactly as it was. Should step 3 fail, the new firmware still starts with the
previous web UI; upload `sd-assets.zip` through `/upload` to finish.

**Check for updates** under the firmware version on the dashboard, or the
**Check for Firmware Update** button in Home Assistant, checks straight away.
The checks use GitHub's public API without a key, which allows 60 requests an
hour per internet connection — far more than any number of units will make.

`GET /api/update` returns the update state; `POST /api/update/check` and
`POST /api/update/install` do what they say.

### Flash layout and the one-time USB flash

From v3.6.0 the firmware is built for the `min_spiffs` partition scheme: two
1.875 MB app slots, where `default` had 1.25 MB. HTTPS (and the root
certificates it needs) took the firmware to 1.40 MB, past the old slots. The
128 KB SPIFFS partition that remains is unused: everything lives on the SD card.

A partition table can only be written over USB, so **a unit on v3.5.0 or older
must be flashed once over USB** with `aquasensys-full-*.bin`. The release notes
explain how, from a browser or with esptool. `config.json`, the web UI and the
logs are on the SD card and survive it. Uploading v3.6.0 or later through the
old update page is refused ("Not enough space") and the unit keeps running the
version it had.

### Manual updates

Still available, for a unit without internet access:

1. **Firmware** — flash `aquasensys-firmware-*.bin` from the release through the
   device's firmware update page (OTA).
2. **Web UI** — upload `sd-assets.zip` from the same release through the
   device's `/upload` page. Your `config.json` is never overwritten.

Uploads are staged: each file is written to `<name>.part` and only replaces the
live one once it has arrived intact, and a ZIP is replaced as a whole only after
every file in it has passed its CRC check. A failed or interrupted upload
reports an error and leaves the existing web UI exactly as it was.
