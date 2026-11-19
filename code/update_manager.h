#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Automatic updates from the project's GitHub releases.
//
// A background task checks the latest release every config.update_check_hours
// (and on request). When it is newer than this firmware, the web UI and Home
// Assistant show it; nothing is installed until someone accepts it there.
// Installing then fetches, in order:
//   1. sd-assets.zip to the SD card (staged; nothing live changes yet),
//   2. the firmware into the inactive OTA slot (checked against the release's
//      SHA-256; the unit keeps running the old one until it reboots),
//   3. the web UI, replaced from the staged archive,
// and reboots. A failure in 1 or 2 leaves the unit exactly as it was.

// Starts the background task. Call once, after the WiFi and SD setup.
void updateManagerBegin();

// Ask for a check now, instead of waiting for the next scheduled one.
void updateRequestCheck();

// Ask to install the latest release. Returns false, with the reason in `why`,
// if there is nothing to install or an update is already running.
bool updateRequestInstall(String& why);

// True while checking or installing.
bool updateBusy();

// Full status for the web UI (/api/update).
void updateStatusJson(JsonDocument& doc);

// Payload for the Home Assistant MQTT `update` entity.
void updateMqttStateJson(JsonDocument& doc);

// Set when the update state changed and Home Assistant should be told; the
// MQTT task publishes and clears it.
extern volatile bool updateStatePending;

#endif // UPDATE_MANAGER_H
