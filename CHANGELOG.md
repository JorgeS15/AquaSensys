# Changelog

Firmware versions, newest first. Dates are when the version reached `main`,
which is when CI builds and publishes it. Small changes bump only the last
digit. Versions up to 3.0.38 are recorded in the git history only.

Each release has two parts: the firmware (flashed OTA) and the web UI on the SD
card (`sd-assets.zip`, uploaded through `/upload`). Entries marked **[web UI]**
only reach a device once the new `sd-assets.zip` is uploaded. From 3.6.0 on, an
automatic update installs both.

## 3.6.18 — 2026-11-25
- The fault limits can be changed: the pressure fault (`fault_min_pressure`,
  `fault_max_pressure`, default 1 and 5 bar, fixed until now), the dry-run
  delay (`dry_run_seconds`, default 10 s, fixed until now), and the
  overcurrent and phase-imbalance limits (until now only in `config.json`).
  They apply without a restart.
- **[web UI]** Settings has a Fault limits card. It refuses pressure limits
  inside the pump's start/stop band, which would trip it on every start.

## 3.6.17 — 2026-11-25
- **[web UI]** Settings → *Logs and backup*: download the readings of any
  days as one CSV, the events log and older logs; download a backup of the
  settings and restore it, here or on a new unit (keeping that unit's name,
  ID and calibration by default).
- `GET /api/config/backup` (with the passwords only when an admin password is
  set and given) and `GET /api/logs`.
- **[web UI]** Date and time fields in Settings get the same style as the
  others.

## 3.6.16 — 2026-11-25
- **[web UI]** History charts on the dashboard: pressure, flow, water
  temperature and, on a Controller, the phase currents, over the last 24 hours
  or 7 days, from the logs on the SD card. Hover (or touch) shows the values
  at any time; a link downloads today's CSV.
- Logs are now one file a day, `/logs/YYYY/YYYY-MM-DD.csv` (about 25 KB), so
  the charts load only the days they show.

## 3.6.15 — 2026-11-25
- Optional password for actions (`admin_password`, user `admin`). With it
  set, every change through the web UI or the API (settings, pump, restart,
  factory reset, calibration, updates, uploads) asks for it; readings stay
  open. Set it in Settings → Password.
- `config.json` is no longer served as a file: it holds the WiFi and MQTT
  passwords, and anyone on the network could download it.
- **[web UI]** Settings has a Password card, and says when an action needs
  the password.

## 3.6.14 — 2026-11-25
- Pump short-cycling alarm (Controller). The automatic starts in the last hour
  are counted; above 20 (configurable, `short_cycle_max_starts`) the
  **Short Cycling** alarm is raised, as a warning by default or, with
  `short_cycle_stop_pump`, as an error that stops the pump.
- Home Assistant: **Pump Starts (last hour)** and **Short Cycling**.
- **[web UI]** The dashboard shows the starts in the last hour, and
  *System status* shows warnings in amber. Settings has a Pump starts card.

## 3.6.13 — 2026-11-25
- The firmware is split into FreeRTOS tasks: control and protections (every
  20 ms, highest priority), the status LED (every 10 ms), the sensors (every
  second) and the network and logging (the main loop). The protections no
  longer wait for sensor reads, WiFi or the SD card, and commands from the
  web UI or Home Assistant take effect within 20 ms.
- Fixes the green heartbeat LED that stopped blinking after a while: its 50 ms
  blink no longer has to fall between two sensor reads.
- The watchdog watches every task (control 10 s, sensors and main loop
  30 s). New reset reasons `control_stall` and `sensor_stall`.
- Fixes a rare false restart of the watchdog itself: a heartbeat stamped just
  after the check read the clock looked 49 days old.
- Log writes and fault flags are safe from several tasks at once.

## 3.6.12 — 2026-11-25
- Home Assistant gets one alarm per fault cause: **Pressure Out of Range**,
  and on a Controller **Dry Run**, **Overcurrent** and **Phase Imbalance**.
  **System Error** stays as the summary. Each cause is recorded in
  `/logs/events.csv` with its reading.
- **[web UI]** *System status* on the dashboard names the cause instead of
  just "Error".

## 3.6.11 — 2026-11-25
- Leak alarm. It goes off when the water hasn't stopped for an hour (a
  dripping tap, a running cistern or a burst pipe never let it stop), or,
  optionally, when water runs for 5 minutes during quiet hours (01:00–06:00 by
  default). It clears once the water stops. All of it is set in Settings →
  Leak alarm; new `config.json` keys `leak_*`.
