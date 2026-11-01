# Changelog

Firmware versions, newest first. Dates are when the version reached `main`,
which is when CI builds and publishes it. Small changes bump only the last
digit. Versions up to 3.0.38 are recorded in the git history only.

Each release has two parts: the firmware (flashed OTA) and the web UI on the SD
card (`sd-assets.zip`, uploaded through `/upload`). Entries marked **[web UI]**
only reach a device once the new `sd-assets.zip` is uploaded.

## 3.4.10 — 2026-11-01
- The **Water Temperature Offset** setting (`temp_offset`, °C) is now applied to
  the water temperature, alongside the pressure offsets in `config.json`.
  Default 0. Until now the setting existed but was never applied.
- Units carrying the old unused default of 0.16 have it reset to 0 on first boot,
  so their readings don't shift; any other value is kept.
- **[web UI]** The setting is labelled "Water Temperature Offset (°C)".

## 3.4.9 — 2026-11-01
- The MQTT task starts only after its client id, topics and server are set.
- Removed the deprecated, unused ESP32-ADC water-temperature reader.

## 3.4.8 — 2026-11-01
- Settings requests are assembled in full before being parsed, and never read
  past the end of the buffer. Bodies over 2048 bytes are refused (HTTP 413).

## 3.4.7 — 2026-11-01
- Uploads are staged as `<name>.part` and replace the live file only once they
  have arrived intact. A ZIP is CRC-checked and replaced as a whole.
- A failed or interrupted upload now reports an error, and leaves the existing
  web UI untouched instead of half-replaced.

## 3.4.6 — 2026-11-01
- Current is measured as the RMS over two whole mains cycles, around the measured
  mean. Readings no longer swing from one second to the next, and no longer read
  low when the sensor's zero point drifts (in simulation, a 1 A load read
  anywhere from 0.25 to 0.85 A with the zero shifted 75 mV; now 1.00 A).
- Current calibrations saved by earlier versions are discarded once. A Controller
  recalibrates at boot; a Monitor no longer calibrates at boot.

## 3.4.5 — 2026-11-01
- Overcurrent and phase imbalance stop the motor in manual mode too, and latch
  until cleared. Pressure and dry-run faults stay suspended in manual mode, for
  priming.

## 3.4.4 — 2026-11-01
- A disconnected or shorted water-temperature probe reports "no reading" instead
  of a meaningless number.
- **[web UI]** An unreadable sensor shows "--" instead of freezing the rest of the
  dashboard and the diagnostics page.

## 3.4.3 — 2026-11-01
- Home Assistant shows a unit's entities as unavailable when it drops off MQTT
  (last will on `homeassistant/<id>/availability`), instead of keeping its last
  state — a running pump included.

## 3.4.2 — 2026-11-01
- A Controller no longer latches ERROR on every boot, so the pump restarts on its
  own after a power cut, an update or a reset.
- A WiFi outage no longer stalls the pump control and protections: reconnection
  runs in the background without blocking.
- The motor output is held off from reset instead of floating during boot.

## 3.4.1 — 2026-11-01
- **[web UI]** The dashboard header reads "AquaSensys - C3" or "AquaSensys - M".

## 3.4.0 — 2026-11-01
- Each unit's identity (`device_id`, `device_name`) comes from `config.json`,
  defaulting by device type, so units flashed with the same build no longer
  collide on MQTT. The MQTT client id includes the MAC address.

## 3.3.0 — 2026-11-01
- **[web UI]** The dashboard shows the device name, model and firmware version.

## 3.2.0 — 2026-11-01
- Flow input de-glitching: the pin no longer uses the internal pull-down; edges
  faster than a real pulse are ignored (`flow_debounce_us`), and implausible
  samples are discarded (`max_flow_lpm`).
- Configurable flow calibration (`flow_k_factor`, `flow_k_offset`); the 1"
  connection uses F = 8·Q − 4.

## 3.1.0 — 2026-11-01
- Flow rate depends on the connection size: 3/4" (F = 6.6·Q, the default) or 1".
  Fixes the flow formula, which divided by the elapsed milliseconds instead of
  scaling to pulses per second, with integer division: real flows read as 0.
- Device types: Monitor (`M`, measures only, no motor control) and Controller
  (`C`).
