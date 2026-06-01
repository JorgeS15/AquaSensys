#include "sensor_task.h"
#include "freertos_glue.h"
#include "ACS712_handler.h"
#include "config_manager.h"
#include "ota_handler.h"
#include <SD.h>
#include <math.h>

extern MCP3208 adc;

static const int   NUM_SAMPLES     = 10;
static const int   SAMPLE_DELAY_MS = 5;
static const float MCP3208_VREF    = 5.0f;

static unsigned long motorNoFlowStart    = 0;
static unsigned long lastRuntimeSave     = 0;
static const unsigned long NO_FLOW_TIMEOUT_MS = 10000;

portMUX_TYPE      flowMux   = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t pulseCount = 0;

unsigned long motorRuntimeSeconds = 0;

// Pulse counter ISR — IRAM so it runs even during flash cache misses
void IRAM_ATTR pulseCounter() {
    portENTER_CRITICAL_ISR(&flowMux);
    pulseCount++;
    portEXIT_CRITICAL_ISR(&flowMux);
}

// ── MCP3208 helper ────────────────────────────────────────────────────────────

float readMCP3208Average(int channel, int samples) {
    float total = 0.0f;
    {
        SPILock lock;
        if (!lock.ok()) return 0.0f;
        for (int i = 0; i < samples; i++) {
            uint16_t raw = adc.analogRead(channel);
            total += (raw * MCP3208_VREF) / 4095.0f;
            delay(SAMPLE_DELAY_MS);
        }
    }
    return total / samples;
}

// ── Individual sensor readers (each calls readMCP3208Average which takes spiMutex) ──

static float readAmbientTemp() {
    float v = readMCP3208Average(0 /*MCP_CH_AMBIENT_TEMP*/, 20);
    return v * 100.0f; // LM35: 10 mV/°C → °C = V*100
}

static float readInPressure() {
    float v = readMCP3208Average(2 /*MCP_CH_PRESSURE_IN*/, NUM_SAMPLES);
    CfgLock lock(pdMS_TO_TICKS(10));
    float off = lock.ok() ? config.pressure_offset + config.pressure_in_offset : 0.0f;
    return (v - 0.5f) * 1.25f + off;
}

static float readOutPressure() {
    float v = readMCP3208Average(1 /*MCP_CH_PRESSURE_OUT*/, NUM_SAMPLES);
    CfgLock lock(pdMS_TO_TICKS(10));
    float off = lock.ok() ? config.pressure_offset + config.pressure_out_offset : 0.0f;
    return (v - 0.5f) * 1.25f + off;
}

static float readWaterTemp() {
    float v = readMCP3208Average(3 /*MCP_CH_WATER_TEMP*/, NUM_SAMPLES);
    CfgLock lock(pdMS_TO_TICKS(10));
    float off = lock.ok() ? config.temp_offset : 0.0f;
    return (v - 0.5f) * 100.0f + off;
}

static void readAllSensors(SensorSnapshot& snap) {
    snap.ambientTemp  = readAmbientTemp();
    snap.waterTemp    = readWaterTemp();
    snap.temperature  = snap.waterTemp;
    snap.pressureIn   = readInPressure();
    snap.pressureOut  = readOutPressure();
    snap.pressure     = snap.pressureIn;

    // ACS712 calls adc->analogRead() directly — hold spiMutex for all three phases
    {
        SPILock lock;
        if (lock.ok()) {
            CurrentReadings cur = currentSensor.readAllPhases();
            snap.currentL1    = cur.L1;
            snap.currentL2    = cur.L2;
            snap.currentL3    = cur.L3;
            snap.currentTotal = cur.total;
        }
    }
}

// ── Logging task ─────────────────────────────────────────────────────────────

