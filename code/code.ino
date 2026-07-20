#include <math.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ota_handler.h"
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "mqtt_handler.h"
#include "file_manager.h"
#include "config_manager.h"
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include "MCP3208.h"
#include "ACS712_handler.h"
#include "web_routes.h"

// Device constants (not saved to config)
const char* DEVICE_NAME = "AquaSensys C3";
const char* DEVICE_ID = "aquasensys";
const char* DEVICE_MANUFACTURER = "JorgeS15";
const char* DEVICE_MODEL = "AquaSensys C3";
const char* DEVICE_VERSION = "3.0.38"; //Fix Water Temo Formula

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

// Pin definitions
#define TF_CS_PIN 21      
#define MCP_CS_PIN 19    
#define LED_RED 32
#define LED_GREEN 23
#define MOTOR_PIN 26
#define LED_BLUE 22
#define FLOW_PIN 33
#define TEMP_PIN 35      

// MCP3208 Channels - UPDATED MAPPING
#define MCP_CH_AMBIENT_TEMP  0   // Channel 0 for ambient temperature sensor (LM35)
#define MCP_CH_PRESSURE_OUT  1   // Channel 1 for output pressure
#define MCP_CH_PRESSURE_IN   2   // Channel 2 for input pressure
#define MCP_CH_WATER_TEMP    3   // Channel 3 for water temperature
// Channels 4, 5, 6 are used by ACS712_handler for current sensing (L1, L2, L3)
// Channel 7 is empty

// Constants
const int NUM_SAMPLES = 10;
const int DELAY = 5;

// MCP3208 Reference Voltage
const float MCP3208_VREF = 5.0;  // 5V reference

// Create MCP3208 object
MCP3208 adc;

// Variables
volatile int pulseCount = 0;
unsigned long lastTime = 0;

int countTemp = 0;

// Measured values - UPDATED
float pressure = 0;          // Primary pressure (inlet)
float temperature = 0;       // Water temperature
float ambientTemp = 0;       // Ambient temperature
float flow = 0;              // Flow rate
float totalTemp = 0;         // Accumulator for temperature averaging

// NEW: Additional sensor readings
float pressureIn = 0;        // Inlet pressure
float pressureOut = 0;       // Outlet pressure
float waterTemp = 0;         // Water temperature (from MCP3208 CH3)
float currentL1 = 0.0;       // Phase L1 current
float currentL2 = 0.0;       // Phase L2 current
float currentL3 = 0.0;       // Phase L3 current
float currentTotal = 0.0;    // Total current

// Control variables — volatile: shared between loop() task and MQTT FreeRTOS task
volatile bool motor = false;
volatile bool manualOverride = false;
volatile bool manualMotorState = false;
volatile bool mainSwitch = true;
bool lights = true;
volatile bool error = false;
volatile bool rebootRequested = false;
bool shouldRead = false;
bool debug = false;  // Debug mode disabled by default

// Timing for sensor updates
unsigned long lastDiagnosticsUpdate = 0;
const unsigned long DIAGNOSTICS_UPDATE_INTERVAL = 5000;  // 5s — reduced to ease async_tcp load
unsigned long lastDebugDataUpdate = 0;
const unsigned long DEBUG_DATA_INTERVAL = 30000;         // 30s — heavy JSON, rarely needed

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Track Wi-Fi status
bool wifiConnected = false;
bool apModeActive = false;
unsigned long wifiReconnectInterval = 15000;
unsigned long lastWifiReconnectAttempt = 0;

// Motor dry-run watchdog
unsigned long motorNoFlowStart = 0;
const unsigned long NO_FLOW_TIMEOUT_MS = 10000;

// Motor runtime accumulator
unsigned long motorRuntimeSeconds = 0;
unsigned long lastRuntimeSave = 0;

// Data logging
unsigned long lastLogTime = 0;

void IRAM_ATTR pulseCounter() {
    pulseCount++;
}

// ============================================================
// MCP3208 Functions (Rodolfo Prieto's Library)
// ============================================================

