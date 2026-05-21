#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>

// Configuration structure
struct Config {
    // WiFi settings
    String wifi_ssid;
    String wifi_password;
    
    // MQTT settings
    String mqtt_server;
    int mqtt_port;
    String mqtt_user;
    String mqtt_password;
    
    // System parameters
    float min_pressure;
    float max_pressure;
    float pressure_offset;
    float temp_offset;

    // Pressure sensor calibration
    float pressure_in_offset;
    float pressure_out_offset;
    bool pressure_calibrated;

    // Current sensor calibration
    float current_offset_l1;
    float current_offset_l2;
    float current_offset_l3;
    bool current_calibrated;

    // Protection thresholds
    float max_current;          // overcurrent trip point (A)
    float max_phase_imbalance;  // max A difference across phases

    // Data logging
    int log_interval_minutes;   // 0 = disabled

    // Schema version
    int config_version;

    // Constructor with default values
    Config() {
        wifi_ssid = "";
        wifi_password = "";
        mqtt_server = "YOUR_MQTT_IP";
        mqtt_port = 1883;
        mqtt_user = "YOUR_MQTT_USER";
        mqtt_password = "YOUR_MQTT_PASSWORD";
        min_pressure = 2.5;
        max_pressure = 3.5;
        pressure_offset = 0.0;
        temp_offset = 0.16;
        pressure_in_offset = 0.0;
        pressure_out_offset = 0.0;
        pressure_calibrated = false;
        current_offset_l1 = 0.0;
        current_offset_l2 = 0.0;
        current_offset_l3 = 0.0;
        current_calibrated = false;
        max_current = 15.0f;
        max_phase_imbalance = 3.0f;
        log_interval_minutes = 5;
        config_version = 1;
    }
};

// Global config object
extern Config config;

// Function declarations
bool loadConfig();
bool saveConfig();
void printConfig();
String getConfigJson();
bool updateConfigFromJson(const String& jsonStr);

#endif
