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

String normalizeDeviceId(const String& raw) {
    String out;
    String s = raw;
    s.trim();
    s.toLowerCase();
    for (size_t i = 0; i < s.length() && out.length() < 32; i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
        } else if (out.length() > 0 && out[out.length() - 1] != '-') {
            out += '-';   // spaces, underscores and the like collapse to one hyphen
        }
    }
    while (out.endsWith("-")) out.remove(out.length() - 1);
    return out;
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

    config.dual_pressure_sensors = doc["dual_pressure_sensors"] | config.dual_pressure_sensors;
    config.filter_alarm_enabled  = doc["filter_alarm_enabled"]  | config.filter_alarm_enabled;
    config.filter_dp_warning     = doc["filter_dp_warning"]     | config.filter_dp_warning;
    config.filter_min_flow_lpm   = doc["filter_min_flow_lpm"]   | config.filter_min_flow_lpm;
    config.filter_alarm_delay_s  = doc["filter_alarm_delay_s"]  | config.filter_alarm_delay_s;

    config.current_offset_l1 = doc["current_offset_l1"] | config.current_offset_l1;
    config.current_offset_l2 = doc["current_offset_l2"] | config.current_offset_l2;
    config.current_offset_l3 = doc["current_offset_l3"] | config.current_offset_l3;
    config.current_calibrated = doc["current_calibrated"] | config.current_calibrated;
    // A file without the key predates v3.4.6, so its offsets are method 1
    config.current_cal_method = doc["current_cal_method"] | 1;

    config.max_current          = doc["max_current"]          | config.max_current;
    config.max_phase_imbalance  = doc["max_phase_imbalance"]  | config.max_phase_imbalance;
    config.log_interval_minutes = doc["log_interval_minutes"] | config.log_interval_minutes;
    config.update_check_hours   = constrain((int)(doc["update_check_hours"] | config.update_check_hours), 0, 720);

    // Missing keys keep the constructor defaults: 3/4" connection, Controller unit
    config.connection_size = normalizeConnectionSize(doc["connection_size"] | config.connection_size);
    config.device_type     = normalizeDeviceType(doc["device_type"]         | config.device_type);
    config.device_id       = normalizeDeviceId(doc["device_id"]             | config.device_id);
    config.device_name     = doc["device_name"]                             | config.device_name;
    config.device_name.trim();

    config.flow_k_factor    = doc["flow_k_factor"]    | config.flow_k_factor;
    config.flow_k_offset    = doc["flow_k_offset"]    | config.flow_k_offset;
    config.flow_debounce_us = doc["flow_debounce_us"] | config.flow_debounce_us;
    config.max_flow_lpm     = doc["max_flow_lpm"]     | config.max_flow_lpm;

    config.config_version       = doc["config_version"]       | 1;

    if (config.config_version > CONFIG_SCHEMA_VERSION)
        Serial.printf("[Config] Warning: config version %d is newer than this firmware's %d\n",
                      config.config_version, CONFIG_SCHEMA_VERSION);

    // Schema 4 (v3.4.10): temp_offset is applied to the water temperature for the
    // first time. Earlier firmware wrote 0.16 as its default although the value
    // was never used; carried over, it would now shift every reading by 0.16 °C
    // that nobody asked for. That exact default becomes 0 — any other value is
    // somebody's deliberate setting and is kept.
    bool migrated = false;
    if (config.config_version < 4) {
        if (fabsf(config.temp_offset - 0.16f) < 0.0005f) {
            Serial.println("[Config] temp_offset 0.16 was the unused old default; resetting to 0");
            config.temp_offset = 0.0f;
        }
        config.config_version = 4;
        migrated = true;
    }
    if (migrated) saveConfig();

    // Current-sensor offsets saved before v3.4.6 measured the sensor's DC
    // offset, which the mean-corrected RMS now removes by itself. Applied on top,
    // they would subtract 0.1-0.4 A from every reading, so they are discarded
    // once; the next calibration stores method-2 offsets.
    if (config.current_cal_method < 2) {
        if (config.current_offset_l1 != 0.0f || config.current_offset_l2 != 0.0f ||
            config.current_offset_l3 != 0.0f) {
            Serial.printf("[Config] Discarding pre-3.4.6 current offsets (%.1f, %.1f, %.1f mA)\n",
                          config.current_offset_l1, config.current_offset_l2, config.current_offset_l3);
        }
        config.current_offset_l1 = 0.0f;
        config.current_offset_l2 = 0.0f;
        config.current_offset_l3 = 0.0f;
        config.current_cal_method = 2;
        saveConfig();
    }

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
    doc["dual_pressure_sensors"] = config.dual_pressure_sensors;
    doc["filter_alarm_enabled"]  = config.filter_alarm_enabled;
    doc["filter_dp_warning"]     = config.filter_dp_warning;
    doc["filter_min_flow_lpm"]   = config.filter_min_flow_lpm;
    doc["filter_alarm_delay_s"]  = config.filter_alarm_delay_s;

    doc["current_offset_l1"] = config.current_offset_l1;
    doc["current_offset_l2"] = config.current_offset_l2;
    doc["current_offset_l3"] = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;
    doc["current_cal_method"] = config.current_cal_method;

    doc["max_current"]          = config.max_current;
    doc["max_phase_imbalance"]  = config.max_phase_imbalance;
    doc["log_interval_minutes"] = config.log_interval_minutes;
    doc["update_check_hours"]   = config.update_check_hours;

    doc["connection_size"] = config.connection_size;
    doc["device_type"]     = config.device_type;
    doc["device_id"]       = config.device_id;
    doc["device_name"]     = config.device_name;

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
    Serial.println("Water Temp Offset: " + String(config.temp_offset) + " C");
    Serial.println("Current Offset L1: " + String(config.current_offset_l1));
    Serial.println("Current Offset L2: " + String(config.current_offset_l2));
    Serial.println("Current Offset L3: " + String(config.current_offset_l3));
    Serial.println("Current Calibrated: " + String(config.current_calibrated ? "Yes" : "No"));
    Serial.println("Max Current: " + String(config.max_current) + " A");
    Serial.println("Max Phase Imbalance: " + String(config.max_phase_imbalance) + " A");
    Serial.println("Log Interval: " + String(config.log_interval_minutes) + " min");
    Serial.println("Update check: " + (config.update_check_hours > 0
                   ? "every " + String(config.update_check_hours) + " h" : String("manual only")));
    Serial.println("Connection Size: " + config.connection_size + "\"");
    Serial.println("Flow calibration: F = " + String(flowKFactor(), 2) + " * Q "
                   + (flowKOffset() < 0 ? "- " : "+ ") + String(fabsf(flowKOffset()), 2)
                   + (config.flow_k_factor > 0.0f ? "  (config override)" : "  (default for size)"));
    Serial.println("Flow debounce: " + String(config.flow_debounce_us) + " us"
                   + "  (ceiling " + String((1000000.0f / config.flow_debounce_us - flowKOffset()) / flowKFactor(), 0) + " L/min)");
    Serial.println("Max plausible flow: " + String(config.max_flow_lpm, 1) + " L/min");
    Serial.println("Device Type: " + config.device_type + (isControllerDevice() ? " (Controller)" : " (Monitor)"));
    Serial.println("Device ID: " + (config.device_id.length() ? config.device_id : String("(default for type)")));
    Serial.println("Device Name: " + (config.device_name.length() ? config.device_name : String("(default for type)")));
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
    doc["dual_pressure_sensors"] = config.dual_pressure_sensors;
    doc["filter_alarm_enabled"]  = config.filter_alarm_enabled;
    doc["filter_dp_warning"]     = config.filter_dp_warning;
    doc["filter_min_flow_lpm"]   = config.filter_min_flow_lpm;
    doc["filter_alarm_delay_s"]  = config.filter_alarm_delay_s;

    doc["current_offset_l1"] = config.current_offset_l1;
    doc["current_offset_l2"] = config.current_offset_l2;
    doc["current_offset_l3"] = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;

    doc["max_current"]          = config.max_current;
    doc["max_phase_imbalance"]  = config.max_phase_imbalance;
    doc["log_interval_minutes"] = config.log_interval_minutes;
    doc["update_check_hours"]   = config.update_check_hours;

    doc["connection_size"] = config.connection_size;
    doc["device_type"]     = config.device_type;
    doc["device_id"]       = config.device_id;
    doc["device_name"]     = config.device_name;

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

    if (doc.containsKey("dual_pressure_sensors")) config.dual_pressure_sensors = doc["dual_pressure_sensors"];
    if (doc.containsKey("filter_alarm_enabled"))  config.filter_alarm_enabled  = doc["filter_alarm_enabled"];
    if (doc.containsKey("filter_dp_warning"))     config.filter_dp_warning     = doc["filter_dp_warning"];
    if (doc.containsKey("filter_min_flow_lpm"))   config.filter_min_flow_lpm   = doc["filter_min_flow_lpm"];
    if (doc.containsKey("filter_alarm_delay_s"))  config.filter_alarm_delay_s  = doc["filter_alarm_delay_s"];

    if (doc.containsKey("current_offset_l1")) config.current_offset_l1 = doc["current_offset_l1"];
    if (doc.containsKey("current_offset_l2")) config.current_offset_l2 = doc["current_offset_l2"];
    if (doc.containsKey("current_offset_l3")) config.current_offset_l3 = doc["current_offset_l3"];
    if (doc.containsKey("current_calibrated")) config.current_calibrated = doc["current_calibrated"];

    if (doc.containsKey("max_current"))          config.max_current          = doc["max_current"];
    if (doc.containsKey("max_phase_imbalance"))  config.max_phase_imbalance  = doc["max_phase_imbalance"];
    if (doc.containsKey("log_interval_minutes")) config.log_interval_minutes = doc["log_interval_minutes"];
    if (doc.containsKey("update_check_hours"))   config.update_check_hours   = constrain(doc["update_check_hours"].as<int>(), 0, 720);

    if (doc.containsKey("connection_size"))
        config.connection_size = normalizeConnectionSize(doc["connection_size"].as<String>());
    if (doc.containsKey("device_type"))
        config.device_type = normalizeDeviceType(doc["device_type"].as<String>());
    if (doc.containsKey("device_id"))
        config.device_id = normalizeDeviceId(doc["device_id"].as<String>());
    if (doc.containsKey("device_name")) {
        config.device_name = doc["device_name"].as<String>();
        config.device_name.trim();
        if (config.device_name.length() > 48) config.device_name = config.device_name.substring(0, 48);
    }

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

