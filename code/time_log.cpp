#include "time_log.h"
#include "config_manager.h"
#include <SD.h>
#include <time.h>
#include <esp_sntp.h>

const char* const LOG_HEADER =
    "time,epoch,uptime_s,pressure_in,pressure_out,flow_lpm,water_temp,"
    "current_l1,current_l2,current_l3,motor,water_total_l";

static const char* LOG_DIR     = "/logs";
static const char* EVENTS_PATH = "/logs/events.csv";
// Anything before this is the clock's power-on value, not a real date
static const time_t VALID_AFTER = 1704067200;   // 2024-01-01

// lwIP keeps the pointer to the server name, not a copy, so it must outlive
// any change to config.ntp_server
static char ntpServer[65];
static volatile bool restartPending = false;

void timeBegin() {
    strlcpy(ntpServer, config.ntp_server.length() ? config.ntp_server.c_str() : "pool.ntp.org",
            sizeof(ntpServer));
    // Sets TZ and (re)starts SNTP with this server, a name or an IP address;
    // lwIP retries on its own until it gets an answer, then re-syncs every hour
    configTzTime(config.timezone.c_str(), ntpServer);
    Serial.printf("[Time] NTP %s, time zone %s\n", ntpServer, config.timezone.c_str());
}

void timeRequestRestart() {
    restartPending = true;
}

void timeService() {
    if (!restartPending) return;
    restartPending = false;
    timeBegin();
}

bool timeValid() {
    return time(nullptr) > VALID_AFTER;
}

uint32_t epochNow() {
    time_t now = time(nullptr);
    return now > VALID_AFTER ? (uint32_t)now : 0;
}

void formatLocalTime(char* out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    time_t now = time(nullptr);
    if (now <= VALID_AFTER) return;
    struct tm t;
    localtime_r(&now, &t);
    strftime(out, size, "%Y-%m-%d %H:%M:%S", &t);
}

int localMinuteOfDay() {
    time_t now = time(nullptr);
    if (now <= VALID_AFTER) return -1;
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_hour * 60 + t.tm_min;
}

// The sensor, control and main tasks all write logs; one at a time
static SemaphoreHandle_t logLock = nullptr;

static bool lockLogs() {
    if (!logLock) {
        // Created on first use; the first log line is written by setup(),
        // before any other task exists
        logLock = xSemaphoreCreateMutex();
        if (!logLock) return false;
    }
    return xSemaphoreTake(logLock, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void unlockLogs() {
    xSemaphoreGive(logLock);
}

static void ensureLogDir() {
    if (!SD.exists(LOG_DIR)) SD.mkdir(LOG_DIR);
}

// Opens `path` for appending, writing `header` first if the file is new
static File openLog(const char* path, const char* header) {
    ensureLogDir();
    File f = SD.open(path, FILE_APPEND);
    if (f && f.size() == 0) f.println(header);
    return f;
}

void logReadings(float pressureIn, float pressureOut, float flow, float waterTemp,
                 float currentL1, float currentL2, float currentL3, bool motor,
                 double waterTotalLitres) {
    char path[32];
    char stamp[24];
    formatLocalTime(stamp, sizeof(stamp));
    if (stamp[0]) {
        // One file a month, named by local date: "2026-11-25 ..." -> 2026-11
        snprintf(path, sizeof(path), "%s/%.7s.csv", LOG_DIR, stamp);
    } else {
        snprintf(path, sizeof(path), "%s/unsynced.csv", LOG_DIR);
    }
    if (!lockLogs()) return;
    File f = openLog(path, LOG_HEADER);
    if (!f) { unlockLogs(); return; }
    // NAN (no reading) is written as an empty field
    char wt[12] = "";
    if (!isnan(waterTemp)) snprintf(wt, sizeof(wt), "%.1f", waterTemp);
    f.printf("%s,%lu,%lu,%.3f,%.3f,%.2f,%s,%.2f,%.2f,%.2f,%d,%.1f\n",
             stamp, (unsigned long)epochNow(), (unsigned long)(millis() / 1000),
             pressureIn, pressureOut, flow, wt,
             currentL1, currentL2, currentL3, motor ? 1 : 0, waterTotalLitres);
    f.close();
    unlockLogs();
}

void logEvent(const char* type, const char* detail) {
    char stamp[24];
    formatLocalTime(stamp, sizeof(stamp));
    Serial.printf("[Event] %s %s\n", type, detail ? detail : "");
    if (!lockLogs()) return;
    File f = openLog(EVENTS_PATH, "time,epoch,uptime_s,event,detail");
    if (f) {
        f.printf("%s,%lu,%lu,%s,%s\n", stamp, (unsigned long)epochNow(),
                 (unsigned long)(millis() / 1000), type, detail ? detail : "");
        f.close();
    }
    unlockLogs();
}
