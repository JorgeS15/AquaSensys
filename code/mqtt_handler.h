#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "freertos_glue.h"

// Device identity from code.ino
extern const char* DEVICE_NAME;
extern const char* DEVICE_ID;
extern const char* DEVICE_MANUFACTURER;
extern const char* DEVICE_MODEL;
extern const char* DEVICE_VERSION;

// MQTT state from code.ino
extern PubSubClient  mqttClient;
extern bool          wifiConnected;
extern unsigned long lastMqttReconnectAttempt;

void setupMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool reconnectMQTT();
void publishState();
void sendAutoDiscoveryConfigs();
bool isMqttConfigured();

// FreeRTOS task — Core 0, handles MQTT + SSE notifications
void mqttTask(void* pvParameters);

void publishDiscovery(const String& topic, JsonDocument& config);
void createSensorConfig(const char* sensorId, const char* name, const char* unit,
                        const char* deviceClass, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey);
void createSwitchConfig(const char* switchId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const String& stateTopic,
                        const char* valueKey, const char* commandTopic);
void createButtonConfig(const char* buttonId, const char* name, const char* icon,
                        JsonDocument& deviceDoc, const char* commandTopic);

#endif
