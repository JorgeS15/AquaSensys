#ifndef FREERTOS_GLUE_H
#define FREERTOS_GLUE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ── Synchronization handles ──────────────────────────────────────────────────
extern SemaphoreHandle_t spiMutex;       // protects MCP3208 + SD SPI bus
extern SemaphoreHandle_t snapshotMutex;  // protects SensorSnapshot
extern SemaphoreHandle_t stateMutex;     // protects SystemState
extern SemaphoreHandle_t configMutex;    // protects Config struct
extern SemaphoreHandle_t mqttPublishSem; // binary: sensorTask -> mqttTask

// ── Log command queue ────────────────────────────────────────────────────────
enum LogCmdType : uint8_t { LOG_SENSOR_DATA, LOG_RUNTIME_SAVE };

struct LogCmd {
    LogCmdType    type;
    unsigned long uptimeSeconds;
    float         pressureIn, pressureOut, flow;
    float         currentL1, currentL2, currentL3;
    bool          motor;
    unsigned long motorRuntimeSeconds; // used by LOG_RUNTIME_SAVE
};

extern QueueHandle_t logQueue;

// ── Shared data structs ──────────────────────────────────────────────────────
struct SensorSnapshot {
    float         pressureIn, pressureOut, pressure;
    float         ambientTemp, waterTemp, temperature;
    float         flow;
    float         currentL1, currentL2, currentL3, currentTotal;
    unsigned long motorRuntimeSeconds;
    unsigned long timestamp; // millis() when taken
};

struct SystemState {
    bool motor;
    bool manualOverride;
    bool manualMotorState;
    bool mainSwitch;
    bool lights;
    bool error;
    bool rebootRequested;
    bool debug;
};

extern SensorSnapshot latestSnapshot;
extern SystemState    systemState;

// ── RAII mutex lock ──────────────────────────────────────────────────────────
class MutexLock {
public:
    explicit MutexLock(SemaphoreHandle_t m, TickType_t timeout = portMAX_DELAY)
        : m_(m), held_(m && xSemaphoreTake(m, timeout) == pdTRUE) {}
    ~MutexLock() { if (held_) xSemaphoreGive(m_); }
    bool ok() const { return held_; }
private:
    SemaphoreHandle_t m_;
    bool held_;
};

struct SPILock  : public MutexLock { explicit SPILock (TickType_t t=portMAX_DELAY) : MutexLock(spiMutex,      t){} };
struct SnapLock : public MutexLock { explicit SnapLock(TickType_t t=portMAX_DELAY) : MutexLock(snapshotMutex, t){} };
struct SttLock  : public MutexLock { explicit SttLock (TickType_t t=portMAX_DELAY) : MutexLock(stateMutex,    t){} };
struct CfgLock  : public MutexLock { explicit CfgLock (TickType_t t=portMAX_DELAY) : MutexLock(configMutex,   t){} };

// Call once at the very start of setup() before any task creation or SD access
void initFreeRTOSGlue();

#endif
