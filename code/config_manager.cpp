#include "config_manager.h"

// Global config instance
Config config;

const char* CONFIG_FILE = "/config.json";

bool loadConfig() {
    // Check if config file exists
    if (!SD.exists(CONFIG_FILE)) {
        Serial.println("Config file not found, creating with defaults");
        return saveConfig(); // Save default config
    }
    
    // Open config file
    File configFile = SD.open(CONFIG_FILE);
    if (!configFile) {
        Serial.println("Failed to open config file");
        return false;
    }
    
    // Parse JSON
    size_t size = configFile.size();
    if (size > 2048) {
        Serial.println("Config file too large");
        configFile.close();
        return false;
    }
    
    DynamicJsonDocument doc(1536);  // Increased size for calibration data
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        Serial.print("Failed to parse config file: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Load values from JSON
    config.wifi_ssid = doc["wifi_ssid"] | config.wifi_ssid;
    config.wifi_password = doc["wifi_password"] | config.wifi_password;
    
    config.mqtt_server = doc["mqtt_server"] | config.mqtt_server;
    config.mqtt_port = doc["mqtt_port"] | config.mqtt_port;
    config.mqtt_user = doc["mqtt_user"] | config.mqtt_user;
    config.mqtt_password = doc["mqtt_password"] | config.mqtt_password;
    
    config.min_pressure = doc["min_pressure"] | config.min_pressure;
    config.max_pressure = doc["max_pressure"] | config.max_pressure;
    config.pressure_offset = doc["pressure_offset"] | config.pressure_offset;
    config.temp_offset = doc["temp_offset"] | config.temp_offset;

    // Load pressure calibration data
    config.pressure_in_offset = doc["pressure_in_offset"] | config.pressure_in_offset;
    config.pressure_out_offset = doc["pressure_out_offset"] | config.pressure_out_offset;
    config.pressure_calibrated = doc["pressure_calibrated"] | config.pressure_calibrated;

    // Load current calibration data
    config.current_offset_l1 = doc["current_offset_l1"] | config.current_offset_l1;
    config.current_offset_l2 = doc["current_offset_l2"] | config.current_offset_l2;
    config.current_offset_l3 = doc["current_offset_l3"] | config.current_offset_l3;
    config.current_calibrated = doc["current_calibrated"] | config.current_calibrated;
    
    Serial.println("Config loaded successfully");
    printConfig();
    return true;
}

bool saveConfig() {
    // Remove existing file
    if (SD.exists(CONFIG_FILE)) {
        SD.remove(CONFIG_FILE);
    }
    
    // Open file for writing
    File configFile = SD.open(CONFIG_FILE, FILE_WRITE);
    if (!configFile) {
        Serial.println("Failed to create config file");
        return false;
    }
    
    // Create JSON document
    DynamicJsonDocument doc(1536);  // Increased size for calibration data
    
    // Add all config values
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_password"] = config.wifi_password;
    
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_password"] = config.mqtt_password;

    doc["min_pressure"] = config.min_pressure;
    doc["max_pressure"] = config.max_pressure;
    doc["pressure_offset"] = config.pressure_offset;
    doc["temp_offset"] = config.temp_offset;

    // Add pressure calibration data
    doc["pressure_in_offset"] = config.pressure_in_offset;
    doc["pressure_out_offset"] = config.pressure_out_offset;
    doc["pressure_calibrated"] = config.pressure_calibrated;

    // Add current calibration data
    doc["current_offset_l1"] = config.current_offset_l1;
    doc["current_offset_l2"] = config.current_offset_l2;
    doc["current_offset_l3"] = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;
    
    // Serialize JSON to file
    if (serializeJsonPretty(doc, configFile) == 0) {
        Serial.println("Failed to write config file");
        configFile.close();
        return false;
    }
    
    configFile.close();
    Serial.println("Config saved successfully");
    return true;
}

void printConfig() {
    Serial.println("=== Current Configuration ===");
    Serial.println("WiFi SSID: " + config.wifi_ssid);
    Serial.println("WiFi Password: " + String(config.wifi_password.length()) + " chars");
    Serial.println("MQTT Server: " + config.mqtt_server);
    Serial.println("MQTT Port: " + String(config.mqtt_port));
    Serial.println("MQTT User: " + config.mqtt_user);
    Serial.println("MQTT Password: " + String(config.mqtt_password.length()) + " chars");
    Serial.println("Min Pressure: " + String(config.min_pressure));
    Serial.println("Max Pressure: " + String(config.max_pressure));
    Serial.println("Pressure Offset: " + String(config.pressure_offset));
    Serial.println("Temp Offset: " + String(config.temp_offset));
    Serial.println("Current Offset L1: " + String(config.current_offset_l1));
    Serial.println("Current Offset L2: " + String(config.current_offset_l2));
    Serial.println("Current Offset L3: " + String(config.current_offset_l3));
    Serial.println("Current Calibrated: " + String(config.current_calibrated ? "Yes" : "No"));
    Serial.println("===========================");
}

String getConfigJson() {
    DynamicJsonDocument doc(1536);
    
    // Add non-sensitive config values
    doc["wifi_ssid"] = config.wifi_ssid;
    // Don't send passwords
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;

    doc["min_pressure"] = config.min_pressure;
    doc["max_pressure"] = config.max_pressure;
    doc["pressure_offset"] = config.pressure_offset;
    doc["temp_offset"] = config.temp_offset;

    // Add pressure calibration data
    doc["pressure_in_offset"] = config.pressure_in_offset;
    doc["pressure_out_offset"] = config.pressure_out_offset;
    doc["pressure_calibrated"] = config.pressure_calibrated;

    // Add current calibration data
    doc["current_offset_l1"] = config.current_offset_l1;
    doc["current_offset_l2"] = config.current_offset_l2;
    doc["current_offset_l3"] = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;
    
    String output;
    serializeJson(doc, output);
    return output;
}

bool updateConfigFromJson(const String& jsonStr) {
    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        Serial.print("Failed to parse config update: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Update only provided fields
    if (doc.containsKey("wifi_ssid")) {
        config.wifi_ssid = doc["wifi_ssid"].as<String>();
    }
    if (doc.containsKey("wifi_password") && doc["wifi_password"].as<String>().length() > 0) {
        config.wifi_password = doc["wifi_password"].as<String>();
    }
    
    if (doc.containsKey("mqtt_server")) {
        config.mqtt_server = doc["mqtt_server"].as<String>();
    }
    if (doc.containsKey("mqtt_port")) {
        config.mqtt_port = doc["mqtt_port"];
    }
    if (doc.containsKey("mqtt_user")) {
        config.mqtt_user = doc["mqtt_user"].as<String>();
    }
    if (doc.containsKey("mqtt_password") && doc["mqtt_password"].as<String>().length() > 0) {
        config.mqtt_password = doc["mqtt_password"].as<String>();
    }
    
    if (doc.containsKey("min_pressure")) {
        config.min_pressure = doc["min_pressure"];
    }
    if (doc.containsKey("max_pressure")) {
        config.max_pressure = doc["max_pressure"];
    }
    if (doc.containsKey("pressure_offset")) {
        config.pressure_offset = doc["pressure_offset"];
    }
    if (doc.containsKey("temp_offset")) {
        config.temp_offset = doc["temp_offset"];
    }

    // Update pressure calibration data
    if (doc.containsKey("pressure_in_offset")) {
        config.pressure_in_offset = doc["pressure_in_offset"];
    }
    if (doc.containsKey("pressure_out_offset")) {
        config.pressure_out_offset = doc["pressure_out_offset"];
    }
    if (doc.containsKey("pressure_calibrated")) {
        config.pressure_calibrated = doc["pressure_calibrated"];
    }

    // Update current calibration data
    if (doc.containsKey("current_offset_l1")) {
        config.current_offset_l1 = doc["current_offset_l1"];
    }
    if (doc.containsKey("current_offset_l2")) {
        config.current_offset_l2 = doc["current_offset_l2"];
    }
    if (doc.containsKey("current_offset_l3")) {
        config.current_offset_l3 = doc["current_offset_l3"];
    }
    if (doc.containsKey("current_calibrated")) {
        config.current_calibrated = doc["current_calibrated"];
    }
    
    return saveConfig();
}
