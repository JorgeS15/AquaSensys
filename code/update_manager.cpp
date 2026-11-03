#include "update_manager.h"
#include "config_manager.h"
#include "file_manager.h"
#include "ota_handler.h"
#include <WiFi.h>
#include <SD.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

extern const char* DEVICE_VERSION;
extern bool wifiConnected;
extern volatile bool rebootRequested;

// The root certificates ESP-IDF ships with (the full Mozilla set, stored as
// subject + public key only), already compiled into the core's mbedTLS library.
// GitHub serves the API and the release files from different hosts and CAs;
// with the whole set, a change of CA on their side doesn't break updates.
extern const uint8_t crtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crtBundleEnd[]   asm("_binary_x509_crt_bundle_end");

static const char* RELEASES_API   = "https://api.github.com/repos/JorgeS15/AquaSensys/releases/latest";
static const char* FIRMWARE_PREFIX = "aquasensys-firmware-";
static const char* UI_ASSET_NAME  = "sd-assets.zip";
static const char* UI_ZIP_PATH    = "/update-ui.zip";

// First check this long after WiFi comes up, so a boot isn't slowed by it.
static const uint32_t FIRST_CHECK_DELAY_MS = 2UL * 60UL * 1000UL;
// After a failed check (no internet, GitHub rate limit), retry sooner.
static const uint32_t RETRY_AFTER_FAILURE_MS = 60UL * 60UL * 1000UL;
// TLS needs ~45 KB of heap on top of what the unit already uses.
static const uint32_t MIN_FREE_HEAP = 60000;

enum UpdatePhase {
    PHASE_IDLE,
    PHASE_CHECKING,
    PHASE_DOWNLOADING_UI,
    PHASE_INSTALLING_FW,
    PHASE_APPLYING_UI,
    PHASE_REBOOTING
};

struct ReleaseInfo {
    String version;     // "3.6.1"
    String tag;         // "v3.6.1-build37"
    String pageUrl;     // the release page, for the notes
    String fwUrl;
    String fwSha256;    // hex, from the asset's "sha256:..." digest
    uint32_t fwSize = 0;
    String uiUrl;
    String uiSha256;
    uint32_t uiSize = 0;
};

volatile bool updateStatePending = false;

static SemaphoreHandle_t stateLock = nullptr;
// Guarded by stateLock
static ReleaseInfo latest;
static bool haveLatest = false;
static String lastError;

static volatile UpdatePhase phase = PHASE_IDLE;
static volatile int progressPct = -1;           // -1 = not known
static volatile bool checkRequested = false;
static volatile bool installRequested = false;
static volatile bool lastCheckFailed = false;
static volatile bool checkedOnce = false;
static volatile uint32_t lastCheckMs = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// "v3.6.0-build36" or "3.6.0" -> {3, 6, 0}
static bool parseVersion(const char* s, int out[3]) {
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s)) return false;
        char* end;
        out[i] = (int)strtol(s, &end, 10);
        s = end;
        if (i < 2) {
            if (*s != '.') return false;
            s++;
        }
    }
    return true;
}

// <0, 0, >0 like strcmp; an unparseable version never counts as newer
static int compareVersions(const char* a, const char* b) {
    int va[3], vb[3];
    if (!parseVersion(a, va) || !parseVersion(b, vb)) return 0;
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}

static void setPhase(UpdatePhase p, int pct = -1) {
    phase = p;
    progressPct = pct;
    updateStatePending = true;
}

static void setProgress(int pct) {
    // Only every 5 %, so Home Assistant isn't flooded
    if (pct / 5 != progressPct / 5) updateStatePending = true;
    progressPct = pct;
}

static void setError(const String& msg) {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    lastError = msg;
    xSemaphoreGive(stateLock);
    Serial.printf("[Update] %s\n", msg.c_str());
}

static String userAgent() {
    return String("AquaSensys/") + DEVICE_VERSION;
}

static void secureClient(NetworkClientSecure& client) {
    client.setCACertBundle(crtBundleStart, crtBundleEnd - crtBundleStart);
}

static String hex(const uint8_t* data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    String s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        s += digits[data[i] >> 4];
        s += digits[data[i] & 0x0f];
    }
    return s;
}

static size_t nextSlotSize() {
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    return next ? next->size : 0;
}

// ---------------------------------------------------------------------------
// Checking
// ---------------------------------------------------------------------------

