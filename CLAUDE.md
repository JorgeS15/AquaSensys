# AquaSensys — notes for Claude

## Versioning

The firmware version is `DEVICE_VERSION` in `code/code.ino`, with a short comment
describing the change.

Small changes increment **only the last digit**: 3.4.0 → 3.4.1 → 3.4.2 → …
Every change to the firmware or the web UI gets its own bump.

Every bump gets an entry in `CHANGELOG.md`, newest first, dated when it reaches
`main`. Mark changes that live in `code/sd/` with **[web UI]**. Update
`README.md` when a change adds or alters a `config.json` key or a behaviour
it describes.

## Things that are easy to get wrong

- **The web UI lives on the SD card, not in the firmware.** An OTA firmware update
  does not touch `code/sd/`. Any change there only reaches a device once
  `sd-assets.zip` (from the CI release) is uploaded through the device's `/upload`
  page. Say so whenever a change touches `code/sd/`.
- **CI only builds on push to `main`.** Pull requests are never compiled, so the
  first real build of any change happens after merge.
- **One firmware binary runs on every unit.** Anything that must differ between
  units (identity, device type, connection size, calibration) belongs in
  `config.json`, never in a compile-time constant.
