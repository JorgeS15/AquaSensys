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

// Device identity.
// One firmware binary runs on every unit, so the identity cannot be a compile-time
// constant: two units flashed with the same build would share an MQTT client id
// (the broker drops whichever connected first, so they knock each other off in
// turn), publish into the same Home Assistant entities, and claim the same mDNS
// name. resolveDeviceIdentity() fills these from config.json at boot, before
// anything uses them; the initial values only cover the moments before that.
static char deviceIdBuf[33]    = "aquasensys";
static char deviceNameBuf[49]  = "AquaSensys";
static char deviceModelBuf[33] = "AquaSensys";
const char* DEVICE_NAME  = deviceNameBuf;
const char* DEVICE_ID    = deviceIdBuf;
const char* DEVICE_MODEL = deviceModelBuf;
const char* DEVICE_MANUFACTURER = "JorgeS15";
const char* DEVICE_VERSION = "3.4.6"; //Current sensor: whole-cycle, mean-corrected RMS

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
volatile uint32_t pulseCount = 0;
unsigned long lastTime = 0;

// Flow input conditioning.
// A slow or noisy edge makes the GPIO input oscillate around its threshold, and
// every oscillation would otherwise be counted as water: bursts of ~730 kHz have
// been observed, against ~130 Hz for a real 20 L/min. lastPulseMicros/
// flowDebounceUs reject anything faster than a real pulse can be; flowGlitchCount
// and flowRejectedSamples expose how much is being thrown away.
volatile uint32_t lastPulseMicros = 0;
volatile uint32_t flowGlitchCount = 0;
uint32_t flowRejectedSamples = 0;
// Cached from config each cycle so the ISR never touches anything but plain RAM
volatile uint32_t flowDebounceUs = 500;

int countTemp = 0;

// Measured values - UPDATED
float pressure = 0;          // Primary pressure (inlet)
float temperature = 0;       // Water temperature
float ambientTemp = 0;       // Ambient temperature
float flow = 0;              // Flow rate
float totalTemp = 0;         // Accumulator for temperature averaging

// False until readAllSensors() has run once. Before that every reading above is
// its initial 0, and 0 bar is below the 1 bar fault threshold: evaluating the
// protections on it latched a Controller into ERROR on every boot, so the pump
// never restarted on its own after a power cut, an OTA update or a reset.
bool sensorsReady = false;

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
// Latched electrical fault: overcurrent or phase imbalance. Unlike the pressure
// and dry-run faults, manual mode cannot override it — it protects the motor
// itself, whoever is commanding it. Cleared with the error, by turning the main
// switch back on or switching the error off from Home Assistant.
volatile bool motorFault = false;
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

/**
 * Resolve this unit's identity from config.json.
 *
 * device_id and device_name override everything. Left empty, they default by
 * device type to the identities these units carried when each was built by hand,
 * so existing Home Assistant devices and entity history carry straight over:
 *   Controller: id "aquasensys",   name "AquaSensys C3", model "AquaSensys C3"
 *   Monitor:    id "aquasensys-m", name "AquaSensys M",  model "AquaSensys Monitor"
 * Two units of the same type still need an explicit device_id each.
 *
 * Called once from setup(); a change to any of these takes effect on reboot,
 * because MQTT topics and the mDNS name are built from them at startup.
 */
void resolveDeviceIdentity() {
    bool controller = isControllerDevice();

    String id = config.device_id.length() ? config.device_id
                                          : String(controller ? "aquasensys" : "aquasensys-m");
    String name = config.device_name.length() ? config.device_name
                                              : String(controller ? "AquaSensys C3" : "AquaSensys M");
    const char* model = controller ? "AquaSensys C3" : "AquaSensys Monitor";

    strlcpy(deviceIdBuf,    id.c_str(),   sizeof(deviceIdBuf));
    strlcpy(deviceNameBuf,  name.c_str(), sizeof(deviceNameBuf));
    strlcpy(deviceModelBuf, model,        sizeof(deviceModelBuf));

    Serial.printf("Identity: id=%s name=\"%s\" model=\"%s\"%s\n",
                  DEVICE_ID, DEVICE_NAME, DEVICE_MODEL,
                  config.device_id.length() ? "" : " (id defaulted from device type)");
}