static bool fetchLatestRelease(ReleaseInfo& out, String& err) {
    NetworkClientSecure client;
    secureClient(client);

    HTTPClient http;
    // HTTP/1.0: no chunked encoding, so the body can be parsed straight off the stream
    http.useHTTP10(true);
    http.setTimeout(15000);
    if (!http.begin(client, RELEASES_API)) {
        err = "Could not start the request to GitHub";
        return false;
    }
    http.setUserAgent(userAgent());
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        if (code < 0)                     err = "GitHub unreachable: " + HTTPClient::errorToString(code);
        else if (code == 403 || code == 429) err = "GitHub rate limit reached; will retry later";
        else if (code == 404)             err = "No release published yet";
        else                              err = "GitHub answered HTTP " + String(code);
        http.end();
        return false;
    }

    // The full response is ~10 KB, mostly release notes and uploader details;
    // keep only what is needed
    StaticJsonDocument<384> filter;
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["size"] = true;
    filter["assets"][0]["digest"] = true;

    DynamicJsonDocument doc(4096);
    DeserializationError jerr = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (jerr) {
        err = String("Unreadable answer from GitHub: ") + jerr.c_str();
        return false;
    }

    const char* tag = doc["tag_name"] | "";
    int v[3];
    if (!parseVersion(tag, v)) {
        err = String("Unrecognised release tag: ") + tag;
        return false;
    }
    out = ReleaseInfo();
    out.tag     = tag;
    out.version = String(v[0]) + "." + v[1] + "." + v[2];
    out.pageUrl = doc["html_url"] | "";

    for (JsonObject a : doc["assets"].as<JsonArray>()) {
        String name   = a["name"] | "";
        String digest = a["digest"] | "";
        String sha    = digest.startsWith("sha256:") ? digest.substring(7) : "";
        if (name.startsWith(FIRMWARE_PREFIX) && name.endsWith(".bin")) {
            out.fwUrl    = a["browser_download_url"] | "";
            out.fwSize   = a["size"] | 0;
            out.fwSha256 = sha;
        } else if (name == UI_ASSET_NAME) {
            out.uiUrl    = a["browser_download_url"] | "";
            out.uiSize   = a["size"] | 0;
            out.uiSha256 = sha;
        }
    }
    if (out.fwUrl.isEmpty()) {
        err = "Release " + out.tag + " has no firmware file";
        return false;
    }
    return true;
}

static void runCheck() {
    setPhase(PHASE_CHECKING);
    checkedOnce = true;
    lastCheckMs = millis();

    ReleaseInfo info;
    String err;
    bool ok = ESP.getFreeHeap() >= MIN_FREE_HEAP;
    if (!ok) err = "Not enough free memory to check now";
    else     ok = fetchLatestRelease(info, err);

    lastCheckFailed = !ok;
    if (ok) {
        bool newer = compareVersions(info.version.c_str(), DEVICE_VERSION) > 0;
        xSemaphoreTake(stateLock, portMAX_DELAY);
        latest = info;
        haveLatest = true;
        lastError = "";
        xSemaphoreGive(stateLock);
        Serial.printf("[Update] Latest release %s (%s); running %s%s\n",
                      info.version.c_str(), info.tag.c_str(), DEVICE_VERSION,
                      newer ? " — update available" : " — up to date");
    } else {
        setError(err);
    }
    setPhase(PHASE_IDLE);
}

// ---------------------------------------------------------------------------
// Installing
// ---------------------------------------------------------------------------

// Download `url` to `path` on the SD card, via `<path>.part`, checking the size
// and SHA-256 the release declared. On failure nothing is left behind.
static bool downloadToSd(const String& url, const char* path, uint32_t expectedSize,
                         const String& expectedSha, String& err) {
    NetworkClientSecure client;
    secureClient(client);

    HTTPClient http;
    http.useHTTP10(true);   // a plain byte stream: no chunk markers mixed into the file
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // GitHub redirects to its file host
    http.setTimeout(20000);
    if (!http.begin(client, url)) {
        err = "Could not start the web UI download";
        return false;
    }
    http.setUserAgent(userAgent());
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        err = "Web UI download failed: " + (code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code));
        http.end();
        return false;
    }

    int declared = http.getSize();   // -1 if the server didn't say
    uint32_t total = declared > 0 ? (uint32_t)declared : expectedSize;

    String part = String(path) + ".part";
    if (SD.exists(part)) SD.remove(part);
    File f = SD.open(part, FILE_WRITE);
    if (!f) {
        err = "Could not create " + part + " on the SD card";
        http.end();
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    NetworkClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    uint32_t got = 0;
    uint32_t lastData = millis();
    bool writeFailed = false;

    while (declared < 0 || got < (uint32_t)declared) {
        size_t avail = stream->available();
        if (!avail) {
            if (!http.connected() || millis() - lastData > 20000) break;
            delay(5);
            continue;
        }
        int n = stream->read(buf, avail < sizeof(buf) ? avail : sizeof(buf));
        if (n <= 0) continue;
        if (f.write(buf, n) != (size_t)n) {
            writeFailed = true;
            break;
        }
        mbedtls_sha256_update(&sha, buf, n);
        got += n;
        lastData = millis();
        if (total) setProgress((int)((uint64_t)got * 100 / total));
        // This task runs on core 0, whose idle task the watchdog checks: with
        // data always waiting, the loop would otherwise never let it run
        vTaskDelay(1);
    }
    f.close();
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (writeFailed)                                err = "Writing the web UI to the SD card failed";
    else if (expectedSize && got != expectedSize)   err = "Web UI download incomplete (" + String(got) + " of " + String(expectedSize) + " bytes)";
    else if (declared > 0 && got != (uint32_t)declared) err = "Web UI download incomplete";
    else if (expectedSha.length() == 64 && hex(digest, sizeof(digest)) != expectedSha)
                                                    err = "Web UI download corrupted (SHA-256 mismatch)";
    if (err.length()) {
        SD.remove(part);
        return false;
    }

    if (SD.exists(path)) SD.remove(path);
    if (!SD.rename(part, path)) {
        SD.remove(part);
        err = "Could not move the web UI archive into place";
        return false;
    }
    return true;
}

