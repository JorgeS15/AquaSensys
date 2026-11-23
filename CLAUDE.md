# AquaSensys — notes for Claude

## Versioning

The firmware version is `DEVICE_VERSION` in `code/code.ino`, with a short comment
describing the change.

Small changes increment **only the last digit**: 3.4.0 → 3.4.1 → 3.4.2 → …
Every change to the firmware or the web UI gets its own bump.

Every bump gets an entry in `CHANGELOG.md`, newest first, dated when it reaches
`main`. Mark changes that live in `code/sd/` with **[web UI]**. Update
`docs/REFERENCE.md` when a change adds or alters a `config.json` key or a
behaviour it describes. `README.md` is the product page for customers: touch it
only for user-visible features, and keep its screenshots (`site/images/`) in
step with the web UI.

## Things that are easy to get wrong

- **The web UI lives on the SD card, not in the firmware.** A manual OTA firmware
  upload does not touch `code/sd/`; the automatic update (v3.6.0+) installs the
  release's `sd-assets.zip` with the firmware, and from v3.6.2 a unit fetches the
  web UI matching its firmware by itself. Say so whenever a change touches
  `code/sd/`.
- **CI publishes a release only on push to `main`.** Pull requests are compiled
  (no release), and the build is kept as a workflow artifact for testing. A PR
  that changes only docs still produces a release once merged, with the same
  version and a new build number.
- **The roadmap is `ROADMAP.md`**, one GitHub issue per item. Update its status
  column when an item is started or done.
- **One firmware binary runs on every unit.** Anything that must differ between
  units (identity, device type, connection size, calibration) belongs in
  `config.json`, never in a compile-time constant.
