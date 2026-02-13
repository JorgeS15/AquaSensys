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

// Device constants (not saved to config)
const char* DEVICE_NAME = "AquaSensys C3";
const char* DEVICE_ID = "aquasensys";
const char* DEVICE_MANUFACTURER = "JorgeS15";
const char* DEVICE_MODEL = "AquaSensys C3";
const char* DEVICE_VERSION = "3.0.20"; //LEDs Fixed + Memory Optimization

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

// Control variables
bool motor = false;
bool manualOverride = false;
bool manualMotorState = false;
bool mainSwitch = true;
bool lights = true;
bool error = false;
bool rebootRequested = false;
bool shouldRead = false;
bool debug = false;  // Debug mode disabled by default

// NEW: Timing for sensor updates
unsigned long lastDiagnosticsUpdate = 0;
const unsigned long DIAGNOSTICS_UPDATE_INTERVAL = 1000; // 1Hz update for diagnostics

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Track Wi-Fi status
bool wifiConnected = false;
unsigned long wifiReconnectInterval = 15000;
unsigned long lastWifiReconnectAttempt = 0;

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
    
    // Convert voltage to temperature
    // This is a placeholder - adjust based on your actual sensor
    // Example for NTC thermistor with voltage divider
    float tempC = (voltage - 0.5) * 100.0 + config.temp_offset;
    
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
        float temp_resistance = 97 * (1 / ((5.0 / temp_voltage) - 1));
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
    
    // Setup OTA updates
    OTA.begin(&server, "/update");
    OTA.enableBackup(true, "/backup");
    
    // Setup MQTT
    mqttClient.setBufferSize(2048);
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

    // WiFi reconnection
    if (!wifiConnected && millis() - lastWifiReconnectAttempt > wifiReconnectInterval) {
        Serial.println("Attempting to reconnect to Wi-Fi...");
        lastWifiReconnectAttempt = millis();
        setupWiFi();
    }

    // MQTT reconnection
    if (wifiConnected && !mqttClient.connected() && 
        millis() - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
        lastMqttReconnectAttempt = millis();
        mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port);
        if (reconnectMQTT()) {
            lastMqttReconnectAttempt = 0;
        }
    }
    mqttClient.loop();
    
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

        // Control logic
        controlMotor();
        updateLights();

        // Update clients and MQTT
        notifyClients();
        publishState();
        updateSerial();
    }
    
    // NEW: Update diagnostics at 1Hz (separate from main update)
    if (millis() - lastDiagnosticsUpdate >= DIAGNOSTICS_UPDATE_INTERVAL) {
        lastDiagnosticsUpdate = millis();
        publishDiagnostics();
        publishDebugData(); // Also publish debug data for debug page
    }
    
    // Check for error conditions
    checkForErrors();
}

// ============================================================
// Control Functions
// ============================================================