static bool flashFirmware(const ReleaseInfo& r, String& err) {
    NetworkClientSecure client;
    secureClient(client);

    httpUpdate.rebootOnUpdate(false);   // the web UI goes in first; we reboot after
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    // Update refuses to switch slots unless the image matches the release's digest
    httpUpdate.setSHA256sum(r.fwSha256.length() == 64 ? r.fwSha256 : String());
    httpUpdate.onProgress([](int done, int total) {
        if (total > 0) setProgress((int)((int64_t)done * 100 / total));
    });

    t_httpUpdate_return res = httpUpdate.update(client, r.fwUrl);
    if (res != HTTP_UPDATE_OK) {
        err = "Firmware download failed: " + httpUpdate.getLastErrorString();
        return false;
    }
    return true;
}

static void runInstall() {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    ReleaseInfo r = latest;
    bool known = haveLatest;
    lastError = "";
    xSemaphoreGive(stateLock);

    String err;
    if (!known || compareVersions(r.version.c_str(), DEVICE_VERSION) <= 0) {
        setError("Nothing to install");
        return;
    }
    size_t slot = nextSlotSize();
    if (r.fwSize > slot) {
        setError("This unit's flash layout is too small for v" + r.version +
                 " — flash it once over USB (see the release notes)");
        return;
    }
    if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
        setError("Not enough free memory to update now");
        return;
    }

    Serial.printf("[Update] Installing %s\n", r.tag.c_str());

    // 1. The web UI archive, to the SD card. Nothing live changes yet.
    bool haveUi = r.uiUrl.length() > 0;
    if (haveUi) {
        setPhase(PHASE_DOWNLOADING_UI, 0);
        if (!downloadToSd(r.uiUrl, UI_ZIP_PATH, r.uiSize, r.uiSha256, err)) {
            setError(err);
            setPhase(PHASE_IDLE);
            return;
        }
    }

    // 2. The firmware, into the inactive slot. Until the reboot, this one keeps running.
    setPhase(PHASE_INSTALLING_FW, 0);
    if (!flashFirmware(r, err)) {
        if (haveUi) SD.remove(UI_ZIP_PATH);
        setError(err);
        setPhase(PHASE_IDLE);
        return;
    }

    // 3. Only now replace the web UI, from the archive verified in step 1. Should
    // this fail, the files already on the card are kept and the new firmware
    // still starts; sd-assets.zip can then be uploaded through /upload.
    if (haveUi) {
        setPhase(PHASE_APPLYING_UI);
        String zipErr;
        int files = 0;
        if (applyZipFile(UI_ZIP_PATH, zipErr, files)) {
            Serial.printf("[Update] Web UI updated, %d file(s)\n", files);
        } else {
            Serial.printf("[Update] Web UI NOT updated: %s — upload sd-assets.zip through /upload\n", zipErr.c_str());
        }
        SD.remove(UI_ZIP_PATH);
    }

    Serial.printf("[Update] %s installed, rebooting\n", r.tag.c_str());
    setPhase(PHASE_REBOOTING);
    delay(1500);   // let the web UI and Home Assistant see it
    rebootRequested = true;
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