/**
 * Read voltage from MCP3208 channel with averaging
 * Simple integer channel numbers (0-7)
 */
float readMCP3208Average(int channel, int samples) {
    float total = 0.0;
    for (int i = 0; i < samples; i++) {
        // Library function: readADC(channel) returns raw value (0-4095)
        uint16_t rawValue = adc.analogRead(channel);
        // Convert to voltage
        float voltage = (rawValue * MCP3208_VREF) / 4095.0;
        total += voltage;
        delay(DELAY);
    }
    return (total / samples);
}

// ============================================================
// Sensor Reading Functions
// ============================================================

/**
 * Read ambient temperature from MCP3208 using LM35
 * LM35: 10mV/°C, no offset (0°C = 0V, 100°C = 1V)
 */
float readAmbientTemp() {
    float voltage = readMCP3208Average(MCP_CH_AMBIENT_TEMP, 20);
    // LM35 conversion: Temperature (°C) = voltage * 100
    float temperature = voltage * 100.0;
    
    return temperature;
}

/**
 * Read input pressure from MCP3208
 */
float readInPressure() {
    float voltage = readMCP3208Average(MCP_CH_PRESSURE_IN, NUM_SAMPLES);

    // Convert voltage to pressure (bar)
    // For 0.5-4.5V = 0-5bar: pressure = (voltage - 0.5) * 1.25
    float pressure = (voltage - 0.5) * 1.25 + config.pressure_offset + config.pressure_in_offset;

    return pressure;
}

/**
 * Read output pressure from MCP3208
 */
float readOutPressure() {
    float voltage = readMCP3208Average(MCP_CH_PRESSURE_OUT, NUM_SAMPLES);

    // Convert voltage to pressure (bar)
    float pressure = (voltage - 0.5) * 1.25 + config.pressure_offset + config.pressure_out_offset;

    return pressure;
}

/**
 * NEW: Read water temperature from MCP3208 Channel 3
 * Using thermistor or temperature sensor connected to CH3
 */
float readWaterTemp() {
    float voltage = readMCP3208Average(MCP_CH_WATER_TEMP, NUM_SAMPLES);
    
    // Convert voltage to resistance and then to temperature
    float temp_resistance = 100 * (1 / ((5.0 / temp_voltage) - 1));
    float tempC = -26.92 * log(temp_resistance) + 0.0796 * temp_resistance + 126.29 ;
    
    return tempC;
}

/**
 * DEPRECATED: Old water temperature reading from ESP32 ADC
 * Keeping for backward compatibility but not used in normal operation
 */
float readAverageTemperature(int read) {
    totalTemp += readMCP3208Average(TEMP_PIN, 1);
    countTemp++;
    
    if (read) {
        float temp_voltage = totalTemp / countTemp * (5.0 / 4096.0) + config.temp_offset;
        float temp_resistance = 100 * (1 / ((5.0 / temp_voltage) - 1));
        float temp = -26.92 * log(temp_resistance) + 0.0796 * temp_resistance + 126.29 ;
        totalTemp = 0;
        countTemp = 0;
        return temp;
    }
    return 0;
}

/**
 * NEW: Read all sensors including current sensors
 */
void readAllSensors() {
    // Read temperature sensors
    ambientTemp = readAmbientTemp();
    waterTemp = readWaterTemp();
    temperature = waterTemp; // Use water temp as primary temperature
    
    // Read pressure sensors
    pressureIn = readInPressure();
    pressureOut = readOutPressure();
    pressure = pressureIn; // Use inlet pressure as primary pressure
    
    // Read current sensors - all three phases
    CurrentReadings current = currentSensor.readAllPhases();
    currentL1 = current.L1;
    currentL2 = current.L2;
    currentL3 = current.L3;
    currentTotal = current.total;
}

// Forward declarations for functions defined after setup()/loop()
void loadRuntime();
void saveRuntime();
void appendLogEntry();

