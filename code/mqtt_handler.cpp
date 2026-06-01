#include "mqtt_handler.h"
#include "config_manager.h"
#include "web_routes.h"
#include "ota_handler.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static char mqttTopicMotor[64];
static char mqttTopicOverride[64];
static char mqttTopicMain[64];
static char mqttTopicError[64];
static char mqttTopicReboot[64];
static char mqttTopicState[64];

void setupMQTT() {
    snprintf(mqttTopicMotor,    sizeof(mqttTopicMotor),    "homeassistant/%s/motor/set",    DEVICE_ID);
    snprintf(mqttTopicOverride, sizeof(mqttTopicOverride),  "homeassistant/%s/override/set", DEVICE_ID);
    snprintf(mqttTopicMain,     sizeof(mqttTopicMain),      "homeassistant/%s/main/set",     DEVICE_ID);
    snprintf(mqttTopicError,    sizeof(mqttTopicError),     "homeassistant/%s/error/set",    DEVICE_ID);
    snprintf(mqttTopicReboot,   sizeof(mqttTopicReboot),    "homeassistant/%s/reboot/set",   DEVICE_ID);
    snprintf(mqttTopicState,    sizeof(mqttTopicState),     "homeassistant/%s/state",        DEVICE_ID);
    mqttClient.setCallback(mqttCallback);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (length > 256) { Serial.println("MQTT message too long, ignoring"); return; }
    char message[257];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("MQTT [%s]: %s\n", topic, message);

    SttLock lock(pdMS_TO_TICKS(20));
    if (!lock.ok()) return;

    if (strcmp(topic, mqttTopicMotor) == 0) {
        systemState.manualMotorState = (strcmp(message, "ON") == 0);
    } else if (strcmp(topic, mqttTopicOverride) == 0) {
        systemState.manualOverride = (strcmp(message, "ON") == 0);
    } else if (strcmp(topic, mqttTopicMain) == 0) {
        systemState.mainSwitch = (strcmp(message, "ON") == 0);
    } else if (strcmp(topic, mqttTopicError) == 0) {
        systemState.error = (strcmp(message, "ON") == 0);
    } else if (strcmp(topic, mqttTopicReboot) == 0 && strcmp(message, "PRESS") == 0) {
        Serial.println("Reboot requested via MQTT");
        systemState.rebootRequested = true;
    }
    // publishState() is called from mqttTask after mqttClient.loop() returns
}

bool reconnectMQTT() {
    if (mqttClient.connect(DEVICE_ID, config.mqtt_user.c_str(), config.mqtt_password.c_str())) {
        Serial.println("MQTT connected");
        mqttClient.subscribe(mqttTopicMotor);
        mqttClient.subscribe(mqttTopicOverride);
        mqttClient.subscribe(mqttTopicMain);
        mqttClient.subscribe(mqttTopicError);
        mqttClient.subscribe(mqttTopicReboot);
        sendAutoDiscoveryConfigs();
        return true;
    }
    Serial.printf("MQTT connection failed, rc=%d\n", mqttClient.state());
    return false;
}

bool isMqttConfigured() {
    return !config.mqtt_server.isEmpty() &&
           config.mqtt_server != "YOUR_MQTT_IP" &&
           config.mqtt_port > 0;
}

void publishState() {
    if (!mqttClient.connected()) return;

    // Copy shared data under their mutexes
    SensorSnapshot snap = {};
    { SnapLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) snap = latestSnapshot; }

    SystemState st = {};
    { SttLock lock(pdMS_TO_TICKS(10)); if (lock.ok()) st = systemState; }

    StaticJsonDocument<512> doc;
    doc["pressure"]         = snap.pressure;
    doc["temperature"]      = snap.temperature;
    doc["flow"]             = snap.flow;
    doc["motor"]            = st.motor    ? "ON" : "OFF";
    doc["override"]         = st.manualOverride ? "ON" : "OFF";
    doc["main"]             = st.mainSwitch     ? "ON" : "OFF";
    doc["error"]            = st.error          ? "ON" : "OFF";
    doc["reboot_requested"] = st.rebootRequested;
    doc["uptime"]           = millis() / 1000;
    doc["free_heap"]        = ESP.getFreeHeap();
    doc["wifi_rssi"]        = WiFi.RSSI();

    char state[512];
    serializeJson(doc, state, sizeof(state));
    mqttClient.publish(mqttTopicState, state, true);
}