void loggingTask(void* pvParameters) {
    LogCmd cmd;
    for (;;) {
        if (xQueueReceive(logQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        SPILock lock;
        if (!lock.ok()) continue;

        if (cmd.type == LOG_SENSOR_DATA) {
            File f = SD.open("/log.csv", FILE_APPEND);
            if (f) {
                if (f.size() == 0)
                    f.println("uptime_s,pressureIn,pressureOut,flow,currentL1,currentL2,currentL3,motor");
                f.printf("%lu,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%s\n",
                    cmd.uptimeSeconds,
                    cmd.pressureIn, cmd.pressureOut, cmd.flow,
                    cmd.currentL1,  cmd.currentL2,   cmd.currentL3,
                    cmd.motor ? "ON" : "OFF");
                f.close();
            }
        } else if (cmd.type == LOG_RUNTIME_SAVE) {
            File f = SD.open("/runtime.json", FILE_WRITE);
            if (f) {
                f.printf("{\"motor_runtime_s\":%lu}\n", cmd.motorRuntimeSeconds);
                f.close();
            }
        }
    }
}

// ── Sensor task ──────────────────────────────────────────────────────────────

void sensorTask(void* pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        bool isDebug = false;
        {
            SttLock lock(pdMS_TO_TICKS(5));
            if (lock.ok()) isDebug = systemState.debug;
        }

        if (isDebug) {
            SensorSnapshot snap = {};
            readAllSensors(snap);
            snap.timestamp = millis();
            { SnapLock lock; if (lock.ok()) latestSnapshot = snap; }
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(2000));
            continue;
        }

        // ── Snapshot pulse count and reset ───────────────────────────────
        uint32_t count;
        portENTER_CRITICAL(&flowMux);
        count      = pulseCount;
        pulseCount = 0;
        portEXIT_CRITICAL(&flowMux);

        // ── Read all sensors ─────────────────────────────────────────────
        SensorSnapshot snap = {};
        readAllSensors(snap);
        // Preserve original formula (elapsed ≈ 1000 ms, 8 pulses/L)
        snap.flow      = (float)(count / 1000UL) / 1000.0f / 8.0f;
        snap.timestamp = millis();

        // ── Motor runtime ────────────────────────────────────────────────
        bool motorOn = false;
        {
            SttLock lock(pdMS_TO_TICKS(5));
            if (lock.ok()) motorOn = systemState.motor;
        }
        if (motorOn) motorRuntimeSeconds++;
        snap.motorRuntimeSeconds = motorRuntimeSeconds;

        // ── Dry-run watchdog ─────────────────────────────────────────────
        if (motorOn && snap.flow == 0.0f) {
            if (motorNoFlowStart == 0) motorNoFlowStart = millis();
            else if (millis() - motorNoFlowStart > NO_FLOW_TIMEOUT_MS) {
                SttLock lock(pdMS_TO_TICKS(5));
                if (lock.ok()) {
                    systemState.error = true;
                    Serial.println("[Error] Motor running with no flow — possible dry run");
                }
            }
        } else {
            motorNoFlowStart = 0;
        }

        // ── Publish snapshot ─────────────────────────────────────────────
        { SnapLock lock; if (lock.ok()) latestSnapshot = snap; }

        // ── Wake MQTT/SSE task ───────────────────────────────────────────
        if (!OTA.isUpdating()) xSemaphoreGive(mqttPublishSem);

        // ── Enqueue data log entry ───────────────────────────────────────
        {
            unsigned long logIntervalMs = 0;
            {
                CfgLock lock(pdMS_TO_TICKS(5));
                if (lock.ok()) logIntervalMs = (unsigned long)config.log_interval_minutes * 60000UL;
            }
            static unsigned long lastLogTime = 0;
            if (logIntervalMs > 0 && millis() - lastLogTime >= logIntervalMs) {
                lastLogTime = millis();
                LogCmd cmd = {};
                cmd.type          = LOG_SENSOR_DATA;
                cmd.uptimeSeconds = millis() / 1000;
                cmd.pressureIn    = snap.pressureIn;
                cmd.pressureOut   = snap.pressureOut;
                cmd.flow          = snap.flow;
                cmd.currentL1     = snap.currentL1;
                cmd.currentL2     = snap.currentL2;
                cmd.currentL3     = snap.currentL3;
                cmd.motor         = motorOn;
                xQueueSend(logQueue, &cmd, 0);
            }
        }

        // ── Enqueue runtime save every 5 minutes ─────────────────────────
        if (millis() - lastRuntimeSave > 300000UL) {
            lastRuntimeSave = millis();
            LogCmd cmd = {};
            cmd.type                = LOG_RUNTIME_SAVE;
            cmd.motorRuntimeSeconds = motorRuntimeSeconds;
            xQueueSend(logQueue, &cmd, 0);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }
}