void IRAM_ATTR pulseCounter() {
    uint32_t now = micros();
    // Unsigned subtraction, so this stays correct across the micros() rollover
    if ((uint32_t)(now - lastPulseMicros) < flowDebounceUs) {
        flowGlitchCount++;
        return;
    }
    lastPulseMicros = now;
    pulseCount++;
}

/**
 * Convert a pulse count over an elapsed window into flow in L/min.
 *
 * The sensors follow the datasheet form  F = K * Q + offset,  where F is the
 * pulse frequency in Hz and Q the flow in L/min, so Q = (F - offset) / K.
 *   3/4" connection: F = 6.6 * Q      -> 396 pulses per litre  (default)
 *   1"   connection: F = 8 * Q - 4    -> ~476 pulses per litre
 * Both constants come from flowKFactor()/flowKOffset(), which prefer the
 * config.json override over the default for config.connection_size.
 */
float computeFlowLpm(uint32_t pulses, unsigned long elapsedMs) {
    if (elapsedMs == 0 || pulses == 0) return 0.0f;
    float pulsesPerSecond = (pulses * 1000.0f) / (float)elapsedMs;
    float lpm = (pulsesPerSecond - flowKOffset()) / flowKFactor();
    // The offset makes the line cross zero at a small positive frequency, so a
    // single stray pulse must not come out as negative flow.
    return lpm > 0.0f ? lpm : 0.0f;
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
    float temp_voltage = readMCP3208Average(MCP_CH_WATER_TEMP, NUM_SAMPLES);

    // A probe that is disconnected or shorted pins the divider to a rail, and the
    // conversion below then divides by zero or takes log(0). Report "no reading"
    // (NAN, serialised as null) rather than a meaningless number. Real readings
    // sit well inside these bounds (~0.15 V at 100 °C to ~3.3 V near 0 °C).
    if (temp_voltage < 0.05f || temp_voltage > 4.95f) return NAN;

    // Convert voltage to resistance and then to temperature
    float temp_resistance = 100 * (1 / ((5.0 / temp_voltage) - 1));
    float tempC = -26.92 * log(temp_resistance) + 0.0796 * temp_resistance + 126.29 ;

    return (isnan(tempC) || isinf(tempC)) ? NAN : tempC;
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

    sensorsReady = true;
}

// Forward declarations for functions defined after setup()/loop()
void loadRuntime();
void saveRuntime();
void appendLogEntry();
void serviceWiFiReconnect();
void castDNS();

// ============================================================
// Setup and Main Functions
// ============================================================