// ── mqttTask — Core 0, priority 2 ────────────────────────────────────────────
// Handles MQTT connection, incoming callbacks, state publishing, and all SSE events.
void mqttTask(void* pvParameters) {
    unsigned long lastDiag  = 0;
    unsigned long lastDebug = 0;

    for (;;) {
        // ── MQTT connection management ────────────────────────────────────
        if (wifiConnected && isMqttConfigured()) {
            if (!mqttClient.connected()) {
                if (millis() - lastMqttReconnectAttempt > 5000UL) {
                    lastMqttReconnectAttempt = millis();
                    reconnectMQTT();
                }
            } else {
                mqttClient.loop(); // runs mqttCallback() for incoming messages
            }
        }

        // ── Sensor update signal → publish MQTT state + SSE 1s update ────
        if (xSemaphoreTake(mqttPublishSem, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!OTA.isUpdating()) {
                notifyClients(); // SSE "update" event (thread-safe)
                if (wifiConnected && isMqttConfigured() && mqttClient.connected())
                    publishState();
            }
        }

        // ── SSE diagnostics every 5 s ─────────────────────────────────────
        if (!OTA.isUpdating() && millis() - lastDiag >= 5000) {
            lastDiag = millis();
            publishDiagnostics();
        }

        // ── SSE debug data every 30 s ─────────────────────────────────────
        if (!OTA.isUpdating() && millis() - lastDebug >= 30000) {
            lastDebug = millis();
            publishDebugData();
        }

        // ── Reboot check ──────────────────────────────────────────────────
        bool doReboot = false;
        {
            SttLock lock(pdMS_TO_TICKS(5));
            if (lock.ok()) doReboot = systemState.rebootRequested;
        }
        if (doReboot) {
            delay(1000);
            ESP.restart();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Home Assistant auto-discovery helpers ─────────────────────────────────────

void sendAutoDiscoveryConfigs() {
    if (!mqttClient.connected()) return;
    DynamicJsonDocument deviceDoc(256);
    deviceDoc["identifiers"]  = DEVICE_ID;
    deviceDoc["name"]         = DEVICE_NAME;
    deviceDoc["manufacturer"] = DEVICE_MANUFACTURER;
    deviceDoc["model"]        = DEVICE_MODEL;
    deviceDoc["sw_version"]   = DEVICE_VERSION;

    String stateTopic = mqttTopicState;
    createSensorConfig("pressure",     "Pressure",     "bar",   "pressure",       "mdi:gauge",         deviceDoc, stateTopic, "pressure");
    createSensorConfig("temperature",  "Temperature",  "°C",    "temperature",    "mdi:thermometer",   deviceDoc, stateTopic, "temperature");
    createSensorConfig("flow",         "Flow Rate",    "L/min", nullptr,          "mdi:water",         deviceDoc, stateTopic, "flow");
    createSensorConfig("uptime",       "Uptime",       "s",     "duration",       "mdi:clock-outline", deviceDoc, stateTopic, "uptime");
    createSensorConfig("heap",         "Free Memory",  "bytes", nullptr,          "mdi:memory",        deviceDoc, stateTopic, "free_heap");
    createSensorConfig("wifi_strength","WiFi Signal",  "dBm",   "signal_strength","mdi:wifi",          deviceDoc, stateTopic, "wifi_rssi");
    createSwitchConfig("motor",    "Pump Motor",       "mdi:pump",           deviceDoc, stateTopic, "motor",    mqttTopicMotor);
    createSwitchConfig("main",     "Main Power",       "mdi:power",          deviceDoc, stateTopic, "main",     mqttTopicMain);
    createSwitchConfig("override", "Manual Override",  "mdi:account-wrench", deviceDoc, stateTopic, "override", mqttTopicOverride);
    createSwitchConfig("error",    "System Error",     "mdi:alert",          deviceDoc, stateTopic, "error",    mqttTopicError);
    createButtonConfig("reboot",   "Reboot Device",    "mdi:restart",        deviceDoc, mqttTopicReboot);
}

void createSensorConfig(const char* sensorId, const char* name, const char* unit,
                        const char* deviceClass, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/sensor/%s_%s/config", DEVICE_ID, sensorId);
    DynamicJsonDocument doc(512);
    doc["name"]           = name;
    doc["unique_id"]      = String(DEVICE_ID) + "_" + sensorId;
    doc["state_topic"]    = stateTopic;
    doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
    if (unit)        doc["unit_of_measurement"] = unit;
    if (deviceClass) doc["device_class"]        = deviceClass;
    if (icon)        doc["icon"]                = icon;
    doc["device"] = deviceDoc;
    publishDiscovery(configTopic, doc);
}

void createSwitchConfig(const char* switchId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey, const char* commandTopic) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/switch/%s_%s/config", DEVICE_ID, switchId);
    DynamicJsonDocument doc(512);
    doc["name"]           = name;
    doc["unique_id"]      = String(DEVICE_ID) + "_" + switchId;
    doc["state_topic"]    = stateTopic;
    doc["command_topic"]  = commandTopic;
    doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
    doc["payload_on"]     = "ON";
    doc["payload_off"]    = "OFF";
    if (icon) doc["icon"] = icon;
    doc["device"] = deviceDoc;
    publishDiscovery(configTopic, doc);
}

void createButtonConfig(const char* buttonId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const char* commandTopic) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/button/%s_%s/config", DEVICE_ID, buttonId);
    DynamicJsonDocument doc(512);
    doc["name"]          = name;
    doc["unique_id"]     = String(DEVICE_ID) + "_" + buttonId;
    doc["command_topic"] = commandTopic;
    doc["payload_press"] = "PRESS";
    if (icon) doc["icon"] = icon;
    doc["device"] = deviceDoc;
    publishDiscovery(configTopic, doc);
}

void publishDiscovery(const String& topic, JsonDocument& cfg) {
    String payload;
    serializeJson(cfg, payload);
    if (!mqttClient.publish(topic.c_str(), payload.c_str(), true))
        Serial.println("Discovery publish failed!");
}
