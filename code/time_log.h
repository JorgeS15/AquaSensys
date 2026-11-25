#ifndef TIME_LOG_H
#define TIME_LOG_H

#include <Arduino.h>

// Real time and the logs on the SD card.
//
// The clock is set over NTP (config.ntp_server) in the time zone of
// config.timezone, a POSIX TZ string such as "WET0WEST,M3.5.0/1,M10.5.0"
// (Portugal). Until the first sync, and on a unit with no internet, the time is
// unknown: log lines then carry only the uptime.
//
// Readings go to /logs/YYYY-MM.csv, one file a month, or to /logs/unsynced.csv
// while the time is unknown. Alarms and other events go to /logs/events.csv.

// Starts NTP. Call once WiFi is up (it keeps retrying by itself afterwards).
void timeBegin();

// Applies a changed config.ntp_server or config.timezone. Safe from any task:
// the change is made by timeService(), which the main loop calls.
void timeRequestRestart();
void timeService();

// True once the clock has been set
bool timeValid();

// Seconds since 1970 (UTC), or 0 while the time is unknown
uint32_t epochNow();

// "2026-11-25 14:03:12" in local time, or "" while the time is unknown
void formatLocalTime(char* out, size_t size);

// Local hour and minute as minutes since midnight, or -1 while unknown
int localMinuteOfDay();

// Appends one line of readings to this month's log
void logReadings(float pressureIn, float pressureOut, float flow, float waterTemp,
                 float currentL1, float currentL2, float currentL3, bool motor,
                 double waterTotalLitres);

// Appends an event (an alarm raised or cleared, a restart, ...) to
// /logs/events.csv. `type` is a short word ("leak", "dry_run", ...), `detail`
// free text without commas or line breaks.
void logEvent(const char* type, const char* detail);

// The reading log's columns, for the web UI and the CSV export
extern const char* const LOG_HEADER;

#endif // TIME_LOG_H
