#include "mqtt_handler.h"
#include "config_manager.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// MQTT client object
extern PubSubClient mqttClient;
extern bool wifiConnected;
extern unsigned long lastMqttReconnectAttempt;

volatile bool publishStatePending = false;

// Pre-built topic strings — populated once in setupMQTT(), reused everywhere
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

    mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
    mqttClient.setCallback(mqttCallback);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Build message string with length guard
    if (length > 256) {
        Serial.println("MQTT message too long, ignoring");
        return;
    }
    char message[257];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("MQTT [%s]: %s\n", topic, message);

    if (strcmp(topic, mqttTopicMotor) == 0) {
        manualMotorState = (strcmp(message, "ON") == 0);
        Serial.printf("Manual motor: %s\n", manualMotorState ? "ON" : "OFF");
    } else if (strcmp(topic, mqttTopicOverride) == 0) {
        manualOverride = (strcmp(message, "ON") == 0);
        Serial.printf("Manual override: %s\n", manualOverride ? "ON" : "OFF");
    } else if (strcmp(topic, mqttTopicMain) == 0) {
        mainSwitch = (strcmp(message, "ON") == 0);
        Serial.printf("Main switch: %s\n", mainSwitch ? "ON" : "OFF");
    } else if (strcmp(topic, mqttTopicError) == 0) {
        error = (strcmp(message, "ON") == 0);
        Serial.printf("Error state: %s\n", error ? "ON" : "OFF");
    } else if (strcmp(topic, mqttTopicReboot) == 0 && strcmp(message, "PRESS") == 0) {
        Serial.println("Reboot requested via MQTT");
        rebootRequested = true;
    }

    publishState();
}

bool reconnectMQTT() {
    if (mqttClient.connect(DEVICE_ID, config.mqtt_user.c_str(), config.mqtt_password.c_str())) {
        Serial.println("MQTT connected");

        mqttClient.subscribe(mqttTopicMotor);
        mqttClient.subscribe(mqttTopicOverride);
        mqttClient.subscribe(mqttTopicMain);
        mqttClient.subscribe(mqttTopicError);
        mqttClient.subscribe(mqttTopicReboot);

        Serial.println("Subscribed to command topics");

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

    StaticJsonDocument<512> doc;
    doc["pressure"]         = pressure;
    doc["temperature"]      = temperature;
    doc["flow"]             = flow;
    doc["motor"]            = motor ? "ON" : "OFF";
    doc["override"]         = manualOverride ? "ON" : "OFF";
    doc["main"]             = mainSwitch ? "ON" : "OFF";
    doc["error"]            = error ? "ON" : "OFF";
    doc["reboot_requested"] = rebootRequested;
    doc["uptime"]           = millis() / 1000;
    doc["free_heap"]        = ESP.getFreeHeap();
    doc["wifi_rssi"]        = WiFi.RSSI();

    char state[512];
    serializeJson(doc, state, sizeof(state));
    mqttClient.publish(mqttTopicState, state, true);

    Serial.printf("Published state: %s\n", state);
}

void mqttTask(void* pvParameters) {
    for (;;) {
        if (wifiConnected && isMqttConfigured()) {
            if (!mqttClient.connected()) {
                if (millis() - lastMqttReconnectAttempt > 5000UL) {
                    lastMqttReconnectAttempt = millis();
                    reconnectMQTT();
                }
            } else {
                mqttClient.loop();
                if (publishStatePending) {
                    publishStatePending = false;
                    publishState();
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void sendAutoDiscoveryConfigs() {
    if (!mqttClient.connected()) return;

    Serial.println("Sending auto-discovery configs...");

    DynamicJsonDocument deviceDoc(256);
    deviceDoc["identifiers"] = DEVICE_ID;
    deviceDoc["name"]        = DEVICE_NAME;
    deviceDoc["manufacturer"] = DEVICE_MANUFACTURER;
    deviceDoc["model"]       = DEVICE_MODEL;
    deviceDoc["sw_version"]  = DEVICE_VERSION;

    String stateTopic = mqttTopicState;

    createSensorConfig("pressure",    "Pressure",      "bar",   "pressure",         "mdi:gauge",         deviceDoc, stateTopic, "pressure");
    createSensorConfig("temperature", "Temperature",   "°C",    "temperature",       "mdi:thermometer",   deviceDoc, stateTopic, "temperature");
    createSensorConfig("flow",        "Flow Rate",     "L/min", nullptr,             "mdi:water",         deviceDoc, stateTopic, "flow");
    createSensorConfig("uptime",      "Uptime",        "s",     "duration",          "mdi:clock-outline", deviceDoc, stateTopic, "uptime");
    createSensorConfig("heap",        "Free Memory",   "bytes", nullptr,             "mdi:memory",        deviceDoc, stateTopic, "free_heap");
    createSensorConfig("wifi_strength","WiFi Signal",  "dBm",   "signal_strength",   "mdi:wifi",          deviceDoc, stateTopic, "wifi_rssi");

    createSwitchConfig("motor",    "Pump Motor",      "mdi:pump",          deviceDoc, stateTopic, "motor",    mqttTopicMotor);
    createSwitchConfig("main",     "Main Power",      "mdi:power",         deviceDoc, stateTopic, "main",     mqttTopicMain);
    createSwitchConfig("override", "Manual Override", "mdi:account-wrench",deviceDoc, stateTopic, "override", mqttTopicOverride);
    createSwitchConfig("error",    "System Error",    "mdi:alert",         deviceDoc, stateTopic, "error",    mqttTopicError);

    createButtonConfig("reboot", "Reboot Device", "mdi:restart", deviceDoc, mqttTopicReboot);
}

void createSensorConfig(const char* sensorId, const char* name, const char* unit,
                        const char* deviceClass, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/sensor/%s_%s/config", DEVICE_ID, sensorId);

    DynamicJsonDocument doc(512);
    doc["name"]        = name;
    doc["unique_id"]   = String(DEVICE_ID) + "_" + sensorId;
    doc["state_topic"] = stateTopic;
    doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
    if (unit)        doc["unit_of_measurement"] = unit;
    if (deviceClass) doc["device_class"]        = deviceClass;
    if (icon)        doc["icon"]                = icon;
    doc["device"]    = deviceDoc;

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
    doc["device"]         = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void createButtonConfig(const char* buttonId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const char* commandTopic) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/button/%s_%s/config", DEVICE_ID, buttonId);

    DynamicJsonDocument doc(512);
    doc["name"]           = name;
    doc["unique_id"]      = String(DEVICE_ID) + "_" + buttonId;
    doc["command_topic"]  = commandTopic;
    doc["payload_press"]  = "PRESS";
    if (icon) doc["icon"] = icon;
    doc["device"]         = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void publishDiscovery(const String& topic, JsonDocument& config) {
    String payload;
    serializeJson(config, payload);

    Serial.printf("Publishing discovery: %s\n", topic.c_str());

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), true)) {
        Serial.println("Discovery publish failed!");
    }
}
