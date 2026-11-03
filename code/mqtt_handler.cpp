#include "mqtt_handler.h"
#include "config_manager.h"
#include "update_manager.h"
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
static char mqttTopicUpdateState[64];
static char mqttTopicUpdateInstall[64];
static char mqttTopicUpdateCheck[64];

// "online" / "offline", retained. The broker publishes "offline" itself (the
// last will) when the board stops answering, and every discovered entity points
// at this topic — so Home Assistant marks a dead unit unavailable instead of
// showing its last retained state, a running pump included, indefinitely.
static char mqttTopicAvailability[64];

// Broker session id. Built from DEVICE_ID plus the last three MAC bytes so it is
// unique per physical board: the broker drops an existing session whenever a new
// one connects with the same client id, so two boards sharing an id knock each
// other offline in turn, every reconnect interval, indefinitely.
static char mqttClientId[48];

void setupMQTT() {
    snprintf(mqttTopicMotor,    sizeof(mqttTopicMotor),    "homeassistant/%s/motor/set",    DEVICE_ID);
    snprintf(mqttTopicOverride, sizeof(mqttTopicOverride),  "homeassistant/%s/override/set", DEVICE_ID);
    snprintf(mqttTopicMain,     sizeof(mqttTopicMain),      "homeassistant/%s/main/set",     DEVICE_ID);
    snprintf(mqttTopicError,    sizeof(mqttTopicError),     "homeassistant/%s/error/set",    DEVICE_ID);
    snprintf(mqttTopicReboot,   sizeof(mqttTopicReboot),    "homeassistant/%s/reboot/set",   DEVICE_ID);
    snprintf(mqttTopicState,    sizeof(mqttTopicState),     "homeassistant/%s/state",        DEVICE_ID);
    snprintf(mqttTopicAvailability, sizeof(mqttTopicAvailability), "homeassistant/%s/availability", DEVICE_ID);
    snprintf(mqttTopicUpdateState,   sizeof(mqttTopicUpdateState),   "homeassistant/%s/update/state",   DEVICE_ID);
    snprintf(mqttTopicUpdateInstall, sizeof(mqttTopicUpdateInstall), "homeassistant/%s/update/install", DEVICE_ID);
    snprintf(mqttTopicUpdateCheck,   sizeof(mqttTopicUpdateCheck),   "homeassistant/%s/update/check",   DEVICE_ID);

    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(mqttClientId, sizeof(mqttClientId), "%s-%02x%02x%02x", DEVICE_ID, mac[3], mac[4], mac[5]);
    Serial.printf("MQTT client id: %s\n", mqttClientId);

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

    // A Monitor unit never subscribes to the motor topics, but guard anyway so a
    // retained command left on the broker cannot resurrect motor control.
    bool controller = isControllerDevice();

    if (controller && strcmp(topic, mqttTopicMotor) == 0) {
        manualMotorState = (strcmp(message, "ON") == 0);
        Serial.printf("Manual motor: %s\n", manualMotorState ? "ON" : "OFF");
    } else if (controller && strcmp(topic, mqttTopicOverride) == 0) {
        manualOverride = (strcmp(message, "ON") == 0);
        Serial.printf("Manual override: %s\n", manualOverride ? "ON" : "OFF");
    } else if (controller && strcmp(topic, mqttTopicMain) == 0) {
        mainSwitch = (strcmp(message, "ON") == 0);
        Serial.printf("Main switch: %s\n", mainSwitch ? "ON" : "OFF");
    } else if (controller && strcmp(topic, mqttTopicError) == 0) {
        error = (strcmp(message, "ON") == 0);
        if (!error) motorFault = false;   // clearing the error clears a latched electrical fault
        Serial.printf("Error state: %s\n", error ? "ON" : "OFF");
    } else if (strcmp(topic, mqttTopicReboot) == 0 && strcmp(message, "PRESS") == 0) {
        Serial.println("Reboot requested via MQTT");
        rebootRequested = true;
    } else if (strcmp(topic, mqttTopicUpdateInstall) == 0 && strcmp(message, "install") == 0) {
        // "Install" pressed on the update entity in Home Assistant
        String why;
        if (updateRequestInstall(why)) Serial.println("Firmware update accepted via MQTT");
        else                           Serial.printf("Firmware update refused: %s\n", why.c_str());
        updateStatePending = true;
    } else if (strcmp(topic, mqttTopicUpdateCheck) == 0 && strcmp(message, "PRESS") == 0) {
        updateRequestCheck();
    }

    publishState();
}