// ============================================================
// Setup and Main Functions
// ============================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== AquaSensys C3 Starting ===");
    Serial.println("Version: " + String(DEVICE_VERSION));
    
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.print("Reset reason: ");
    Serial.println(reset_reason);
    
    // Initialize SPI for both SD and MCP3208
    SPI.begin(16, 17, 18, -1); // SCK, MISO, MOSI, SS
    
    // Initialize MCP3208
    adc.begin(MCP_CS_PIN);
    adc.analogReadResolution(12); // set resolution to 12 bit
    Serial.println("MCP3208 initialized (Rodolfo Prieto library)");
    
    // NEW: Initialize ACS712 current sensor handler
    currentSensor.begin(&adc, 50, 5); // 50Hz AC frequency, 5 cycles per reading
    currentSensor.setCalibration(0.0, 1000.0); // offset in mV, scale factor
    Serial.println("ACS712 current sensors initialized (L1, L2, L3)");
    
    // Initialize SD Card
    initSDCard();
    loadConfig();
    loadRuntime();

    // NEW: Load saved calibration or perform auto-calibration
    if (config.current_offset_l1 != 0.0 || config.current_offset_l2 != 0.0 || config.current_offset_l3 != 0.0) {
        // Load saved calibration
        currentSensor.setIndividualOffsets(
            config.current_offset_l1,
            config.current_offset_l2,
            config.current_offset_l3
        );
        Serial.println("✓ Loaded saved current sensor calibration from config");
    } else {
        // No saved calibration - perform auto-calibration
        Serial.println("\n⚠ No saved calibration found");
        Serial.println("⚠ IMPORTANT: Make sure motor is OFF!");
        Serial.println("Starting auto-calibration in 3 seconds...");
        delay(3000);

        if (currentSensor.performAutoCalibration(100)) {
            CalibrationData cal = currentSensor.getCalibrationData();
            config.current_offset_l1 = cal.offsetL1;
            config.current_offset_l2 = cal.offsetL2;
            config.current_offset_l3 = cal.offsetL3;
            saveConfig();
            Serial.println("✓ Calibration saved to config file");
        } else {
            Serial.println("⚠ Using default calibration (0.0 mA offset)");
        }
    }
    
    // Setup WiFi
    setupWiFi();

    // MQTT runs on its own FreeRTOS task (Core 0) so blocking TCP connect()
    // calls never stall the Arduino loop() task
    xTaskCreatePinnedToCore(mqttTask, "mqtt", 8192, NULL, 1, NULL, 0);

    // Setup OTA updates
    OTA.begin(&server, "/update");
    OTA.enableBackup(true, "/backup");
    
    // Setup MQTT
    // Reduced from 2048 to 1024 bytes to free memory for OTA updates
    mqttClient.setBufferSize(1024);
    mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
    setupMQTT();
    
    // Setup web routes
    webRoutes();
    
    // Serve static files from SD card
    server.serveStatic("/", SD, "/");
    server.addHandler(&events);
    server.begin();
    Serial.println("Web server started");
    
    // Setup GPIO pins
    setupPins();
    digitalWrite(MOTOR_PIN, LOW);
    
    testLEDs();
    
    lastTime = millis();
    Serial.println("=== Setup Complete ===\n");
}