static void updateTask(void*) {
    uint32_t wifiSince = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (phase == PHASE_REBOOTING) continue;

        if (!wifiConnected) {
            wifiSince = 0;
            continue;
        }
        if (!wifiSince) wifiSince = millis();

        if (installRequested) {
            installRequested = false;
            runInstall();
            continue;
        }

        bool due = false;
        if (checkRequested) {
            checkRequested = false;
            due = true;
        } else if (config.update_check_hours > 0) {
            if (!checkedOnce) {
                due = millis() - wifiSince >= FIRST_CHECK_DELAY_MS;
            } else {
                uint32_t interval = (uint32_t)config.update_check_hours * 3600000UL;
                if (lastCheckFailed && interval > RETRY_AFTER_FAILURE_MS) interval = RETRY_AFTER_FAILURE_MS;
                due = millis() - lastCheckMs >= interval;
            }
        }
        if (due && !OTA.isUpdating()) runCheck();
    }
}

void updateManagerBegin() {
    if (stateLock) return;
    stateLock = xSemaphoreCreateMutex();
    // TLS handshakes and the SD/flash writes need a deeper stack than the MQTT task
    xTaskCreatePinnedToCore(updateTask, "update", 16384, NULL, 1, NULL, 0);
}

// ---------------------------------------------------------------------------
// Requests and status
// ---------------------------------------------------------------------------

void updateRequestCheck() {
    if (phase == PHASE_IDLE) checkRequested = true;
}

bool updateBusy() {
    return phase != PHASE_IDLE || installRequested || checkRequested;
}

bool updateRequestInstall(String& why) {
    if (phase != PHASE_IDLE || installRequested) { why = "An update is already running"; return false; }
    if (OTA.isUpdating())                        { why = "A firmware upload is in progress"; return false; }
    if (!wifiConnected)                          { why = "No internet connection"; return false; }

    xSemaphoreTake(stateLock, portMAX_DELAY);
    bool available = haveLatest && compareVersions(latest.version.c_str(), DEVICE_VERSION) > 0;
    bool fits = latest.fwSize <= nextSlotSize();
    xSemaphoreGive(stateLock);

    if (!available) { why = "No newer version known — check for updates first"; return false; }
    if (!fits)      { why = "This unit must be flashed once over USB first (see the release notes)"; return false; }

    installRequested = true;
    updateStatePending = true;
    return true;
}

static const char* phaseName(UpdatePhase p) {
    switch (p) {
        case PHASE_CHECKING:       return "checking";
        case PHASE_DOWNLOADING_UI: return "downloading_ui";
        case PHASE_INSTALLING_FW:  return "installing";
        case PHASE_APPLYING_UI:    return "applying_ui";
        case PHASE_REBOOTING:      return "rebooting";
        default:                   return "idle";
    }
}

void updateStatusJson(JsonDocument& doc) {
    UpdatePhase p = installRequested && phase == PHASE_IDLE ? PHASE_DOWNLOADING_UI : (UpdatePhase)phase;
    size_t slot = nextSlotSize();

    doc["current"]     = DEVICE_VERSION;
    doc["state"]       = phaseName(p);
    if (progressPct >= 0 && p != PHASE_IDLE) doc["progress"] = (int)progressPct;
    else                                    doc["progress"] = nullptr;
    doc["check_hours"] = config.update_check_hours;
    doc["slot_size"]   = (uint32_t)slot;
    if (checkedOnce) doc["checked_ago_s"] = (millis() - lastCheckMs) / 1000;
    else             doc["checked_ago_s"] = nullptr;

    xSemaphoreTake(stateLock, portMAX_DELAY);
    if (haveLatest) {
        doc["latest"]        = latest.version;
        doc["tag"]           = latest.tag;
        doc["release_url"]   = latest.pageUrl;
        doc["firmware_size"] = latest.fwSize;
        bool newer = compareVersions(latest.version.c_str(), DEVICE_VERSION) > 0;
        doc["available"]          = newer;
        doc["usb_flash_required"] = newer && latest.fwSize > slot;
    } else {
        doc["latest"]    = nullptr;
        doc["available"] = false;
        doc["usb_flash_required"] = false;
    }
    if (lastError.length()) doc["error"] = lastError;
    else                    doc["error"] = nullptr;
    xSemaphoreGive(stateLock);
}

void updateMqttStateJson(JsonDocument& doc) {
    UpdatePhase p = (UpdatePhase)phase;
    bool installing = installRequested || (p != PHASE_IDLE && p != PHASE_CHECKING);

    doc["installed_version"] = DEVICE_VERSION;
    doc["title"] = "AquaSensys firmware";
    xSemaphoreTake(stateLock, portMAX_DELAY);
    // Until a check has succeeded, report "up to date" rather than unknown
    doc["latest_version"] = haveLatest ? latest.version : String(DEVICE_VERSION);
    if (haveLatest && latest.pageUrl.length()) doc["release_url"] = latest.pageUrl;
    xSemaphoreGive(stateLock);
    doc["in_progress"] = installing;
    if (installing && progressPct >= 0) doc["update_percentage"] = (int)progressPct;
    else                                doc["update_percentage"] = nullptr;
}