void setup() {
    // First thing on boot: hold the motor output low. From reset until
    // setupPins() runs — through WiFi, calibration and server setup, up to ~20 s —
    // GPIO26 is otherwise a floating input, and a relay driver without its own
    // pull-down can pick that up as "on".
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    Serial.begin(115200);
    Serial.println("\n\n=== AquaSensys Starting ===");
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
    // 2 full 50 Hz cycles (40 ms) per phase: a whole number of cycles is what makes
    // the RMS exact, and 2 keeps the three phases to ~120 ms of each 1 s loop.
    currentSensor.begin(&adc, 50, 2);
    currentSensor.setCalibration(0.0, 1000.0); // offset in mV, scale factor
    Serial.println("ACS712 current sensors initialized (L1, L2, L3)");
    
    // Initialize SD Card
    initSDCard();
    loadConfig();
    loadRuntime();
    resolveDeviceIdentity();   // before WiFi/mDNS/MQTT, which all use it

    Serial.printf("Device type: %s — %s\n",
                  config.device_type.c_str(),
                  isControllerDevice() ? "Controller (motor control active)"
                                       : "Monitor (motor control disabled)");
    Serial.printf("Connection size: %s\" — flow curve F = %.2f * Q %c %.2f Hz\n",
                  config.connection_size.c_str(), flowKFactor(),
                  flowKOffset() < 0 ? '-' : '+', fabsf(flowKOffset()));
    flowDebounceUs = config.flow_debounce_us;
    Serial.printf("Flow debounce: %lu us (ceiling %.0f L/min), max plausible %.0f L/min\n",
                  (unsigned long)flowDebounceUs,
                  (1000000.0f / (float)flowDebounceUs - flowKOffset()) / flowKFactor(),
                  config.max_flow_lpm);

    // NEW: Load saved calibration or perform auto-calibration
    if (config.current_offset_l1 != 0.0 || config.current_offset_l2 != 0.0 || config.current_offset_l3 != 0.0) {
        // Load saved calibration
        currentSensor.setIndividualOffsets(
            config.current_offset_l1,
            config.current_offset_l2,
            config.current_offset_l3
        );
        Serial.println("✓ Loaded saved current sensor calibration from config");
    } else if (!isControllerDevice()) {
        // A Monitor doesn't drive the pump, so it can't know the pump is stopped,
        // and calibrating while it runs would record the running current as zero.
        // Readings are mean-corrected, so they are accurate uncalibrated; the
        // residual noise (~40 mA) is below the 0.1 A display floor.
        Serial.println("⚠ No saved current calibration — skipped on a Monitor, which can't be sure the pump is off.");
        Serial.println("  Calibrate from the Diagnostics page with the pump stopped if needed.");
    } else {
        // No saved calibration - perform auto-calibration. Safe on a Controller:
        // the motor output has been held low since reset.
        Serial.println("\n⚠ No saved calibration found");
        Serial.println("⚠ IMPORTANT: Make sure motor is OFF!");
        Serial.println("Starting auto-calibration in 3 seconds...");
        delay(3000);

        // 10 full-cycle windows per phase: each one already averages 2 cycles
        if (currentSensor.performAutoCalibration(10)) {
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

    // Take a real reading now, so the protections and the control loop start
    // from actual values rather than from the zero-initialised ones.
    readAllSensors();
    Serial.printf("Initial readings: pressure in %.2f bar, out %.2f bar\n", pressureIn, pressureOut);

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
            // Give the driver's own auto-reconnect a full interval first: short
            // drops recover by themselves in a second or two, and scanning straight
            // away would only abort that.
            lastWifiReconnectAttempt = millis();
            Serial.println("[WiFi] Connection lost");
        }

        // WiFi reconnection — one non-blocking step per pass, so the pump
        // control and protections below keep running while the network is down
        serviceWiFiReconnect();

    }
    
    // Main measurement and control loop (every 1 second)
    unsigned long currentTime = millis();
    if (currentTime - lastTime >= 1000) {
        // Read flow sensor — snapshot the ISR counter, then do the maths with
        // interrupts back on so pulses are not lost during the conversion.
        noInterrupts();
        uint32_t pulses = pulseCount;
        pulseCount = 0;
        interrupts();

        unsigned long elapsedMs = currentTime - lastTime;
        float measuredFlow = computeFlowLpm(pulses, elapsedMs);

        // Electrical noise can still outrun the debounce. A reading beyond what
        // the plumbing can physically deliver is not water, so keep the last
        // good value rather than publishing a spike.
        if (measuredFlow > config.max_flow_lpm) {
            flowRejectedSamples++;
            Serial.printf("[Flow] Discarded implausible reading: %.0f L/min (%lu pulses in %lu ms)\n",
                          measuredFlow, (unsigned long)pulses, elapsedMs);
        } else {
            flow = measuredFlow;
        }

        lastTime = millis();
        flowDebounceUs = config.flow_debounce_us;  // pick up live config changes
        
        // Read all sensors (temperatures, pressures, currents)
        readAllSensors();

        // Motor-related bookkeeping only exists on Controller units
        if (isControllerDevice()) {
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
    // Monitor units (device_type "M") have no pump to drive: the motor output
    // stays de-energised and no control decision is ever made. Nor is one made
    // before there is a real pressure reading to make it on.
    if (!isControllerDevice() || !sensorsReady) {
        motor = false;
        digitalWrite(MOTOR_PIN, LOW);
        return;
    }

    if (mainSwitch) {
        if (manualOverride) {
            // checkForErrors() runs just before this; without the motorFault
            // check, manual mode would switch the motor straight back on.
            motor = manualMotorState && !motorFault;
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
    // Plain INPUT, never INPUT_PULLDOWN. The sensor output is push-pull and
    // reaches the ESP32 through a 5V->3.3V level converter that already pulls
    // the line up through ~10k. An internal pull-down (~45k) divides that down
    // to ~2.7 V against the ESP32's 2.475 V input threshold, leaving ~0.2 V of
    // margin — close enough to the threshold for the input to oscillate and
    // flood the ISR with phantom pulses.
    pinMode(FLOW_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, RISING);
    // Driven low on Monitor units too, so the output is never left floating
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    // RGB LED - PWM via LEDC
    ledcAttachChannel(LED_RED,   5000, 8, 0);  // pin, freq, resolution, channel
    ledcAttachChannel(LED_GREEN, 5000, 8, 1);
    ledcAttachChannel(LED_BLUE,  5000, 8, 2);
}

void updateSerial() {
    if (!debug) return;
    if (isControllerDevice()) {
        Serial.print("Override: ");
        Serial.println(manualOverride ? "ON" : "OFF");
    }
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
    if (isControllerDevice()) {
        Serial.print("Motor status: ");
        Serial.println(motor ? "ON" : "OFF");
    }
    Serial.printf("Current: L1=%.2fA, L2=%.2fA, L3=%.2fA\n", currentL1, currentL2, currentL3);
}

void updateLights() {
    if (lights) {
        // Monitor units have no motor or main switch to signal — only a
        // healthy/error indication.
        if (!isControllerDevice()) {
            if (error) {
                ledcWrite(LED_RED, ((millis() % 1000) < 500) ? 255 : 0);
                ledcWrite(LED_GREEN, 0);
            } else {
                ledcWrite(LED_RED, 0);
                ledcWrite(LED_GREEN, ((millis() % 1000) < 50) ? 255 : 0);
            }
            ledcWrite(LED_BLUE, 0);
            return;
        }

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
    // No reading yet means nothing to judge — see sensorsReady
    if (!sensorsReady) return;

    // Monitor units only report; nothing is tripped and nothing is switched off,
    // so the flag tracks the live reading instead of latching.
    if (!isControllerDevice()) {
        error = (pressure < 1.0 || pressure > 5.0);
        return;
    }

    // Electrical faults apply in every mode: manual mode overrides the pressure
    // logic, not the motor's own protection.
    if (motor && currentTotal > config.max_current) {
        if (!motorFault) Serial.printf("[Error] Overcurrent: %.2f A > %.2f A\n", currentTotal, config.max_current);
        motorFault = true;
    }
    if (motor) {
        float maxI = max(currentL1, max(currentL2, currentL3));
        float minI = min(currentL1, min(currentL2, currentL3));
        if (maxI - minI > config.max_phase_imbalance) {
            if (!motorFault) Serial.printf("[Error] Phase imbalance: %.2f A > %.2f A\n", maxI - minI, config.max_phase_imbalance);
            motorFault = true;
        }
    }

    if (!manualOverride) {
        if (pressure < 1.0 || pressure > 5.0) {
            error = true;
        }
    } else {
        // Manual mode is how an operator primes or refills an empty system, where
        // low pressure and no flow are expected — so those faults stay cleared.
        error = false;
    }
    if (motorFault) {
        error = true;
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

// Strongest access point advertising the configured SSID, from the last scan.
// Pinning to it bypasses band-steering on dual-band routers that share an SSID.
bool findBestBSSID(int scanCount, uint8_t* bssid, int* rssi) {
    bool found = false;
    int best = -100;
    for (int i = 0; i < scanCount; i++) {
        if (WiFi.SSID(i) == config.wifi_ssid && WiFi.RSSI(i) > best) {
            memcpy(bssid, WiFi.BSSID(i), 6);
            best = WiFi.RSSI(i);
            found = true;
        }
    }
    *rssi = best;
    return found;
}

void beginWiFi(bool pinned, const uint8_t* bssid, int rssi) {
    if (pinned) {
        Serial.printf("[WiFi] Pinning to BSSID %02X:%02X:%02X:%02X:%02X:%02X (RSSI %d)\n",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], rssi);
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str(), 0, bssid);
    } else {
        Serial.println("[WiFi] SSID not found in scan, connecting without BSSID pin");
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());
    }
}

// ------------------------------------------------------------
// Non-blocking reconnection.
// The old reconnect re-ran setupWiFi() from loop(): a synchronous scan plus up
// to 15 s waiting for the link, ~18 s in all, retried every 15 s counted from
// the start of the previous attempt. With the router down, loop() spent nearly
// all its time in there, and the pressure control, dry-run watchdog and
// overcurrent trip ran once every ~18 s. Each call below does one short step
// and returns.
// ------------------------------------------------------------
enum WifiReconnectState : uint8_t { WIFI_RC_IDLE, WIFI_RC_SCANNING, WIFI_RC_CONNECTING };
static WifiReconnectState wifiRcState = WIFI_RC_IDLE;
static unsigned long wifiRcStepStart = 0;
const unsigned long WIFI_SCAN_TIMEOUT_MS    = 10000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

static void markWiFiConnected() {
    wifiConnected = true;
    wifiRcState = WIFI_RC_IDLE;
    Serial.printf("[WiFi] Reconnected, IP %s\n", WiFi.localIP().toString().c_str());
    castDNS();
}

static void markWiFiAttemptFailed(const char* why) {
    Serial.printf("[WiFi] Reconnect failed (%s); retrying in %lu s\n",
                  why, wifiReconnectInterval / 1000);
    lastWifiReconnectAttempt = millis();   // the next try waits a full interval
    wifiRcState = WIFI_RC_IDLE;
}

void serviceWiFiReconnect() {
    switch (wifiRcState) {

    case WIFI_RC_IDLE:
        if (wifiConnected) return;
        // The WiFi driver also retries on its own; if it got there first, adopt it
        if (WiFi.status() == WL_CONNECTED) { markWiFiConnected(); return; }
        if (millis() - lastWifiReconnectAttempt < wifiReconnectInterval) return;

        Serial.println("[WiFi] Reconnecting...");
        WiFi.disconnect();                 // a scan can't run alongside a pending connect
        WiFi.scanNetworks(true, false);    // async: returns at once
        wifiRcStepStart = millis();
        wifiRcState = WIFI_RC_SCANNING;
        return;

    case WIFI_RC_SCANNING: {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            if (millis() - wifiRcStepStart >= WIFI_SCAN_TIMEOUT_MS) markWiFiAttemptFailed("scan timed out");
            return;
        }
        uint8_t bssid[6] = {0};
        int rssi = -100;
        bool pinned = n > 0 && findBestBSSID(n, bssid, &rssi);   // n < 0: scan failed
        WiFi.scanDelete();
        beginWiFi(pinned, bssid, rssi);
        wifiRcStepStart = millis();
        wifiRcState = WIFI_RC_CONNECTING;
        return;
    }

    case WIFI_RC_CONNECTING:
        if (WiFi.status() == WL_CONNECTED) { markWiFiConnected(); return; }
        if (millis() - wifiRcStepStart >= WIFI_CONNECT_TIMEOUT_MS) markWiFiAttemptFailed("no link");
        return;
    }
}

// Blocking connect, used once at boot only — before the control loop starts,
// with the motor output already held low.
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
    int bestRSSI = -100;
    bool bssidFound = scanCount > 0 && findBestBSSID(scanCount, targetBSSID, &bestRSSI);
    WiFi.scanDelete();
    beginWiFi(bssidFound, targetBSSID, bestRSSI);
    
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
