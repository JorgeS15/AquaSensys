#include "web_routes.h"
#include "freertos_glue.h"
#include "sensor_task.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include "config_manager.h"
#include "ACS712_handler.h"
#include "ota_handler.h"
#include "file_manager.h"
#include "wifi_task.h"

extern AsyncWebServer    server;
extern AsyncEventSource  events;
extern unsigned long     lastMqttReconnectAttempt;
extern PubSubClient      mqttClient;
extern MCP3208           adc;

extern const char* DEVICE_NAME;
extern const char* DEVICE_ID;
extern const char* DEVICE_VERSION;
extern const char* DEVICE_MODEL;

// ── SSE helper: copy snapshot + state then send — safe from any core ─────────

void notifyClients() {
    if (!events.count()) return;
    if (!apModeActive && !wifiConnected) return;

    SensorSnapshot snap = {};
    SystemState    st   = {};
    { SnapLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) snap = latestSnapshot; }
    { SttLock  lock(pdMS_TO_TICKS(10)); if (lock.ok()) st   = systemState;    }

    StaticJsonDocument<256> doc;
    doc["pressure"]      = snap.pressure;
    doc["temperature"]   = snap.temperature;
    doc["ambientTemp"]   = snap.ambientTemp;
    doc["flow"]          = snap.flow;
    doc["motor"]         = st.motor;
    doc["manualOverride"]= st.manualOverride;
    doc["mainSwitch"]    = st.mainSwitch;
    doc["error"]         = st.error;

    String json;
    serializeJson(doc, json);
    events.send(json.c_str(), "update", millis());
}

void publishDiagnostics() {
    if (!events.count()) return;

    SensorSnapshot snap = {};
    { SnapLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) snap = latestSnapshot; }

    StaticJsonDocument<512> doc;
    doc["current_l1"]    = snap.currentL1;
    doc["current_l2"]    = snap.currentL2;
    doc["current_l3"]    = snap.currentL3;
    doc["current_total"] = snap.currentTotal;
    doc["temp_ambient"]  = snap.ambientTemp;
    doc["temp_water"]    = snap.waterTemp;
    doc["pressure_in"]   = snap.pressureIn;
    doc["pressure_out"]  = snap.pressureOut;
    doc["uptime"]        = millis() / 1000;
    doc["free_heap"]     = ESP.getFreeHeap();
    doc["wifi_rssi"]     = WiFi.RSSI();
    doc["motor_runtime_s"] = snap.motorRuntimeSeconds;

    String json;
    serializeJson(doc, json);
    events.send(json.c_str(), "diagnostics", millis());
}

void publishDebugData() {
    if (!events.count()) return;

    DynamicJsonDocument doc(1536);

    // ── SD card section (holds spiMutex for the duration) ────────────────
    {
        SPILock lock;
        if (lock.ok()) {
            doc["sd_detected"] = (SD.cardType() != CARD_NONE);
            doc["sd_type"]     = (SD.cardType() == CARD_MMC)  ? "MMC"  :
                                 (SD.cardType() == CARD_SD)   ? "SD"   :
                                 (SD.cardType() == CARD_SDHC) ? "SDHC" : "UNKNOWN";
            doc["sd_size"]     = SD.cardSize();

            int fileCount = 0;
            File root = SD.open("/");
            if (root) {
                File file = root.openNextFile();
                while (file) { fileCount++; file = root.openNextFile(); }
                root.close();
            }
            doc["sd_files"] = fileCount;
        }
    }

    // ── Raw ADC values (separate spiMutex scope) ──────────────────────────
    doc["mcp_init"]   = true;
    doc["mcp_vref"]   = 5.0f;
    doc["mcp_errors"] = 0;
    doc["spi_status"] = "OK";
    {
        SPILock lock;
        if (lock.ok()) {
            for (int i = 0; i < 8; i++)
                doc["adc_ch" + String(i)] = adc.analogRead(i);
        }
    }

    // ── Averaged voltage readings — readMCP3208Average takes spiMutex internally
    for (int i = 0; i < 4; i++)
        doc["volt_ch" + String(i)] = readMCP3208Average(i, 10);

    // ── Processed sensor values from snapshot ────────────────────────────
    SensorSnapshot snap = {};
    { SnapLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) snap = latestSnapshot; }

    doc["pressure_in"]  = snap.pressureIn;
    doc["pressure_out"] = snap.pressureOut;
    doc["temp_ambient"] = snap.ambientTemp;
    doc["temp_water"]   = snap.waterTemp;

    // ── Calibration offsets ───────────────────────────────────────────────
    {
        CfgLock lock(pdMS_TO_TICKS(10));
        if (lock.ok()) {
            doc["cal_press_in"]  = config.pressure_in_offset;
            doc["cal_press_out"] = config.pressure_out_offset;
            doc["cal_curr_l1"]   = config.current_offset_l1;
            doc["cal_curr_l2"]   = config.current_offset_l2;
            doc["cal_curr_l3"]   = config.current_offset_l3;
            doc["mqtt_server"]   = config.mqtt_server;
            doc["mqtt_port"]     = config.mqtt_port;
        }
    }

    // ── System info ───────────────────────────────────────────────────────
    doc["free_heap"]   = ESP.getFreeHeap();
    doc["uptime"]      = millis() / 1000;
    doc["reset_reason"]= esp_reset_reason();
    doc["cpu_freq"]    = ESP.getCpuFreqMHz();
    doc["wifi_connected"] = WiFi.isConnected();
    doc["wifi_ssid"]      = WiFi.SSID();
    doc["wifi_rssi"]      = WiFi.RSSI();
    doc["wifi_ip"]        = WiFi.localIP().toString();
    doc["mqtt_connected"] = mqttClient.connected();
    doc["mqtt_last"]      = (millis() - lastMqttReconnectAttempt) / 1000;

    // ── Control state ─────────────────────────────────────────────────────
    SystemState st = {};
    { SttLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) st = systemState; }
    doc["motor"]          = st.motor;
    doc["main_switch"]    = st.mainSwitch;
    doc["manual_override"]= st.manualOverride;
    doc["error"]          = st.error;

    String json;
    serializeJson(doc, json);
    events.send(json.c_str(), "debug", millis());
}

