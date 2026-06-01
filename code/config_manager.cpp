#include "config_manager.h"
#include "freertos_glue.h"

Config config;

static const char* CONFIG_FILE = "/config.json";
static const char* CONFIG_TMP  = "/config.tmp";

bool loadConfig() {
    // Phase 1: check existence and recover interrupted save
    bool needsDefault = false;
    {
        SPILock lock;
        if (!lock.ok()) return false;
        if (!SD.exists(CONFIG_FILE) && SD.exists(CONFIG_TMP)) {
            Serial.println("[Config] Recovering from config.tmp");
            SD.rename(CONFIG_TMP, CONFIG_FILE);
        }
        needsDefault = !SD.exists(CONFIG_FILE);
    } // lock released here — saveConfig() can now take it without deadlock

    if (needsDefault) {
        Serial.println("Config file not found, creating with defaults");
        return saveConfig();
    }

    // Phase 2: read file under a fresh lock
    SPILock lock;
    if (!lock.ok()) return false;

    File configFile = SD.open(CONFIG_FILE);
    if (!configFile) { Serial.println("Failed to open config file"); return false; }

    size_t size = configFile.size();
    if (size > 2048) { Serial.println("Config file too large"); configFile.close(); return false; }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, configFile);
    configFile.close();

    if (err) { Serial.printf("Failed to parse config: %s\n", err.c_str()); return false; }

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

    config.current_offset_l1  = doc["current_offset_l1"]  | config.current_offset_l1;
    config.current_offset_l2  = doc["current_offset_l2"]  | config.current_offset_l2;
    config.current_offset_l3  = doc["current_offset_l3"]  | config.current_offset_l3;
    config.current_calibrated = doc["current_calibrated"] | config.current_calibrated;

    config.max_current          = doc["max_current"]          | config.max_current;
    config.max_phase_imbalance  = doc["max_phase_imbalance"]  | config.max_phase_imbalance;
    config.log_interval_minutes = doc["log_interval_minutes"] | config.log_interval_minutes;
    config.config_version       = doc["config_version"]       | 1;

    if (config.config_version != 1)
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
    doc["current_offset_l1"]  = config.current_offset_l1;
    doc["current_offset_l2"]  = config.current_offset_l2;
    doc["current_offset_l3"]  = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;
    doc["max_current"]          = config.max_current;
    doc["max_phase_imbalance"]  = config.max_phase_imbalance;
    doc["log_interval_minutes"] = config.log_interval_minutes;
    doc["config_version"]       = config.config_version;

    SPILock lock;
    if (!lock.ok()) return false;

    if (SD.exists(CONFIG_TMP)) SD.remove(CONFIG_TMP);
    File tmpFile = SD.open(CONFIG_TMP, FILE_WRITE);
    if (!tmpFile) { Serial.println("Failed to create temp config file"); return false; }

    if (serializeJsonPretty(doc, tmpFile) == 0) {
        Serial.println("Failed to write config");
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
    Serial.println("MQTT Server: " + config.mqtt_server + ":" + String(config.mqtt_port));
    Serial.printf("Pressure: %.2f – %.2f bar\n", config.min_pressure, config.max_pressure);
    Serial.printf("Max Current: %.1f A  Phase Imbalance: %.1f A\n",
                  config.max_current, config.max_phase_imbalance);
    Serial.printf("Log interval: %d min\n", config.log_interval_minutes);
    Serial.println("===========================");
}

String getConfigJson() {
    DynamicJsonDocument doc(2048);
    doc["wifi_ssid"]   = config.wifi_ssid;
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
    doc["current_offset_l1"]  = config.current_offset_l1;
    doc["current_offset_l2"]  = config.current_offset_l2;
    doc["current_offset_l3"]  = config.current_offset_l3;
    doc["current_calibrated"] = config.current_calibrated;
    doc["max_current"]          = config.max_current;
    doc["max_phase_imbalance"]  = config.max_phase_imbalance;
    doc["log_interval_minutes"] = config.log_interval_minutes;

    String output;
    serializeJson(doc, output);
    return output;
}

bool updateConfigFromJson(const String& jsonStr) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, jsonStr)) return false;

    // Update config struct fields — CfgLock not needed here because this is always
    // called from async_tcp (single writer), and saveConfig() below re-serializes atomically.
    if (doc.containsKey("wifi_ssid"))     config.wifi_ssid     = doc["wifi_ssid"].as<String>();
    if (doc.containsKey("wifi_password") && doc["wifi_password"].as<String>().length() > 0)
        config.wifi_password = doc["wifi_password"].as<String>();
    if (doc.containsKey("mqtt_server"))   config.mqtt_server   = doc["mqtt_server"].as<String>();
    if (doc.containsKey("mqtt_port"))     config.mqtt_port     = doc["mqtt_port"];
    if (doc.containsKey("mqtt_user"))     config.mqtt_user     = doc["mqtt_user"].as<String>();
    if (doc.containsKey("mqtt_password") && doc["mqtt_password"].as<String>().length() > 0)
        config.mqtt_password = doc["mqtt_password"].as<String>();

    if (doc.containsKey("min_pressure"))    config.min_pressure    = doc["min_pressure"];
    if (doc.containsKey("max_pressure"))    config.max_pressure    = doc["max_pressure"];
    if (doc.containsKey("pressure_offset")) config.pressure_offset = doc["pressure_offset"];
    if (doc.containsKey("temp_offset"))     config.temp_offset     = doc["temp_offset"];
    if (doc.containsKey("pressure_in_offset"))  config.pressure_in_offset  = doc["pressure_in_offset"];
    if (doc.containsKey("pressure_out_offset")) config.pressure_out_offset = doc["pressure_out_offset"];
    if (doc.containsKey("pressure_calibrated")) config.pressure_calibrated = doc["pressure_calibrated"];
    if (doc.containsKey("current_offset_l1"))   config.current_offset_l1   = doc["current_offset_l1"];
    if (doc.containsKey("current_offset_l2"))   config.current_offset_l2   = doc["current_offset_l2"];
    if (doc.containsKey("current_offset_l3"))   config.current_offset_l3   = doc["current_offset_l3"];
    if (doc.containsKey("current_calibrated"))  config.current_calibrated  = doc["current_calibrated"];
    if (doc.containsKey("max_current"))          config.max_current          = doc["max_current"];
    if (doc.containsKey("max_phase_imbalance"))  config.max_phase_imbalance  = doc["max_phase_imbalance"];
    if (doc.containsKey("log_interval_minutes")) config.log_interval_minutes = doc["log_interval_minutes"];

    return saveConfig();
}