void controlMotor() {
    if (mainSwitch) {
        if (manualOverride) {
            motor = manualMotorState;
        } else {
            if (pressure <= config.min_pressure) {
                motor = true;
            } else if (pressure >= config.max_pressure) {
                motor = false;
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
            digitalWrite(LED_RED, (millis() % 1000) < 500);
            digitalWrite(LED_GREEN, 0);
            digitalWrite(LED_BLUE, 0);
        }
        else if (!mainSwitch) {
            // System off - Solid red
            digitalWrite(LED_RED, 255);
            digitalWrite(LED_GREEN, 0);
            digitalWrite(LED_BLUE, 0);
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
        ledcWrite(LED_RED, LOW);
        ledcWrite(LED_GREEN, LOW);
        ledcWrite(LED_BLUE, LOW);
    }
}

void checkForErrors() {
    if (!manualOverride) {
        if (pressure < 1.0 || pressure > 5.0) {
            error = true;
        }
    }
    else{
        error = false;
    }
    if (error) {
        motor = false;
        mainSwitch = false;
    }
}

// ============================================================
// SD Card Functions
// ============================================================

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

void setupWiFi() {
    WiFi.mode(WIFI_STA);
    Serial.print("Connecting to ");
    Serial.println(config.wifi_ssid);
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());
    
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
// Web Server Functions
// ============================================================

void notifyClients() {
    DynamicJsonDocument doc(256);
    doc["pressure"] = pressure;
    doc["temperature"] = temperature;
    doc["ambientTemp"] = ambientTemp;
    doc["flow"] = flow;
    doc["motor"] = motor;
    doc["manualOverride"] = manualOverride;
    doc["mainSwitch"] = mainSwitch;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    events.send(json.c_str(), "update", millis());
}

/**
 * NEW: Publish diagnostics data including current sensors
 */
void publishDiagnostics() {
    if (!events.count()) return; // No clients connected
    
    DynamicJsonDocument doc(512);
    
    // Current readings
    doc["current_l1"] = currentL1;
    doc["current_l2"] = currentL2;
    doc["current_l3"] = currentL3;
    doc["current_total"] = currentTotal;
    
    // Temperature readings
    doc["temp_ambient"] = ambientTemp;
    doc["temp_water"] = waterTemp;
    
    // Pressure readings
    doc["pressure_in"] = pressureIn;
    doc["pressure_out"] = pressureOut;
    
    // System info
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    
    String json;
    serializeJson(doc, json);
    
    events.send(json.c_str(), "diagnostics", millis());
}

/**
 * Publish comprehensive debug data for troubleshooting hardware issues
 */
void publishDebugData() {
    if (!events.count()) return; // No clients connected

    DynamicJsonDocument doc(1536);

    // SD Card Status
    doc["sd_detected"] = (SD.cardType() != CARD_NONE);
    doc["sd_type"] = (SD.cardType() == CARD_MMC) ? "MMC" :
                     (SD.cardType() == CARD_SD) ? "SD" :
                     (SD.cardType() == CARD_SDHC) ? "SDHC" : "UNKNOWN";
    doc["sd_size"] = SD.cardSize();

    // Count files on SD
    int fileCount = 0;
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            fileCount++;
            file = root.openNextFile();
        }
        root.close();
    }
    doc["sd_files"] = fileCount;

    // MCP3208 Status
    doc["mcp_init"] = true; // MCP3208 initialized in setup()
    doc["mcp_vref"] = MCP3208_VREF;
    doc["mcp_errors"] = 0; // TODO: track errors if MCP has error counter
    doc["spi_status"] = "OK"; // TODO: add actual SPI status if available

    // Raw ADC Values (0-4095)
    for (int i = 0; i < 8; i++) {
        doc["adc_ch" + String(i)] = adc.analogRead(i);
    }

    // Voltage Readings
    for (int i = 0; i < 4; i++) {
        doc["volt_ch" + String(i)] = readMCP3208Average(i, NUM_SAMPLES);
    }

    // Processed Sensor Values
    doc["pressure_in"] = pressureIn;
    doc["pressure_out"] = pressureOut;
    doc["temp_ambient"] = ambientTemp;
    doc["temp_water"] = waterTemp;

    // Calibration Offsets
    doc["cal_press_in"] = config.pressure_in_offset;
    doc["cal_press_out"] = config.pressure_out_offset;
    doc["cal_curr_l1"] = config.current_offset_l1;
    doc["cal_curr_l2"] = config.current_offset_l2;
    doc["cal_curr_l3"] = config.current_offset_l3;

    // System Information
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["reset_reason"] = esp_reset_reason();
    doc["cpu_freq"] = ESP.getCpuFreqMHz();

    // WiFi Status
    doc["wifi_connected"] = WiFi.isConnected();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["wifi_ip"] = WiFi.localIP().toString();

    // MQTT Status
    doc["mqtt_connected"] = mqttClient.connected();
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_last"] = (millis() - lastMqttReconnectAttempt) / 1000; // seconds since last attempt

    // Motor & Control State
    doc["motor"] = motor;
    doc["main_switch"] = mainSwitch;
    doc["manual_override"] = manualOverride;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    events.send(json.c_str(), "debug", millis());
}

