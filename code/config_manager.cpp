#include "config_manager.h"

// Global config instance
Config config;

const char* CONFIG_FILE = "/config.json";
static const char* CONFIG_TMP  = "/config.tmp";

String normalizeConnectionSize(const String& raw) {
    String s = raw;
    s.trim();
    s.replace("\"", "");
    s.replace("'", "");
    s.replace(" ", "");
    s.toLowerCase();
    s.replace("inch", "");
    s.replace("in", "");
    // Anything that is not explicitly the 1" variant falls back to the 3/4" default
    if (s == "1" || s == "1.0" || s == "1.00") return "1";
    return "3/4";
}

String normalizeDeviceType(const String& raw) {
    String s = raw;
    s.trim();
    s.toUpperCase();
    // Only an explicit Monitor marker disables motor control; everything else
    // stays a Controller so an unreadable value never silently drops the pump.
    if (s.startsWith("M")) return "M";
    return "C";
}

float flowKFactor() {
    // An explicit override always wins, so a sensor whose body is printed with
    // a different constant can be calibrated without new firmware.
    if (config.flow_k_factor > 0.0f) return config.flow_k_factor;
    return config.connection_size == "1" ? FLOW_K_ONE_INCH
                                         : FLOW_K_THREE_QUARTER;
}

float flowKOffset() {
    // The offset is only meaningful alongside the factor it was measured with,
    // so it follows whichever of the two sources flowKFactor() picked.
    if (config.flow_k_factor > 0.0f) return config.flow_k_offset;
    return config.connection_size == "1" ? FLOW_K_OFFSET_ONE_INCH
                                         : FLOW_K_OFFSET_THREE_QUARTER;
}

bool isControllerDevice() {
    return config.device_type != "M";
}

bool loadConfig() {
    // Recover an interrupted atomic save: if the main file is missing but the
    // temp file exists, the device was reset between SD.remove() and SD.rename().
    // Complete the rename now so the saved data is not lost.
    if (!SD.exists(CONFIG_FILE) && SD.exists(CONFIG_TMP)) {
        Serial.println("[Config] Recovering interrupted save from config.tmp");
        SD.rename(CONFIG_TMP, CONFIG_FILE);
    }

    if (!SD.exists(CONFIG_FILE)) {
        Serial.println("Config file not found, creating with defaults");
        return saveConfig();
    }

    File configFile = SD.open(CONFIG_FILE);
    if (!configFile) {
        Serial.println("Failed to open config file");
        return false;
    }

    size_t size = configFile.size();
    if (size > 2048) {
        Serial.println("Config file too large");
        configFile.close();
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.print("Failed to parse config file: ");
        Serial.println(error.c_str());
        return false;
    }

    config.wifi_ssid     = doc["wifi_ssid"]     | config.wifi_ssid;
    config.wifi_password = doc["wifi_password"] | config.wifi_password;

    config.mqtt_server   = doc["mqtt_server"]   | config.mqtt_server;
    config.mqtt_port     = doc["mqtt_port"]     | config.mqtt_port;
    config.mqtt_user     = doc["mqtt_user"]     | config.mqtt_user;
    config.mqtt_password = doc["mqtt_password"] | config.mqtt_password;

    config.min_pressure    = doc["min_pressure"]    | config.min_pressure;
    config.max_pressure    = doc["max_pressure"]    | config.max_pressure;
    config.pressure_offset = doc["pressure_offset"] | config.pressure_offset;
    config.temp_offset     = doc["temp_offset"]     | config.temp_offset;

    config.pressure_in_offset  = doc["pressure_in_offset"]  | config.pressure_in_offset;
    config.pressure_out_offset = doc["pressure_out_offset"] | config.pressure_out_offset;
    config.pressure_calibrated = doc["pressure_calibrated"] | config.pressure_calibrated;

    config.current_offset_l1 = doc["current_offset_l1"] | config.current_offset_l1;
    config.current_offset_l2 = doc["current_offset_l2"] | config.current_offset_l2;
    config.current_offset_l3 = doc["current_offset_l3"] | config.current_offset_l3;
    config.current_calibrated = doc["current_calibrated"] | config.current_calibrated;

    config.max_current          = doc["max_current"]          | config.max_current;
    config.max_phase_imbalance  = doc["max_phase_imbalance"]  | config.max_phase_imbalance;
    config.log_interval_minutes = doc["log_interval_minutes"] | config.log_interval_minutes;

    // Missing keys keep the constructor defaults: 3/4" connection, Controller unit
    config.connection_size = normalizeConnectionSize(doc["connection_size"] | config.connection_size);
    config.device_type     = normalizeDeviceType(doc["device_type"]         | config.device_type);

    config.flow_k_factor    = doc["flow_k_factor"]    | config.flow_k_factor;
    config.flow_k_offset    = doc["flow_k_offset"]    | config.flow_k_offset;
    config.flow_debounce_us = doc["flow_debounce_us"] | config.flow_debounce_us;
    config.max_flow_lpm     = doc["max_flow_lpm"]     | config.max_flow_lpm;

    config.config_version       = doc["config_version"]       | 1;

    if (config.config_version > 2)
        Serial.printf("[Config] Warning: unexpected config version %d\n", config.config_version);

    Serial.println("Config loaded successfully");
    printConfig();
    return true;
}