void loop() {
    // ============================================================
    // DEBUG MODE - Test all sensor readings
    // ============================================================

    if (debug) {
        static unsigned long lastDebugTime = 0;
        
        if (millis() - lastDebugTime >= 2000) {  // Print every 2 seconds
            lastDebugTime = millis();
            
            Serial.println("\n=== Sensor Debug Readings ===");
            
            // Read all sensors
            readAllSensors();
            
            // Print temperature readings
            Serial.print("Ambient Temperature: ");
            Serial.print(ambientTemp);
            Serial.println(" °C");
            
            Serial.print("Water Temperature: ");
            Serial.print(waterTemp);
            Serial.println(" °C");
            
            // Print pressure readings
            Serial.print("Input Pressure: ");
            Serial.print(pressureIn);
            Serial.println(" bar");
            
            Serial.print("Output Pressure: ");
            Serial.print(pressureOut);
            Serial.println(" bar");
            
            // Print current readings
            Serial.print("Current L1: ");
            Serial.print(currentL1);
            Serial.println(" A");
            
            Serial.print("Current L2: ");
            Serial.print(currentL2);
            Serial.println(" A");
            
            Serial.print("Current L3: ");
            Serial.print(currentL3);
            Serial.println(" A");
            
            Serial.print("Total Current: ");
            Serial.print(currentTotal);
            Serial.println(" A");
            
            Serial.println("==============================\n");
        }
        
        // Keep minimal system functions running in debug mode
        if (rebootRequested) {
            delay(1000);
            ESP.restart();
        }
        
        updateLights();
        
        return;  // Skip normal loop in debug mode
    }
    
    // ============================================================
    // NORMAL OPERATION MODE
    // ============================================================
    
    // Reboot if requested
    if (rebootRequested) {
        delay(1000);
        ESP.restart();
    }

    if (!apModeActive) {
        // Sync flag with actual hardware state so reconnect fires on mid-operation drops
        if (wifiConnected && WiFi.status() != WL_CONNECTED) {
            wifiConnected = false;
            Serial.println("[WiFi] Connection lost");
        }

        // WiFi reconnection
        if (!wifiConnected && millis() - lastWifiReconnectAttempt > wifiReconnectInterval) {
            Serial.println("Attempting to reconnect to Wi-Fi...");
            lastWifiReconnectAttempt = millis();
            setupWiFi();
        }

    }
    
    // Main measurement and control loop (every 1 second)
    unsigned long currentTime = millis();
    if (currentTime - lastTime >= 1000) {
        // Read flow sensor
        noInterrupts();
        flow = pulseCount / (currentTime - lastTime) / 1000.0 / 8.0;
        pulseCount = 0;
        interrupts();
        lastTime = millis();
        
        // Read all sensors (temperatures, pressures, currents)
        readAllSensors();

        // Dry-run watchdog: motor on with no flow for > 10 s → error
        if (motor && flow == 0.0f) {
            if (motorNoFlowStart == 0) motorNoFlowStart = millis();
            else if (millis() - motorNoFlowStart > NO_FLOW_TIMEOUT_MS) {
                error = true;
                Serial.println("[Error] Motor running with no flow — possible dry run");
            }
        } else {
            motorNoFlowStart = 0;
        }

        // Motor runtime accumulator
        if (motor) motorRuntimeSeconds++;
        if (millis() - lastRuntimeSave > 300000UL) {
            lastRuntimeSave = millis();
            saveRuntime();
        }

        // Periodic data logging to SD
        if (config.log_interval_minutes > 0 &&
            millis() - lastLogTime >= (unsigned long)config.log_interval_minutes * 60000UL) {
            lastLogTime = millis();
            appendLogEntry();
        }

        // Update clients and signal MQTT task to publish state
        notifyClients();
        if (!OTA.isUpdating()) {
            publishStatePending = true;
        }
        updateSerial();
    }
    
    // Diagnostics at 5s, debug data at 30s — decoupled to avoid flooding async_tcp
    if (!OTA.isUpdating() && millis() - lastDiagnosticsUpdate >= DIAGNOSTICS_UPDATE_INTERVAL) {
        lastDiagnosticsUpdate = millis();
        publishDiagnostics();
    }
    if (!OTA.isUpdating() && millis() - lastDebugDataUpdate >= DEBUG_DATA_INTERVAL) {
        lastDebugDataUpdate = millis();
        publishDebugData();
    }
    // Check for error conditions
    checkForErrors();
    // Control logic
    controlMotor();
    updateLights();
    delay(1);
}

// ============================================================
// Control Functions
// ============================================================

void controlMotor() {
    if (mainSwitch) {
        if (manualOverride) {
            motor = manualMotorState;
        } else {
            if (!error){
                if (pressure <= config.min_pressure) {
                    motor = true;
                } else if (pressure >= config.max_pressure) {
                    motor = false;
                }
            }
        }
    } else {
        motor = false;
    }
    digitalWrite(MOTOR_PIN, motor ? HIGH : LOW);
}

