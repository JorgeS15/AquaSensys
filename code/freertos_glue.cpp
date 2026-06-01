#include "freertos_glue.h"

SemaphoreHandle_t spiMutex       = nullptr;
SemaphoreHandle_t snapshotMutex  = nullptr;
SemaphoreHandle_t stateMutex     = nullptr;
SemaphoreHandle_t configMutex    = nullptr;
SemaphoreHandle_t mqttPublishSem = nullptr;

QueueHandle_t logQueue = nullptr;

SensorSnapshot latestSnapshot = {};

SystemState systemState = {
    /* motor            */ false,
    /* manualOverride   */ false,
    /* manualMotorState */ false,
    /* mainSwitch       */ true,
    /* lights           */ true,
    /* error            */ false,
    /* rebootRequested  */ false,
    /* debug            */ false,
};

void initFreeRTOSGlue() {
    spiMutex       = xSemaphoreCreateMutex();
    snapshotMutex  = xSemaphoreCreateMutex();
    stateMutex     = xSemaphoreCreateMutex();
    configMutex    = xSemaphoreCreateMutex();
    mqttPublishSem = xSemaphoreCreateBinary();
    logQueue       = xQueueCreate(8, sizeof(LogCmd));
}