bool saveConfig() {
    DynamicJsonDocument doc(2048);

    doc["wifi_ssid"]     = config.wifi_ssid;
    doc["wifi_password"] = config.wifi_password;

    doc["mqtt_server"]   = config.mqtt_server;
    doc["mqtt_port"]     = config.mqtt_port;
    doc["mqtt_user"]     = config.mqtt_user;
    doc["mqtt_password"] = config.mqtt_password;

    doc["min_pressure"]    = config.min_pressure;
    doc["max_pressure"]    = config.max_pressure;
    doc["pressure_offset"] = config.pressure_offset;
    doc["temp_offset"]     = config.temp_offset;

    doc["pressure_in_offset"]  = config.pressure_in_offset;
    doc["pressure_out_offset"] = config.pressure_out_offset;
    doc["pressure_calibrated"] = config.pressure_calibrated;

    doc["current_offset_l1"] = config.current_offset_l1;
    doc["current_offset_l2"] = config.current_offset_l2;
    doc["current_offset_l3"] = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;

    doc["max_current"]          = config.max_current;
    doc["max_phase_imbalance"]  = config.max_phase_imbalance;
    doc["log_interval_minutes"] = config.log_interval_minutes;

    doc["connection_size"] = config.connection_size;
    doc["device_type"]     = config.device_type;

    doc["flow_k_factor"]    = config.flow_k_factor;
    doc["flow_k_offset"]    = config.flow_k_offset;
    doc["flow_debounce_us"] = config.flow_debounce_us;
    doc["max_flow_lpm"]     = config.max_flow_lpm;

    doc["config_version"]       = config.config_version;

    // Write to temp file first; rename on success (atomic swap)
    if (SD.exists(CONFIG_TMP)) SD.remove(CONFIG_TMP);
    File tmpFile = SD.open(CONFIG_TMP, FILE_WRITE);
    if (!tmpFile) {
        Serial.println("Failed to create temp config file");
        return false;
    }

    if (serializeJsonPretty(doc, tmpFile) == 0) {
        Serial.println("Failed to write config file");
        tmpFile.close();
        SD.remove(CONFIG_TMP);
        return false;
    }
    tmpFile.close();

    if (SD.exists(CONFIG_FILE)) SD.remove(CONFIG_FILE);
    SD.rename(CONFIG_TMP, CONFIG_FILE);

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
    Serial.println("Max Current: " + String(config.max_current) + " A");
    Serial.println("Max Phase Imbalance: " + String(config.max_phase_imbalance) + " A");
    Serial.println("Log Interval: " + String(config.log_interval_minutes) + " min");
    Serial.println("Connection Size: " + config.connection_size + "\"");
    Serial.println("Flow calibration: F = " + String(flowKFactor(), 2) + " * Q "
                   + (flowKOffset() < 0 ? "- " : "+ ") + String(fabsf(flowKOffset()), 2)
                   + (config.flow_k_factor > 0.0f ? "  (config override)" : "  (default for size)"));
    Serial.println("Flow debounce: " + String(config.flow_debounce_us) + " us"
                   + "  (ceiling " + String((1000000.0f / config.flow_debounce_us - flowKOffset()) / flowKFactor(), 0) + " L/min)");
    Serial.println("Max plausible flow: " + String(config.max_flow_lpm, 1) + " L/min");
    Serial.println("Device Type: " + config.device_type + (isControllerDevice() ? " (Controller)" : " (Monitor)"));
    Serial.println("===========================");
}