// Written by the main loop only; the web and MQTT tasks just read the pointer,
// which is a single 32-bit load on the ESP32.
static const char* volatile filterState = "off";
static bool filterLatched = false;          // verdict is "clogged"
static unsigned long filterChangeSince = 0; // when the drop first disagreed with the verdict

void updateFilterState(float pressureIn, float pressureOut, float flowLpm) {
    if (!config.dual_pressure_sensors || !config.filter_alarm_enabled) {
        filterLatched = false;
        filterChangeSince = 0;
        filterState = "off";
        return;
    }
    if (isnan(pressureIn) || isnan(pressureOut) || isnan(flowLpm)) {
        filterChangeSince = 0;
        filterState = "no_reading";
        return;
    }
    // Without water moving through it the filter shows no drop, clogged or not:
    // keep the last verdict if it was clogged, and restart the timing
    if (flowLpm < config.filter_min_flow_lpm) {
        filterChangeSince = 0;
        filterState = filterLatched ? "clogged" : "no_flow";
        return;
    }

    bool dropHigh = (pressureIn - pressureOut) >= config.filter_dp_warning;
    if (dropHigh == filterLatched) {
        filterChangeSince = 0;              // reading agrees with the verdict
    } else {
        // A pressure spike (or dip) must last the whole delay to flip the verdict
        unsigned long now = millis();
        if (filterChangeSince == 0) filterChangeSince = now;
        unsigned long delayMs = (unsigned long)max(0, config.filter_alarm_delay_s) * 1000UL;
        if (now - filterChangeSince >= delayMs) {
            filterLatched = dropHigh;
            filterChangeSince = 0;
            Serial.printf("[Filter] %s (drop %.2f bar)\n", filterLatched ? "Clogged" : "Clear",
                          pressureIn - pressureOut);
        }
    }
    filterState = filterLatched ? "clogged" : "ok";
}

const char* filterStatus() {
    return filterState;
}
