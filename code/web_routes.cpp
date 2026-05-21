#include "web_routes.h"
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
#include "MCP3208.h"

// Globals from code.ino
extern AsyncWebServer server;
extern AsyncEventSource events;
extern bool wifiConnected;
extern bool apModeActive;
extern unsigned long motorRuntimeSeconds;
extern unsigned long lastMqttReconnectAttempt;
extern PubSubClient mqttClient;
extern MCP3208 adc;

// Device constants from code.ino
extern const char* DEVICE_NAME;
extern const char* DEVICE_ID;
extern const char* DEVICE_VERSION;
extern const char* DEVICE_MODEL;

// Sensor readings from code.ino
extern float pressure, temperature, flow;
extern float pressureIn, pressureOut;
extern float ambientTemp, waterTemp;
extern float currentL1, currentL2, currentL3, currentTotal;

// Control flags from code.ino
extern volatile bool motor, manualOverride, manualMotorState;
extern volatile bool mainSwitch, error, rebootRequested;

// Helper function defined in code.ino
float readMCP3208Average(int channel, int samples);

void notifyClients() {
    // Skip if no clients, or if STA WiFi is down (avoids zombie-client watchdog)
    // In AP mode the network is up, so SSE sends are safe
    if (!events.count()) return;
    if (!apModeActive && !wifiConnected) return;
    StaticJsonDocument<256> doc;
    doc["pressure"] = pressure;
    doc["temperature"] = temperature;
    doc["ambientTemp"] = ambientTemp;
    doc["flow"] = flow;
    doc["motor"] = motor;
    doc["manualOverride"] = manualOverride;
    doc["mainSwitch"] = mainSwitch;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    events.send(json.c_str(), "update", millis());
}

void publishDiagnostics() {
    if (!events.count()) return;

    StaticJsonDocument<512> doc;

    doc["current_l1"] = currentL1;
    doc["current_l2"] = currentL2;
    doc["current_l3"] = currentL3;
    doc["current_total"] = currentTotal;

    doc["temp_ambient"] = ambientTemp;
    doc["temp_water"] = waterTemp;

    doc["pressure_in"] = pressureIn;
    doc["pressure_out"] = pressureOut;

    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["motor_runtime_s"] = motorRuntimeSeconds;

    String json;
    serializeJson(doc, json);

    events.send(json.c_str(), "diagnostics", millis());
}

void publishDebugData() {
    if (!events.count()) return;

    DynamicJsonDocument doc(1536);

    // SD Card Status
    doc["sd_detected"] = (SD.cardType() != CARD_NONE);
    doc["sd_type"] = (SD.cardType() == CARD_MMC) ? "MMC" :
                     (SD.cardType() == CARD_SD) ? "SD" :
                     (SD.cardType() == CARD_SDHC) ? "SDHC" : "UNKNOWN";
    doc["sd_size"] = SD.cardSize();

    // Count files on SD
    int fileCount = 0;
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            fileCount++;
            file = root.openNextFile();
        }
        root.close();
    }
    doc["sd_files"] = fileCount;

    // MCP3208 Status
    doc["mcp_init"] = true;
    doc["mcp_vref"] = 5.0f;  // MCP3208_VREF
    doc["mcp_errors"] = 0;
    doc["spi_status"] = "OK";

    // Raw ADC Values (0-4095)
    for (int i = 0; i < 8; i++) {
        doc["adc_ch" + String(i)] = adc.analogRead(i);
    }

    // Voltage Readings (channels 0-3)
    for (int i = 0; i < 4; i++) {
        doc["volt_ch" + String(i)] = readMCP3208Average(i, 10);  // NUM_SAMPLES = 10
    }

    // Processed Sensor Values
    doc["pressure_in"] = pressureIn;
    doc["pressure_out"] = pressureOut;
    doc["temp_ambient"] = ambientTemp;
    doc["temp_water"] = waterTemp;

    // Calibration Offsets
    doc["cal_press_in"] = config.pressure_in_offset;
    doc["cal_press_out"] = config.pressure_out_offset;
    doc["cal_curr_l1"] = config.current_offset_l1;
    doc["cal_curr_l2"] = config.current_offset_l2;
    doc["cal_curr_l3"] = config.current_offset_l3;

    // System Information
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["reset_reason"] = esp_reset_reason();
    doc["cpu_freq"] = ESP.getCpuFreqMHz();

    // WiFi Status
    doc["wifi_connected"] = WiFi.isConnected();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["wifi_ip"] = WiFi.localIP().toString();

    // MQTT Status
    doc["mqtt_connected"] = mqttClient.connected();
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_last"] = (millis() - lastMqttReconnectAttempt) / 1000;

    // Motor & Control State
    doc["motor"] = motor;
    doc["main_switch"] = mainSwitch;
    doc["manual_override"] = manualOverride;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    events.send(json.c_str(), "debug", millis());
}

