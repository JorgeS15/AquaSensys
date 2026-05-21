#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// Device information - extern declarations to access from main file
extern const char* DEVICE_NAME;
extern const char* DEVICE_ID;
extern const char* DEVICE_MANUFACTURER;
extern const char* DEVICE_MODEL;
extern const char* DEVICE_VERSION;

// Extern declarations for variables needed by MQTT functions
extern float pressure;
extern float temperature;
extern float flow;
extern volatile bool motor;
extern volatile bool manualOverride;
extern volatile bool manualMotorState;
extern volatile bool mainSwitch;
extern volatile bool error;
extern volatile bool rebootRequested;

// Set true by loop() when sensor state changes; MQTT task reads and publishes
extern volatile bool publishStatePending;

// Function declarations
void setupMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool reconnectMQTT();
void publishState();
void sendAutoDiscoveryConfigs();

// Only attempt MQTT when server is configured
bool isMqttConfigured();

// FreeRTOS task — created in setup(), runs on Core 0
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