void webRoutes() {
    // Setup file manager routes
    setupFileManagerRoutes(server);
    
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/index.html")) {
            request->send(SD, "/index.html", "text/html");
        } else {
            request->send(404, "text/plain", "index.html not found on SD card");
        }
    });

    // Static file routes
    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/app.js")) {
            request->send(SD, "/app.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "app.js not found on SD card");
        }
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/style.css")) {
            request->send(SD, "/style.css", "text/css");
        } else {
            request->send(404, "text/plain", "style.css not found on SD card");
        }
    });

    server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/settings.html")) {
            request->send(SD, "/settings.html", "text/html");
        } else {
            request->send(404, "text/plain", "settings.html not found on SD card");
        }
    });

    server.on("/settings.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/settings.js")) {
            request->send(SD, "/settings.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "settings.js not found on SD card");
        }
    });

    server.on("/translations.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/translations.js")) {
            request->send(SD, "/translations.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "translations.js not found on SD card");
        }
    });

    server.on("/language.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/language.js")) {
            request->send(SD, "/language.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "language.js not found on SD card");
        }
    });
    
    // NEW: Diagnostics page route
    server.on("/diagnostics.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/diagnostics.html")) {
            request->send(SD, "/diagnostics.html", "text/html");
        } else {
            request->send(404, "text/plain", "diagnostics.html not found on SD card");
        }
    });
    
    // API endpoint to get current configuration
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getConfigJson());
    });
    
    // API endpoint to update configuration
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        String jsonStr = String((char*)data).substring(0, len);
        
        if (updateConfigFromJson(jsonStr)) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, jsonStr);
            bool wifiChanged = doc.containsKey("wifi_ssid") || doc.containsKey("wifi_password");
            
            request->send(200, "application/json", "{\"status\":\"success\",\"wifi_changed\":" + String(wifiChanged ? "true" : "false") + "}");
            
            if (wifiChanged) {
                rebootRequested = true;
            }
        } else {
            request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
        }
    });
    
    // Factory reset endpoint
    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (SD.exists("/config.json")) {
            SD.remove("/config.json");
        }
        
        config = Config();
        saveConfig();
        
        request->send(200, "application/json", "{\"status\":\"success\"}");
        rebootRequested = true;
    });
    
    // NEW: Manual current sensor calibration endpoint
    server.on("/api/calibrate-current", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Check if motor is off
        if (motor) {
            request->send(400, "application/json",
                "{\"error\":\"Motor must be OFF for calibration\",\"motor_status\":\"ON\"}");
            return;
        }

        // Perform calibration
        Serial.println("\n=== Manual Calibration Requested via API ===");
        if (currentSensor.performAutoCalibration(100)) {
            CalibrationData cal = currentSensor.getCalibrationData();
            config.current_offset_l1 = cal.offsetL1;
            config.current_offset_l2 = cal.offsetL2;
            config.current_offset_l3 = cal.offsetL3;
            saveConfig();

            DynamicJsonDocument doc(256);
            doc["status"] = "success";
            doc["offset_l1"] = config.current_offset_l1;
            doc["offset_l2"] = config.current_offset_l2;
            doc["offset_l3"] = config.current_offset_l3;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(500, "application/json",
                "{\"error\":\"Calibration failed - check motor is OFF and retry\"}");
        }
    });

    // Pressure sensor calibration endpoint
    server.on("/api/calibrate-pressure", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("\n=== Pressure Calibration Requested via API ===");

        // Read current pressure values (without offsets)
        float voltageIn = readMCP3208Average(MCP_CH_PRESSURE_IN, NUM_SAMPLES);
        float voltageOut = readMCP3208Average(MCP_CH_PRESSURE_OUT, NUM_SAMPLES);

        // Calculate raw pressure (without any offsets)
        float rawPressureIn = (voltageIn - 0.5) * 1.25;
        float rawPressureOut = (voltageOut - 0.5) * 1.25;

        // Set offsets to zero out current readings
        // offset = -raw_pressure (so raw_pressure + offset = 0)
        config.pressure_in_offset = -(rawPressureIn + config.pressure_offset);
        config.pressure_out_offset = -(rawPressureOut + config.pressure_offset);
        config.pressure_calibrated = true;

        // Save to config
        if (saveConfig()) {
            Serial.printf("Pressure calibration successful:\n");
            Serial.printf("  Inlet offset: %.3f bar\n", config.pressure_in_offset);
            Serial.printf("  Outlet offset: %.3f bar\n", config.pressure_out_offset);

            DynamicJsonDocument doc(256);
            doc["status"] = "success";
            doc["offset_in"] = config.pressure_in_offset;
            doc["offset_out"] = config.pressure_out_offset;
            doc["raw_in"] = rawPressureIn;
            doc["raw_out"] = rawPressureOut;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(500, "application/json",
                "{\"error\":\"Failed to save calibration to config\"}");
        }
    });

    // Diagnostics JSON endpoint (for backward compatibility)
    server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512);
        doc["uptime"] = millis() / 1000;
        doc["heap"] = ESP.getFreeHeap();
        doc["resetReason"] = esp_reset_reason();
        doc["wifiStrength"] = WiFi.RSSI();
        doc["sdCardStatus"] = SD.cardType() != CARD_NONE ? "Connected" : "Not Found";
        doc["sdCardSize"] = SD.cardSize() / (1024 * 1024);
        doc["adcType"] = "MCP3208 (Rodolfo Prieto)";
        
        // Add current sensor data
        doc["current_l1"] = currentL1;
        doc["current_l2"] = currentL2;
        doc["current_l3"] = currentL3;
        doc["current_total"] = currentTotal;
        
        // Add temperature data
        doc["temp_ambient"] = ambientTemp;
        doc["temp_water"] = waterTemp;
        
        // Add pressure data
        doc["pressure_in"] = pressureIn;
        doc["pressure_out"] = pressureOut;
        
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        rebootRequested = true;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(128);
        DeserializationError jsonError = deserializeJson(doc, data, len);
        
        if (!jsonError) {
            if (doc.containsKey("command")) {
                String command = doc["command"];
                if (command == "toggle") {
                    manualOverride = true;
                    manualMotorState = !manualMotorState;
                } else if (command == "override") {
                    manualOverride = !manualOverride;
                } else if (command == "mainSwitch") {
                    mainSwitch = !mainSwitch;
                    // If turning off main switch, clear error state
                    if (mainSwitch) {
                        error = false;
                    }
                }

                notifyClients();
                publishState();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"invalid request\"}");
    });

    // Debug page route - serve debug.html or fallback to simple debug info
    server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SD.exists("/debug.html")) {
            request->send(SD, "/debug.html", "text/html");
        } else {
            // Fallback to simple debug info if debug.html not found
            String html = "<html><body><h1>SD Card Files:</h1><ul>";

            File root = SD.open("/");
            if (root) {
                File file = root.openNextFile();
                while (file) {
                    html += "<li>" + String(file.name()) + " (" + String(file.size()) + " bytes)</li>";
                    file = root.openNextFile();
                }
                root.close();
            } else {
                html += "<li>Could not open root directory</li>";
            }

            html += "</ul>";
            html += "<h2>Current Config:</h2><pre>" + getConfigJson() + "</pre>";
            html += "<h2>Device Info:</h2><pre>";
            html += "Device Name: " + String(DEVICE_NAME) + "\n";
            html += "Device ID: " + String(DEVICE_ID) + "\n";
            html += "Device Version: " + String(DEVICE_VERSION) + "\n";
            html += "Device Model: " + String(DEVICE_MODEL) + "\n";
            html += "ADC Type: MCP3208 (Rodolfo Prieto)\n";
            html += "Current Sensors: ACS712-05B (3-Phase)\n";
            html += "</pre>";
            html += "<h2>Live Readings:</h2><pre>";
            html += "Ambient Temp: " + String(ambientTemp) + " °C\n";
            html += "Water Temp: " + String(waterTemp) + " °C\n";
        html += "Pressure In: " + String(pressureIn) + " bar\n";
        html += "Pressure Out: " + String(pressureOut) + " bar\n";
        html += "Current L1: " + String(currentL1) + " A\n";
        html += "Current L2: " + String(currentL2) + " A\n";
        html += "Current L3: " + String(currentL3) + " A\n";
        html += "Total Current: " + String(currentTotal) + " A\n";
        html += "</pre>";
        html += "</body></html>";
        request->send(200, "text/html", html);
        }
    });

    events.onConnect([](AsyncEventSourceClient *client) {
        Serial.println("Client connected to /events");
        notifyClients();
        publishDiagnostics(); // Send initial diagnostics data
        publishDebugData(); // Send initial debug data
    });
}