void webRoutes() {
    // Setup file manager routes
    setupFileManagerRoutes(server);

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/index.html")) {
            request->send(SD, "/index.html", "text/html");
        } else {
            request->send(404, "text/plain", "index.html not found on SD card");
        }
    });

    // Static file routes
    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/app.js")) {
            request->send(SD, "/app.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "app.js not found on SD card");
        }
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/style.css")) {
            request->send(SD, "/style.css", "text/css");
        } else {
            request->send(404, "text/plain", "style.css not found on SD card");
        }
    });

    server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/settings.html")) {
            request->send(SD, "/settings.html", "text/html");
        } else {
            request->send(404, "text/plain", "settings.html not found on SD card");
        }
    });

    server.on("/settings.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/settings.js")) {
            request->send(SD, "/settings.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "settings.js not found on SD card");
        }
    });

    server.on("/translations.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/translations.js")) {
            request->send(SD, "/translations.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "translations.js not found on SD card");
        }
    });

    server.on("/language.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/language.js")) {
            request->send(SD, "/language.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "language.js not found on SD card");
        }
    });

    server.on("/diagnostics.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/diagnostics.html")) {
            request->send(SD, "/diagnostics.html", "text/html");
        } else {
            request->send(404, "text/plain", "diagnostics.html not found on SD card");
        }
    });

    // API endpoint to get current configuration
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getConfigJson());
    });

    // API endpoint to update configuration
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        String jsonStr = String((char*)data).substring(0, len);

        if (updateConfigFromJson(jsonStr)) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, jsonStr);
            bool wifiChanged = doc.containsKey("wifi_ssid") || doc.containsKey("wifi_password");

            request->send(200, "application/json", "{\"status\":\"success\",\"wifi_changed\":" + String(wifiChanged ? "true" : "false") + "}");

            if (wifiChanged) {
                rebootRequested = true;
            }
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
        }
    });

    // Factory reset endpoint
    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (SD.exists("/config.json")) {
            SD.remove("/config.json");
        }

        config = Config();
        saveConfig();

        request->send(200, "application/json", "{\"status\":\"success\"}");
        rebootRequested = true;
    });

    // Manual current sensor calibration endpoint
    server.on("/api/calibrate-current", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (motor) {
            request->send(400, "application/json",
                "{\"error\":\"Motor must be OFF for calibration\",\"motor_status\":\"ON\"}");
            return;
        }

        Serial.println("\n=== Manual Calibration Requested via API ===");
        if (currentSensor.performAutoCalibration(100)) {
            CalibrationData cal = currentSensor.getCalibrationData();
            config.current_offset_l1 = cal.offsetL1;
            config.current_offset_l2 = cal.offsetL2;
            config.current_offset_l3 = cal.offsetL3;
            saveConfig();

            DynamicJsonDocument doc(256);
            doc["status"] = "success";
            doc["offset_l1"] = config.current_offset_l1;
            doc["offset_l2"] = config.current_offset_l2;
            doc["offset_l3"] = config.current_offset_l3;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(500, "application/json",
                "{\"error\":\"Calibration failed - check motor is OFF and retry\"}");
        }
    });

    // Pressure sensor calibration endpoint
    server.on("/api/calibrate-pressure", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("\n=== Pressure Calibration Requested via API ===");

        // MCP_CH_PRESSURE_IN = 2, MCP_CH_PRESSURE_OUT = 1, NUM_SAMPLES = 10
        float voltageIn  = readMCP3208Average(2, 10);
        float voltageOut = readMCP3208Average(1, 10);

        float rawPressureIn  = (voltageIn  - 0.5) * 1.25;
        float rawPressureOut = (voltageOut - 0.5) * 1.25;

        config.pressure_in_offset  = -(rawPressureIn  + config.pressure_offset);
        config.pressure_out_offset = -(rawPressureOut + config.pressure_offset);
        config.pressure_calibrated = true;

        if (saveConfig()) {
            Serial.printf("Pressure calibration successful:\n");
            Serial.printf("  Inlet offset: %.3f bar\n", config.pressure_in_offset);
            Serial.printf("  Outlet offset: %.3f bar\n", config.pressure_out_offset);

            DynamicJsonDocument doc(256);
            doc["status"] = "success";
            doc["offset_in"] = config.pressure_in_offset;
            doc["offset_out"] = config.pressure_out_offset;
            doc["raw_in"] = rawPressureIn;
            doc["raw_out"] = rawPressureOut;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(500, "application/json",
                "{\"error\":\"Failed to save calibration to config\"}");
        }
    });

    // Diagnostics JSON endpoint (for backward compatibility)
    server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["uptime"] = millis() / 1000;
        doc["heap"] = ESP.getFreeHeap();
        doc["resetReason"] = esp_reset_reason();
        doc["wifiStrength"] = WiFi.RSSI();
        doc["sdCardStatus"] = SD.cardType() != CARD_NONE ? "Connected" : "Not Found";
        doc["sdCardSize"] = SD.cardSize() / (1024 * 1024);
        doc["adcType"] = "MCP3208 (Rodolfo Prieto)";

        doc["current_l1"] = currentL1;
        doc["current_l2"] = currentL2;
        doc["current_l3"] = currentL3;
        doc["current_total"] = currentTotal;

        doc["temp_ambient"] = ambientTemp;
        doc["temp_water"] = waterTemp;

        doc["pressure_in"] = pressureIn;
        doc["pressure_out"] = pressureOut;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        rebootRequested = true;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(128);
        DeserializationError jsonError = deserializeJson(doc, data, len);

        if (!jsonError) {
            if (doc.containsKey("command")) {
                String command = doc["command"];
                if (command == "toggle") {
                    manualOverride = true;
                    manualMotorState = !manualMotorState;
                } else if (command == "override") {
                    manualOverride = !manualOverride;
                } else if (command == "mainSwitch") {
                    mainSwitch = !mainSwitch;
                    if (mainSwitch) {
                        error = false;
                    }
                }

                notifyClients();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    // Debug page route - serve debug.html or fallback to simple debug info
    server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/debug.html")) {
            request->send(SD, "/debug.html", "text/html");
        } else {
            String html = "<html><body><h1>SD Card Files:</h1><ul>";

            File root = SD.open("/");
            if (root) {
                File file = root.openNextFile();
                while (file) {
                    html += "<li>" + String(file.name()) + " (" + String(file.size()) + " bytes)</li>";
                    file = root.openNextFile();
                }
                root.close();
            } else {
                html += "<li>Could not open root directory</li>";
            }

            html += "</ul>";
            html += "<h2>Current Config:</h2><pre>" + getConfigJson() + "</pre>";
            html += "<h2>Device Info:</h2><pre>";
            html += "Device Name: " + String(DEVICE_NAME) + "\n";
            html += "Device ID: " + String(DEVICE_ID) + "\n";
            html += "Device Version: " + String(DEVICE_VERSION) + "\n";
            html += "Device Model: " + String(DEVICE_MODEL) + "\n";
            html += "ADC Type: MCP3208 (Rodolfo Prieto)\n";
            html += "Current Sensors: ACS712-05B (3-Phase)\n";
            html += "</pre>";
            html += "<h2>Live Readings:</h2><pre>";
            html += "Ambient Temp: " + String(ambientTemp) + " °C\n";
            html += "Water Temp: " + String(waterTemp) + " °C\n";
            html += "Pressure In: " + String(pressureIn) + " bar\n";
            html += "Pressure Out: " + String(pressureOut) + " bar\n";
            html += "Current L1: " + String(currentL1) + " A\n";
            html += "Current L2: " + String(currentL2) + " A\n";
            html += "Current L3: " + String(currentL3) + " A\n";
            html += "Total Current: " + String(currentTotal) + " A\n";
            html += "</pre>";
            html += "</body></html>";
            request->send(200, "text/html", html);
        }
    });

    events.onConnect([](AsyncEventSourceClient *client) {
        Serial.println("Client connected to /events");
        // Do NOT call publishDebugData/publishDiagnostics/notifyClients here.
        // This callback runs in the async_tcp task, which shares the SPI bus
        // with loop(). Calling SPI-heavy functions (SD directory scan, ADC reads)
        // blocks the async_tcp task on the SPI mutex and trips the task watchdog.
        // Scheduled sends from loop() deliver the first update within 1-5 seconds.
    });
}
