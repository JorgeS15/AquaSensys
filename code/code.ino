#include <math.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ota_handler.h"
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "mqtt_handler.h"
#include "file_manager.h"
#include "config_manager.h"
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include "MCP3208.h"
#include "ACS712_handler.h"
#include "web_routes.h"
#include "freertos_glue.h"
#include "sensor_task.h"
#include "control_task.h"
#include "wifi_task.h"

// ── Device identity ──────────────────────────────────────────────────────────
const char* DEVICE_NAME         = "AquaSensys C3";
const char* DEVICE_ID           = "aquasensys";
const char* DEVICE_MANUFACTURER = "JorgeS15";
const char* DEVICE_MODEL        = "AquaSensys C3";
const char* DEVICE_VERSION      = "3.0.37";

// ── MQTT client (WiFiClient owned here; PubSubClient uses it) ────────────────
WiFiClient    espClient;
PubSubClient  mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;

// ── Pin definitions ──────────────────────────────────────────────────────────
#define TF_CS_PIN  21
#define MCP_CS_PIN 19
#define LED_RED    32
#define LED_GREEN  23
#define MOTOR_PIN  26
#define LED_BLUE   22
#define FLOW_PIN   33
#define TEMP_PIN   35

// ── Hardware instances ───────────────────────────────────────────────────────
MCP3208 adc;

// ── Web server ───────────────────────────────────────────────────────────────
AsyncWebServer  server(80);
AsyncEventSource events("/events");

// ── SD helpers ───────────────────────────────────────────────────────────────

static void initSDCard() {
    if (!SD.begin(TF_CS_PIN)) {
        Serial.println("SD Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) { Serial.println("No SD card attached"); return; }
    Serial.printf("SD Card Type: %s  Size: %lluMB\n",
        cardType == CARD_MMC  ? "MMC"  :
        cardType == CARD_SD   ? "SDSC" :
        cardType == CARD_SDHC ? "SDHC" : "UNKNOWN",
        SD.cardSize() / (1024 * 1024));
    Serial.println("SD Card initialized successfully");
}

void loadRuntime() {
    SPILock lock;
    if (!lock.ok()) return;
    File f = SD.open("/runtime.json");
    if (!f) return;
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, f) == DeserializationError::Ok)
        motorRuntimeSeconds = doc["motor_runtime_s"] | (unsigned long)0;
    f.close();
    Serial.printf("[Runtime] Loaded motor runtime: %lu s\n", motorRuntimeSeconds);
}

// ── GPIO ─────────────────────────────────────────────────────────────────────

static void setupPins() {
    pinMode(FLOW_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, RISING);
    pinMode(MOTOR_PIN, OUTPUT);
    ledcAttachChannel(LED_RED,   5000, 8, 0);
    ledcAttachChannel(LED_GREEN, 5000, 8, 1);
    ledcAttachChannel(LED_BLUE,  5000, 8, 2);
}

static void testLEDs() {
    ledcWrite(LED_RED, 255);   delay(500); ledcWrite(LED_RED, 0);
    ledcWrite(LED_GREEN, 255); delay(500); ledcWrite(LED_GREEN, 0);
    ledcWrite(LED_BLUE, 255);  delay(500); ledcWrite(LED_BLUE, 0);
}

// ── Entry point ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== AquaSensys C3 Starting ===");
    Serial.printf("Version: %s\n", DEVICE_VERSION);
    Serial.printf("Reset reason: %d\n", esp_reset_reason());

    // Must be first: all subsequent SD/SPI code relies on these handles
    initFreeRTOSGlue();

    // SPI bus
    SPI.begin(16, 17, 18, -1);
    adc.begin(MCP_CS_PIN);
    adc.analogReadResolution(12);
    Serial.println("MCP3208 initialized");

    currentSensor.begin(&adc, 50, 5);
    currentSensor.setCalibration(0.0, 1000.0);
    Serial.println("ACS712 current sensors initialized");

    // SD + config
    {
        SPILock lock;
        initSDCard();
    }
    loadConfig();
    loadRuntime();

    // Current sensor calibration
    if (config.current_offset_l1 != 0.0f ||
        config.current_offset_l2 != 0.0f ||
        config.current_offset_l3 != 0.0f) {
        currentSensor.setIndividualOffsets(
            config.current_offset_l1,
            config.current_offset_l2,
            config.current_offset_l3);
        Serial.println("Loaded saved current sensor calibration");
    } else {
        Serial.println("No saved calibration — auto-calibrating in 3 s (motor must be OFF)");
        delay(3000);
        CalibrationData cal = {};
        bool calOk = false;
        {
            SPILock lock; // released when block exits, before saveConfig takes it
            calOk = currentSensor.performAutoCalibration(100);
            if (calOk) cal = currentSensor.getCalibrationData();
        }
        if (calOk) {
            config.current_offset_l1 = cal.offsetL1;
            config.current_offset_l2 = cal.offsetL2;
            config.current_offset_l3 = cal.offsetL3;
            saveConfig();
            Serial.println("Calibration saved");
        }
    }

    // WiFi (blocking connect during setup is acceptable — no tasks yet)
    setupWiFi();

    // OTA + MQTT + Web
    OTA.begin(&server, "/update");
    OTA.enableBackup(true, "/backup");
    mqttClient.setBufferSize(1024);
    mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
    setupMQTT();
    webRoutes();
    server.serveStatic("/", SD, "/");
    server.addHandler(&events);
    server.begin();
    Serial.println("Web server started");

    // GPIO
    setupPins();
    digitalWrite(MOTOR_PIN, LOW);
    testLEDs();

    // ── Create FreeRTOS tasks ────────────────────────────────────────────────
    // Core 1 — real-time sensing and control
    xTaskCreatePinnedToCore(sensorTask,  "sensor",  6144, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(controlTask, "control", 3072, NULL, 4, NULL, 1);

    // Core 0 — networking and I/O
    xTaskCreatePinnedToCore(mqttTask,    "mqtt",    8192, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(loggingTask, "logging", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(wifiTask,    "wifi",    4096, NULL, 1, NULL, 0);

    Serial.println("=== Setup Complete ===\n");
}

// loop() is unused — all work is in the tasks above
void loop() {
    vTaskDelay(portMAX_DELAY);
}