String getConfigJson() {
    DynamicJsonDocument doc(2048);

    doc["wifi_ssid"]   = config.wifi_ssid;
    // passwords not sent
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"]   = config.mqtt_port;
    doc["mqtt_user"]   = config.mqtt_user;

    doc["min_pressure"]    = config.min_pressure;
    doc["max_pressure"]    = config.max_pressure;
    doc["pressure_offset"] = config.pressure_offset;
    doc["temp_offset"]     = config.temp_offset;

    doc["pressure_in_offset"]  = config.pressure_in_offset;
    doc["pressure_out_offset"] = config.pressure_out_offset;
    doc["pressure_calibrated"] = config.pressure_calibrated;

    doc["current_offset_l1"] = config.current_offset_l1;
    doc["current_offset_l2"] = config.current_offset_l2;
    doc["current_offset_l3"] = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;

    doc["max_current"]          = config.max_current;
    doc["max_phase_imbalance"]  = config.max_phase_imbalance;
    doc["log_interval_minutes"] = config.log_interval_minutes;

    doc["connection_size"] = config.connection_size;
    doc["device_type"]     = config.device_type;

    doc["flow_k_factor"]    = config.flow_k_factor;
    doc["flow_k_offset"]    = config.flow_k_offset;
    doc["flow_debounce_us"] = config.flow_debounce_us;
    doc["max_flow_lpm"]     = config.max_flow_lpm;
    // What the firmware will actually use, after defaults are resolved
    doc["flow_k_factor_effective"] = flowKFactor();
    doc["flow_k_offset_effective"] = flowKOffset();

    String output;
    serializeJson(doc, output);
    return output;
}

bool updateConfigFromJson(const String& jsonStr) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.print("Failed to parse config update: ");
        Serial.println(error.c_str());
        return false;
    }

    if (doc.containsKey("wifi_ssid"))
        config.wifi_ssid = doc["wifi_ssid"].as<String>();
    if (doc.containsKey("wifi_password") && doc["wifi_password"].as<String>().length() > 0)
        config.wifi_password = doc["wifi_password"].as<String>();

    if (doc.containsKey("mqtt_server"))
        config.mqtt_server = doc["mqtt_server"].as<String>();
    if (doc.containsKey("mqtt_port"))
        config.mqtt_port = doc["mqtt_port"];
    if (doc.containsKey("mqtt_user"))
        config.mqtt_user = doc["mqtt_user"].as<String>();
    if (doc.containsKey("mqtt_password") && doc["mqtt_password"].as<String>().length() > 0)
        config.mqtt_password = doc["mqtt_password"].as<String>();

    if (doc.containsKey("min_pressure"))    config.min_pressure    = doc["min_pressure"];
    if (doc.containsKey("max_pressure"))    config.max_pressure    = doc["max_pressure"];
    if (doc.containsKey("pressure_offset")) config.pressure_offset = doc["pressure_offset"];
    if (doc.containsKey("temp_offset"))     config.temp_offset     = doc["temp_offset"];

    if (doc.containsKey("pressure_in_offset"))  config.pressure_in_offset  = doc["pressure_in_offset"];
    if (doc.containsKey("pressure_out_offset")) config.pressure_out_offset = doc["pressure_out_offset"];
    if (doc.containsKey("pressure_calibrated")) config.pressure_calibrated = doc["pressure_calibrated"];

    if (doc.containsKey("current_offset_l1")) config.current_offset_l1 = doc["current_offset_l1"];
    if (doc.containsKey("current_offset_l2")) config.current_offset_l2 = doc["current_offset_l2"];
    if (doc.containsKey("current_offset_l3")) config.current_offset_l3 = doc["current_offset_l3"];
    if (doc.containsKey("current_calibrated")) config.current_calibrated = doc["current_calibrated"];

    if (doc.containsKey("max_current"))          config.max_current          = doc["max_current"];
    if (doc.containsKey("max_phase_imbalance"))  config.max_phase_imbalance  = doc["max_phase_imbalance"];
    if (doc.containsKey("log_interval_minutes")) config.log_interval_minutes = doc["log_interval_minutes"];

    if (doc.containsKey("connection_size"))
        config.connection_size = normalizeConnectionSize(doc["connection_size"].as<String>());
    if (doc.containsKey("device_type"))
        config.device_type = normalizeDeviceType(doc["device_type"].as<String>());

    if (doc.containsKey("flow_k_factor")) {
        float k = doc["flow_k_factor"];
        // Negative is meaningless and would invert the reading; clamp to the
        // "use the default" sentinel instead.
        config.flow_k_factor = k > 0.0f ? k : 0.0f;
    }
    if (doc.containsKey("flow_k_offset"))
        config.flow_k_offset = doc["flow_k_offset"];
    if (doc.containsKey("flow_debounce_us")) {
        long d = doc["flow_debounce_us"] | 500L;
        if (d < 0) d = 0;
        if (d > 100000L) d = 100000L;   // 10 Hz ceiling; beyond this nothing counts
        config.flow_debounce_us = (unsigned int)d;
    }
    if (doc.containsKey("max_flow_lpm")) {
        float m = doc["max_flow_lpm"];
        config.max_flow_lpm = m > 0.0f ? m : 150.0f;
    }

    return saveConfig();
}