// ── Route registration ────────────────────────────────────────────────────────

void webRoutes() {
    setupFileManagerRoutes(server);

    // Static file helper macro: serve from SD or 404
    auto serveSD = [](AsyncWebServerRequest* req, const char* path, const char* mime) {
        SPILock lock;
        if (lock.ok() && SD.exists(path))
            req->send(SD, path, mime);
        else
            req->send(404, "text/plain", String(path) + " not found on SD card");
    };

    server.on("/",               HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/index.html",      "text/html"); });
    server.on("/app.js",         HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/app.js",          "application/javascript"); });
    server.on("/style.css",      HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/style.css",       "text/css"); });
    server.on("/settings.html",  HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/settings.html",   "text/html"); });
    server.on("/settings.js",    HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/settings.js",     "application/javascript"); });
    server.on("/translations.js",HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/translations.js", "application/javascript"); });
    server.on("/language.js",    HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/language.js",     "application/javascript"); });
    server.on("/diagnostics.html",HTTP_GET,[serveSD](AsyncWebServerRequest* r){ serveSD(r, "/diagnostics.html","text/html"); });
    server.on("/debug",          HTTP_GET, [serveSD](AsyncWebServerRequest* r){ serveSD(r, "/debug.html",      "text/html"); });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", getConfigJson());
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest*) {},
    NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        String jsonStr = String((char*)data).substring(0, len);
        if (updateConfigFromJson(jsonStr)) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, jsonStr);
            bool wifiChanged = doc.containsKey("wifi_ssid") || doc.containsKey("wifi_password");
            request->send(200, "application/json",
                "{\"status\":\"success\",\"wifi_changed\":" + String(wifiChanged ? "true":"false") + "}");
            if (wifiChanged) {
                SttLock lock(pdMS_TO_TICKS(10));
                if (lock.ok()) systemState.rebootRequested = true;
            }
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
        }
    });

    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        {
            SPILock lock;
            if (lock.ok()) {
                if (SD.exists("/config.json")) SD.remove("/config.json");
            }
        }
        config = Config();
        saveConfig();
        request->send(200, "application/json", "{\"status\":\"success\"}");
        SttLock lock(pdMS_TO_TICKS(10));
        if (lock.ok()) systemState.rebootRequested = true;
    });

    // Current sensor calibration — blocks async_tcp for ~1 s (intentional: manual/infrequent)
    server.on("/api/calibrate-current", HTTP_POST, [](AsyncWebServerRequest* request) {
        bool motorOn = false;
        { SttLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) motorOn = systemState.motor; }
        if (motorOn) {
            request->send(400, "application/json",
                "{\"error\":\"Motor must be OFF for calibration\",\"motor_status\":\"ON\"}");
            return;
        }

        bool success = false;
        CalibrationData cal = {};
        {
            SPILock lock; // held for entire calibration (~1 s)
            if (lock.ok()) {
                success = currentSensor.performAutoCalibration(100);
                if (success) cal = currentSensor.getCalibrationData();
            }
        }

        if (success) {
            config.current_offset_l1 = cal.offsetL1;
            config.current_offset_l2 = cal.offsetL2;
            config.current_offset_l3 = cal.offsetL3;
            saveConfig(); // takes spiMutex internally

            DynamicJsonDocument doc(256);
            doc["status"]    = "success";
            doc["offset_l1"] = cal.offsetL1;
            doc["offset_l2"] = cal.offsetL2;
            doc["offset_l3"] = cal.offsetL3;
            String resp; serializeJson(doc, resp);
            request->send(200, "application/json", resp);
        } else {
            request->send(500, "application/json",
                "{\"error\":\"Calibration failed - check motor is OFF and retry\"}");
        }
    });

    server.on("/api/calibrate-pressure", HTTP_POST, [](AsyncWebServerRequest* request) {
        // readMCP3208Average takes spiMutex internally for each call
        float voltageIn  = readMCP3208Average(2, 10);
        float voltageOut = readMCP3208Average(1, 10);

        float rawIn  = (voltageIn  - 0.5f) * 1.25f;
        float rawOut = (voltageOut - 0.5f) * 1.25f;

        {
            CfgLock lock;
            if (!lock.ok()) { request->send(500, "application/json", "{\"error\":\"config lock\"}"); return; }
            config.pressure_in_offset  = -(rawIn  + config.pressure_offset);
            config.pressure_out_offset = -(rawOut + config.pressure_offset);
            config.pressure_calibrated = true;
        }

        if (saveConfig()) {
            DynamicJsonDocument doc(256);
            doc["status"]     = "success";
            doc["offset_in"]  = config.pressure_in_offset;
            doc["offset_out"] = config.pressure_out_offset;
            doc["raw_in"]     = rawIn;
            doc["raw_out"]    = rawOut;
            String resp; serializeJson(doc, resp);
            request->send(200, "application/json", resp);
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save calibration\"}");
        }
    });

    server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest* request) {
        SensorSnapshot snap = {};
        { SnapLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) snap = latestSnapshot; }

        DynamicJsonDocument doc(512);
        doc["uptime"]      = millis() / 1000;
        doc["heap"]        = ESP.getFreeHeap();
        doc["resetReason"] = esp_reset_reason();
        doc["wifiStrength"]= WiFi.RSSI();
        {
            SPILock lock(pdMS_TO_TICKS(10));
            doc["sdCardStatus"] = (lock.ok() && SD.cardType() != CARD_NONE) ? "Connected" : "Not Found";
            doc["sdCardSize"]   = lock.ok() ? SD.cardSize() / (1024 * 1024) : 0;
        }
        doc["adcType"]      = "MCP3208 (Rodolfo Prieto)";
        doc["current_l1"]   = snap.currentL1;
        doc["current_l2"]   = snap.currentL2;
        doc["current_l3"]   = snap.currentL3;
        doc["current_total"]= snap.currentTotal;
        doc["temp_ambient"] = snap.ambientTemp;
        doc["temp_water"]   = snap.waterTemp;
        doc["pressure_in"]  = snap.pressureIn;
        doc["pressure_out"] = snap.pressureOut;

        String json; serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        SttLock lock(pdMS_TO_TICKS(10));
        if (lock.ok()) systemState.rebootRequested = true;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/command", HTTP_POST, [](AsyncWebServerRequest*) {},
    NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        DynamicJsonDocument doc(128);
        if (deserializeJson(doc, data, len) || !doc.containsKey("command")) {
            request->send(400, "application/json", "{\"error\":\"invalid request\"}");
            return;
        }

        String command = doc["command"].as<String>();
        {
            SttLock lock(pdMS_TO_TICKS(20)); // stateMutex for compound read-modify-write
            if (!lock.ok()) { request->send(503, "application/json", "{\"error\":\"busy\"}"); return; }

            if (command == "toggle") {
                systemState.manualOverride   = true;
                systemState.manualMotorState = !systemState.manualMotorState;
            } else if (command == "override") {
                systemState.manualOverride = !systemState.manualOverride;
            } else if (command == "mainSwitch") {
                systemState.mainSwitch = !systemState.mainSwitch;
                if (systemState.mainSwitch) systemState.error = false;
            }
        }

        notifyClients();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    events.onConnect([](AsyncEventSourceClient*) {
        // Delivery of first data happens within 1 s (sensorTask → mqttTask signal).
        // Do NOT call SPI-touching functions here — this callback runs in the async_tcp
        // task and the spiMutex may be held by sensorTask on Core 1.
    });
}