- On a Controller, the leak alarm can also stop the pump (off by default).
- Home Assistant: **Water Leak** binary sensor and **Continuous Flow Time**
  sensor. Alarms and their end go to `/logs/events.csv`.
- **[web UI]** The flow card shows the leak alarm, and a warning when the
  water has been running for 10 minutes. Settings has a Leak alarm card.

## 3.6.10 — 2026-11-25
- Real time: the unit sets its clock over NTP. The time zone (default
  Portugal, with summer time) and the time server (a name or an IP address,
  e.g. the router's; default `pool.ntp.org`) are in Settings → System
  Parameters, and apply without a restart. New `config.json` keys `timezone`
  and `ntp_server`.
- Logs move to `/logs/YYYY-MM.csv`, one file a month, with the date and time,
  and now include the water temperature and the water total. Without
  internet the lines go to `/logs/unsynced.csv` with the uptime only. The old
  `/log.csv` is left alone.
- Each start is recorded in `/logs/events.csv` with its reason; the alarms
  will be recorded there too.
- `/api/device` reports the unit's time (`time`, `time_synced`).
- **[web UI]** Settings has the time zone and the time server.

## 3.6.9 — 2026-11-23
- Counts the total water through the flow sensor. Home Assistant gets a
  **Water Total** sensor in litres (`water`, `total_increasing`), which the
  Energy dashboard lists under **Water consumption**, with daily, weekly and
  monthly totals.
- The total is kept on the SD card in `/runtime.json` (with the motor
  runtime), saved every minute while water flows and before any clean
  restart (reboot, update). A power cut loses at most a minute of counting.
  `runtime.json` is now written to a temporary file and renamed, so a power
  cut while writing it no longer loses the counters.
- `POST /api/water_total` with `litres=<value>` sets the total, to match the
  house meter or to start again from 0.
- **[web UI]** The dashboard shows the total under the flow rate, in litres
  up to 1000 L and in m³ from there.

## 3.6.8 — 2026-11-23
- Fixes an automatic update failing with "Firmware download failed: HTTP
  error: read Timeout". The firmware download used the core's default of 8 s
  to wait for GitHub's file host, which a fresh release, two TLS connections
  and a slower link can exceed. Every update request now waits up to 30 s, and
  a download that fails on the network is tried once more.
- On 3.6.0 – 3.6.7, pressing **Install** again usually gets past this error.

## 3.6.7 — 2026-11-23
- **[web UI]** Settings → System Parameters has the log interval
  (`log_interval_minutes`) and how often to check for updates
  (`update_check_hours`); both were only reachable in `config.json`.
- **[web UI]** The debug page fits a phone screen (it scrolled sideways).
- **[web UI]** After a rollback (3.6.6), the update notice says the last
  attempt didn't start and the unit went back to the previous version.

## 3.6.6 — 2026-11-23
- A new firmware is on trial until it has been connected to WiFi for 30 s.
  If it restarts before that (a crash, the loop watchdog, a power cut) or
  goes 10 minutes without WiFi, the unit returns to the previous firmware by
  itself, instead of needing a USB flash. Applies to automatic and manual
  updates alike; a USB flash is never on trial.
- While on trial, a further update is refused, since it would overwrite the
  firmware a rollback returns to. `/api/update` reports `on_trial` and
  `rolled_back`.

## 3.6.5 — 2026-11-23
- Loop watchdog: if the main loop stops for 30 s, the unit restarts. Before,
  a hung loop left the web UI and MQTT answering with frozen readings while
  the pump control and protections were stopped.
- The reason for the last start is reported: `power_on`, `restart`,
  `loop_stall` (the watchdog), `crash`, `brownout`, … as the **Last Reset
  Reason** sensor in Home Assistant and `reset_reason` in `/api/device`.

## 3.6.4 — 2026-11-22
- Fixes the update check, which failed with "Unreadable answer from GitHub:
  IncompleteInput". GitHub's answer (~18 KB) arrives in more than one TLS
  record, and the JSON was parsed through a read that gave up at the first
  moment no data was waiting. It is now read as it arrives. The same fix covers
  fetching the matching web UI (3.6.2).
- **Units on 3.6.0 – 3.6.3 cannot find this update themselves**, since their
  check has the bug. Upload `aquasensys-firmware-*.bin` once through the
  device's firmware update page; the matching web UI then installs itself
  (3.6.2), and later updates are automatic.

## 3.6.3 — 2026-11-22
- **[web UI]** Dark theme on every page. A selector next to the language one
  offers **Auto** (follows the phone's or computer's setting, the default),
  **Light** and **Dark**; the choice is remembered per browser. The light theme
  is unchanged.

## 3.6.2 — 2026-11-22
- The web UI on the SD card is kept in step with the firmware. Each release's
  `sd-assets.zip` now carries `ui_version.txt`; at boot the unit compares it
  with its own version and, if they differ, fetches `sd-assets.zip` from its
  own release and installs it (staged and CRC-checked, as an upload). This
  covers an update whose web UI step failed, a USB flash, and a card moved from
  another unit. A failed attempt is retried every hour.
- CI passes the build number to the firmware, so it knows its own release.
  `/api/update` reports `build` and `ui_version`.

## 3.6.1 — 2026-11-19
- Home Assistant: the flow sensor is now a volume flow rate (`volume_flow_rate`,
  L/min) with long-term statistics, so it can be picked as the **Water flow
  rate** in the Energy dashboard. Pressure, temperature and the filter pressure
  drop get long-term statistics too.
- With two pressure sensors, Home Assistant names them **Pressure In** (before
  the filter) and **Pressure Out** (after it). Entity ids and history are kept.

## 3.6.0 — 2026-11-19
- **Automatic updates.** The unit checks GitHub for a newer release every
  `update_check_hours` (default 12; `0` = only when asked), and installs it once
  accepted: firmware and web UI together, each checked against the release's
  SHA-256, then a restart. A failed download leaves the unit as it was.
- Home Assistant gets a firmware **update** entity (Settings → Updates, with
  Install) and a **Check for Firmware Update** button.
- **New flash layout: one USB flash needed.** The partition scheme changes from
  `default` (1.25 MB app slots) to `min_spiffs` (1.875 MB), since HTTPS makes the
  firmware too big for the old slots (1.40 MB). A unit on 3.5.0 or older must be
  flashed once over USB — instructions in the release notes; it refuses this
  firmware over OTA and keeps running. Settings on the SD card are kept.
- The release adds `aquasensys-full-*.bin`, a complete image for that USB flash.
- CI pins the ESP32 core to 3.3.12.
- **[web UI]** The dashboard shows "Firmware vX is available" with Install and
  release notes, the progress while it installs, and reloads itself afterwards.
  "Check for updates" under the firmware version.

## 3.5.0 — 2026-11-02
- Optional second pressure sensor, after the filter (`dual_pressure_sensors`,
  default off). With both fitted, the drop across the filter (inlet − outlet)
  is published over MQTT (`pressure_out`, `filter_dp`) and on the web UI.
- Clogged-filter alarm (`filter_alarm_enabled`, on by default): the filter is
  flagged once the drop has stayed at or above `filter_dp_warning` (0.5 bar)
  for `filter_alarm_delay_s` (30 s), so short pressure spikes are ignored. The
  drop is judged only while the flow is above `filter_min_flow_lpm`
  (5 L/min) — so it works on Monitor units too, where no pump state is known.
  A clogged verdict holds while there is no flow and clears after the same
  delay back below the limit. Home Assistant gets a "Filter Clogged" problem
  sensor. It is a warning only: it never stops the pump.
- The dashboard header reads "AquaSensys C3" / "AquaSensys M" (was
  "AquaSensys - C3" / "AquaSensys - M").
- **[web UI]** New look for every page: the pump state and controls up front,
  a pressure gauge with the start/stop band, temperature and flow side by side,
  and the unit's model, firmware and connection size. Monitor units show
  readings only. Works at phone width.
- **[web UI]** The Diagnostics and Settings headers show the unit's name too.
- **[web UI]** With two sensors the dashboard shows a filter card (before,
  after, drop and the alarm state) and the diagnostics page shows both sensors
  and the drop; with one sensor only the mains pressure appears.
- **[web UI]** New **Settings → Pressure Sensors** card: number of sensors,
  clogged-filter alarm on/off, its threshold, minimum flow and delay.
- **[web UI]** No longer loads web fonts, so the pages render the same without
  internet access (AP mode).

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