bool reconnectMQTT() {
    // Last will: retained "offline" on the availability topic, sent by the broker
    // once the keepalive lapses (~1.5 x 15 s) without a clean disconnect
    if (mqttClient.connect(mqttClientId, config.mqtt_user.c_str(), config.mqtt_password.c_str(),
                           mqttTopicAvailability, 0, true, "offline")) {
        Serial.println("MQTT connected");
        mqttClient.publish(mqttTopicAvailability, "online", true);

        if (isControllerDevice()) {
            mqttClient.subscribe(mqttTopicMotor);
            mqttClient.subscribe(mqttTopicOverride);
            mqttClient.subscribe(mqttTopicMain);
            mqttClient.subscribe(mqttTopicError);
        }
        mqttClient.subscribe(mqttTopicReboot);
        mqttClient.subscribe(mqttTopicUpdateInstall);
        mqttClient.subscribe(mqttTopicUpdateCheck);

        Serial.println("Subscribed to command topics");

        sendAutoDiscoveryConfigs();
        publishUpdateState();
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
    if (config.dual_pressure_sensors) {
        doc["pressure_out"] = pressureOut;
        doc["filter_dp"]    = pressureIn - pressureOut;
        if (config.filter_alarm_enabled) {
            doc["filter_clogged"] = strcmp(filterStatus(), "clogged") == 0 ? "ON" : "OFF";
        }
    }
    if (isControllerDevice()) {
        doc["motor"]        = motor ? "ON" : "OFF";
        doc["override"]     = manualOverride ? "ON" : "OFF";
        doc["main"]         = mainSwitch ? "ON" : "OFF";
    }
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

// State of the Home Assistant `update` entity: installed and latest version,
// and the progress while an update installs
void publishUpdateState() {
    if (!mqttClient.connected()) return;

    StaticJsonDocument<384> doc;
    updateMqttStateJson(doc);
    char payload[384];
    serializeJson(doc, payload, sizeof(payload));
    mqttClient.publish(mqttTopicUpdateState, payload, true);
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
                if (updateStatePending) {
                    updateStatePending = false;
                    publishUpdateState();
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
    // Second pressure sensor (after the filter): its reading, the drop across
    // the filter and, when enabled, the clogged-filter alarm
    if (config.dual_pressure_sensors) {
        createSensorConfig("pressure_out", "Pressure After Filter", "bar", "pressure", "mdi:gauge",  deviceDoc, stateTopic, "pressure_out");
        createSensorConfig("filter_dp",    "Filter Pressure Drop",  "bar", "pressure", "mdi:filter", deviceDoc, stateTopic, "filter_dp");
    } else {
        removeDiscoveryConfig("sensor", "pressure_out");
        removeDiscoveryConfig("sensor", "filter_dp");
    }
    if (config.dual_pressure_sensors && config.filter_alarm_enabled) {
        createBinarySensorConfig("filter_clogged", "Filter Clogged", "problem", "mdi:filter-remove",
                                 deviceDoc, stateTopic, "filter_clogged");
    } else {
        removeDiscoveryConfig("binary_sensor", "filter_clogged");
    }
    createSensorConfig("temperature", "Temperature",   "°C",    "temperature",       "mdi:thermometer",   deviceDoc, stateTopic, "temperature");
    createSensorConfig("flow",        "Flow Rate",     "L/min", nullptr,             "mdi:water",         deviceDoc, stateTopic, "flow");
    createSensorConfig("uptime",      "Uptime",        "s",     "duration",          "mdi:clock-outline", deviceDoc, stateTopic, "uptime");
    createSensorConfig("heap",        "Free Memory",   "bytes", nullptr,             "mdi:memory",        deviceDoc, stateTopic, "free_heap");
    createSensorConfig("wifi_strength","WiFi Signal",  "dBm",   "signal_strength",   "mdi:wifi",          deviceDoc, stateTopic, "wifi_rssi");

    // Monitor units (device_type "M") expose sensors only — no motor entities.
    if (isControllerDevice()) {
        createSwitchConfig("motor",    "Pump Motor",      "mdi:pump",          deviceDoc, stateTopic, "motor",    mqttTopicMotor);
        createSwitchConfig("main",     "Main Power",      "mdi:power",         deviceDoc, stateTopic, "main",     mqttTopicMain);
        createSwitchConfig("override", "Manual Override", "mdi:account-wrench",deviceDoc, stateTopic, "override", mqttTopicOverride);
        createSwitchConfig("error",    "System Error",    "mdi:alert",         deviceDoc, stateTopic, "error",    mqttTopicError);
        removeDiscoveryConfig("binary_sensor", "error");
    } else {
        // A Monitor still reports faults, it just cannot be commanded — so the
        // error switch becomes a read-only binary sensor.
        createBinarySensorConfig("error", "System Error", "problem", "mdi:alert",
                                 deviceDoc, stateTopic, "error");

        // Clear any switch entities left over from a previous Controller
        // configuration so Home Assistant drops them instead of showing them
        // permanently unavailable.
        removeDiscoveryConfig("switch", "motor");
        removeDiscoveryConfig("switch", "main");
        removeDiscoveryConfig("switch", "override");
        removeDiscoveryConfig("switch", "error");
    }

    createButtonConfig("reboot", "Reboot Device", "mdi:restart", deviceDoc, mqttTopicReboot);

    // Firmware updates: Home Assistant lists the unit under Settings → Updates
    // when a newer release is out, and "Install" there runs the update.
    createUpdateConfig(deviceDoc);
    createButtonConfig("update_check", "Check for Firmware Update", "mdi:update", deviceDoc, mqttTopicUpdateCheck);
}

void createUpdateConfig(JsonDocument& deviceDoc) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/update/%s_firmware/config", DEVICE_ID);

    DynamicJsonDocument doc(768);
    doc["name"]            = "Firmware";
    doc["unique_id"]       = String(DEVICE_ID) + "_firmware";
    doc["device_class"]    = "firmware";
    doc["state_topic"]     = mqttTopicUpdateState;
    doc["command_topic"]   = mqttTopicUpdateInstall;
    doc["payload_install"] = "install";
    doc["availability_topic"] = mqttTopicAvailability;
    doc["device"]          = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void createSensorConfig(const char* sensorId, const char* name, const char* unit,
                        const char* deviceClass, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/sensor/%s_%s/config", DEVICE_ID, sensorId);

    DynamicJsonDocument doc(640);
    doc["name"]        = name;
    doc["unique_id"]   = String(DEVICE_ID) + "_" + sensorId;
    doc["state_topic"] = stateTopic;
    doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
    if (unit)        doc["unit_of_measurement"] = unit;
    if (deviceClass) doc["device_class"]        = deviceClass;
    if (icon)        doc["icon"]                = icon;
    doc["availability_topic"] = mqttTopicAvailability;
    doc["device"]    = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void createSwitchConfig(const char* switchId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey, const char* commandTopic) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/switch/%s_%s/config", DEVICE_ID, switchId);

    DynamicJsonDocument doc(640);
    doc["name"]           = name;
    doc["unique_id"]      = String(DEVICE_ID) + "_" + switchId;
    doc["state_topic"]    = stateTopic;
    doc["command_topic"]  = commandTopic;
    doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
    doc["payload_on"]     = "ON";
    doc["payload_off"]    = "OFF";
    if (icon) doc["icon"] = icon;
    doc["availability_topic"] = mqttTopicAvailability;
    doc["device"]         = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void createButtonConfig(const char* buttonId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const char* commandTopic) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/button/%s_%s/config", DEVICE_ID, buttonId);

    DynamicJsonDocument doc(640);
    doc["name"]           = name;
    doc["unique_id"]      = String(DEVICE_ID) + "_" + buttonId;
    doc["command_topic"]  = commandTopic;
    doc["payload_press"]  = "PRESS";
    if (icon) doc["icon"] = icon;
    doc["availability_topic"] = mqttTopicAvailability;
    doc["device"]         = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void createBinarySensorConfig(const char* sensorId, const char* name,
                              const char* deviceClass, const char* icon,
                              JsonDocument& deviceDoc, const String& stateTopic,
                              const char* valueKey) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/binary_sensor/%s_%s/config", DEVICE_ID, sensorId);

    DynamicJsonDocument doc(640);
    doc["name"]           = name;
    doc["unique_id"]      = String(DEVICE_ID) + "_" + sensorId;
    doc["state_topic"]    = stateTopic;
    doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
    doc["payload_on"]     = "ON";
    doc["payload_off"]    = "OFF";
    if (deviceClass) doc["device_class"] = deviceClass;
    if (icon)        doc["icon"]         = icon;
    doc["availability_topic"] = mqttTopicAvailability;
    doc["device"]         = deviceDoc;

    publishDiscovery(configTopic, doc);
}

void removeDiscoveryConfig(const char* component, const char* objectId) {
    char configTopic[96];
    snprintf(configTopic, sizeof(configTopic), "homeassistant/%s/%s_%s/config",
             component, DEVICE_ID, objectId);
    // An empty retained payload deletes the discovery entry
    mqttClient.publish(configTopic, "", true);
    Serial.printf("Removed discovery: %s\n", configTopic);
}

void publishDiscovery(const String& topic, JsonDocument& config) {
    String payload;
    serializeJson(config, payload);

    Serial.printf("Publishing discovery: %s\n", topic.c_str());

    if (!mqttClient.publish(topic.c_str(), payload.c_str(), true)) {
        Serial.println("Discovery publish failed!");
    }
}