void testLEDs(){
    ledcWrite(LED_RED, 255);
    delay(500);
    ledcWrite(LED_RED, 0);
    ledcWrite(LED_GREEN, 255); 
    delay(500);
    ledcWrite(LED_GREEN, 0);
    ledcWrite(LED_BLUE, 255); 
    delay(500);
    ledcWrite(LED_BLUE, 0); 
}

void setupPins() {
    pinMode(FLOW_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, RISING);
    pinMode(MOTOR_PIN, OUTPUT);

    // RGB LED - PWM via LEDC
    ledcAttachChannel(LED_RED,   5000, 8, 0);  // pin, freq, resolution, channel
    ledcAttachChannel(LED_GREEN, 5000, 8, 1);
    ledcAttachChannel(LED_BLUE,  5000, 8, 2);
}

void updateSerial() {
    if (!debug) return;
    Serial.print("Override: ");
    Serial.println(manualOverride ? "ON" : "OFF");
    Serial.print("Pressure In: ");
    Serial.print(pressureIn);
    Serial.println(" bar");
    Serial.print("Pressure Out: ");
    Serial.print(pressureOut);
    Serial.println(" bar");
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
    Serial.print("Ambient Temp: ");
    Serial.print(ambientTemp);
    Serial.println(" °C");
    Serial.print("Motor status: ");
    Serial.println(motor ? "ON" : "OFF");
    Serial.printf("Current: L1=%.2fA, L2=%.2fA, L3=%.2fA\n", currentL1, currentL2, currentL3);
}

void updateLights() {
    if (lights) {
        if (error) {
            // Error state - Red blinking
            ledcWrite(LED_RED, ((millis() % 1000) < 500) ? 255 : 0 );
            ledcWrite(LED_GREEN, 0);
            ledcWrite(LED_BLUE, 0);
        }
        else if (!mainSwitch) {
            // System off - Solid red
            ledcWrite(LED_RED, 255);
            ledcWrite(LED_GREEN, 0);
            ledcWrite(LED_BLUE, 0);
        } 
        else if (manualOverride) {
            // Manual mode - Yellow (red + green) when off, Blue when on
            ledcWrite(LED_RED, !motor ? 255 : 0);  // ~100% brightness
            ledcWrite(LED_GREEN, !motor ? 64 : 0);  // ~25% brightness
            ledcWrite(LED_BLUE, motor ? 255 : 0);  // ~100% brightness
        } 
        else {
            // Auto mode - Blink Green when off, Blue when on
            ledcWrite(LED_RED, 0);
            ledcWrite(LED_GREEN, (!motor && (millis() % 1000) < 50) ? 255 : 0);
            ledcWrite(LED_BLUE, motor ? 255 : 0);
        }
    }
    else {
        // Lights off
        ledcWrite(LED_RED, 0);
        ledcWrite(LED_GREEN, 0);
        ledcWrite(LED_BLUE, 0);
    }
}

void checkForErrors() {
    if (!manualOverride) {
        if (pressure < 1.0 || pressure > 5.0) {
            error = true;
        }
        if (motor && currentTotal > config.max_current) {
            error = true;
            Serial.println("[Error] Overcurrent detected");
        }
        if (motor) {
            float maxI = max(currentL1, max(currentL2, currentL3));
            float minI = min(currentL1, min(currentL2, currentL3));
            if (maxI - minI > config.max_phase_imbalance) {
                error = true;
                Serial.println("[Error] Phase imbalance detected");
            }
        }
    } else {
        error = false;
    }
    if (error) {
        motor = false;
    }
    digitalWrite(MOTOR_PIN, motor ? HIGH : LOW);
}

// ============================================================
// SD Card Functions
// ============================================================

