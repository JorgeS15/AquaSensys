#include "control_task.h"
#include "freertos_glue.h"
#include "config_manager.h"
#include <Arduino.h>

#define MOTOR_PIN  26
#define LED_RED    32
#define LED_GREEN  23
#define LED_BLUE   22

static void applyMotorControl(const SensorSnapshot& snap, SystemState& st,
                               float minP, float maxP) {
    if (!st.mainSwitch) {
        st.motor = false;
    } else if (st.manualOverride) {
        st.motor = st.manualMotorState;
    } else if (!st.error) {
        if (snap.pressure <= minP)       st.motor = true;
        else if (snap.pressure >= maxP)  st.motor = false;
    }
    digitalWrite(MOTOR_PIN, st.motor ? HIGH : LOW);
}

static void applyErrorCheck(const SensorSnapshot& snap, SystemState& st,
                             float maxCurrent, float maxImbalance) {
    if (st.manualOverride) {
        st.error = false;
        return;
    }
    if (snap.pressure < 1.0f || snap.pressure > 5.0f) {
        st.error = true;
    }
    if (st.motor && snap.currentTotal > maxCurrent) {
        st.error = true;
        Serial.println("[Error] Overcurrent detected");
    }
    if (st.motor) {
        float maxI = max(snap.currentL1, max(snap.currentL2, snap.currentL3));
        float minI = min(snap.currentL1, min(snap.currentL2, snap.currentL3));
        if (maxI - minI > maxImbalance) {
            st.error = true;
            Serial.println("[Error] Phase imbalance detected");
        }
    }
    if (st.error) {
        st.motor = false;
        digitalWrite(MOTOR_PIN, LOW);
    }
}

static void applyLights(const SystemState& st) {
    if (!st.lights) {
        ledcWrite(LED_RED, 0); ledcWrite(LED_GREEN, 0); ledcWrite(LED_BLUE, 0);
        return;
    }
    if (st.error) {
        ledcWrite(LED_RED,   ((millis() % 1000) < 500) ? 255 : 0);
        ledcWrite(LED_GREEN, 0);
        ledcWrite(LED_BLUE,  0);
    } else if (!st.mainSwitch) {
        ledcWrite(LED_RED, 255); ledcWrite(LED_GREEN, 0); ledcWrite(LED_BLUE, 0);
    } else if (st.manualOverride) {
        ledcWrite(LED_RED,   !st.motor ? 255 : 0);
        ledcWrite(LED_GREEN, !st.motor ?  64 : 0);
        ledcWrite(LED_BLUE,   st.motor ? 255 : 0);
    } else {
        ledcWrite(LED_RED, 0);
        ledcWrite(LED_GREEN, (!st.motor && (millis() % 1000) < 50) ? 255 : 0);
        ledcWrite(LED_BLUE,   st.motor ? 255 : 0);
    }
}

void controlTask(void* pvParameters) {
    for (;;) {
        // Copy snapshot and config under their mutexes
        SensorSnapshot snap = {};
        { SnapLock lock(pdMS_TO_TICKS(5)); if (lock.ok()) snap = latestSnapshot; }

        float minP = 2.5f, maxP = 3.5f, maxCurr = 15.0f, maxImb = 3.0f;
        {
            CfgLock lock(pdMS_TO_TICKS(5));
            if (lock.ok()) {
                minP    = config.min_pressure;
                maxP    = config.max_pressure;
                maxCurr = config.max_current;
                maxImb  = config.max_phase_imbalance;
            }
        }

        // Modify state under stateMutex
        {
            SttLock lock(pdMS_TO_TICKS(5));
            if (lock.ok()) {
                applyErrorCheck(snap, systemState, maxCurr, maxImb);
                applyMotorControl(snap, systemState, minP, maxP);
                applyLights(systemState);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz control loop
    }
}