void loadRuntime() {
    File f = SD.open("/runtime.json");
    if (!f) return;
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, f) == DeserializationError::Ok)
        motorRuntimeSeconds = doc["motor_runtime_s"] | (unsigned long)0;
    f.close();
    Serial.printf("[Runtime] Loaded motor runtime: %lu s\n", motorRuntimeSeconds);
}

void saveRuntime() {
    File f = SD.open("/runtime.json", FILE_WRITE);
    if (!f) return;
    DynamicJsonDocument doc(128);
    doc["motor_runtime_s"] = motorRuntimeSeconds;
    serializeJson(doc, f);
    f.close();
}

void appendLogEntry() {
    File f = SD.open("/log.csv", FILE_APPEND);
    if (!f) return;
    if (f.size() == 0)
        f.println("uptime_s,pressureIn,pressureOut,flow,currentL1,currentL2,currentL3,motor");
    f.printf("%lu,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%s\n",
        millis() / 1000, pressureIn, pressureOut, flow,
        currentL1, currentL2, currentL3, motor ? "ON" : "OFF");
    f.close();
}

void initSDCard() {
    if (!SD.begin(TF_CS_PIN)) {
        Serial.println("SD Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();
    
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }
    
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    Serial.println("SD Card initialized successfully");
}

// ============================================================
// WiFi Functions
// ============================================================

void setupAPMode() {
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macSuffix[13];
    snprintf(macSuffix, sizeof(macSuffix), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String ssid = String("AquaSensys-") + macSuffix;
    WiFi.softAP(ssid.c_str());
    apModeActive = true;
    Serial.println("[AP] No WiFi credentials found. Access Point started.");
    Serial.printf("[AP] SSID: %s\n", ssid.c_str());
    Serial.printf("[AP] IP:   %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("[AP] Connect and open http://192.168.4.1 to configure WiFi.");
}

void setupWiFi() {
    if (config.wifi_ssid.isEmpty()) {
        setupAPMode();
        return;
    }
    WiFi.persistent(false);  // credentials managed via config.json, not NVS
    WiFi.mode(WIFI_OFF);     // stop any auto-connect retained from previous session
    delay(100);
    WiFi.mode(WIFI_STA);
    Serial.print("Connecting to ");
    Serial.println(config.wifi_ssid);

    // Scan to find the BSSID of the target SSID so we can pin to it.
    // This bypasses band-steering on dual-band routers that share an SSID.
    Serial.println("[WiFi] Scanning...");
    int scanCount = WiFi.scanNetworks(false, false);
    uint8_t targetBSSID[6] = {0};
    bool bssidFound = false;
    int bestRSSI = -100;
    for (int i = 0; i < scanCount; i++) {
        if (WiFi.SSID(i) == config.wifi_ssid && WiFi.RSSI(i) > bestRSSI) {
            memcpy(targetBSSID, WiFi.BSSID(i), 6);
            bestRSSI = WiFi.RSSI(i);
            bssidFound = true;
        }
    }
    WiFi.scanDelete();

    if (bssidFound) {
        Serial.printf("[WiFi] Pinning to BSSID %02X:%02X:%02X:%02X:%02X:%02X (RSSI %d)\n",
            targetBSSID[0], targetBSSID[1], targetBSSID[2],
            targetBSSID[3], targetBSSID[4], targetBSSID[5], bestRSSI);
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str(), 0, targetBSSID);
    } else {
        Serial.println("[WiFi] SSID not found in scan, connecting without BSSID pin");
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());
    }
    
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\nConnected to ");
        Serial.println(config.wifi_ssid);
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        castDNS();
    } else {
        Serial.println("\nFailed to connect to WiFi. Running in offline mode.");
        wifiConnected = false;
    }
}

void castDNS() {
    if (MDNS.begin(DEVICE_ID)) {
        Serial.println("mDNS responder started");
        Serial.print("Access your ESP32 at: http://");
        Serial.print(DEVICE_ID);
        Serial.println(".local");
        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Error starting mDNS");
    }
}

// ============================================================
// Web Server Functions — see web_routes.cpp
// ============================================================
