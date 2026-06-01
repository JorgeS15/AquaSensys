#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "freertos_glue.h"

// Initialized from SD in setup() via loadRuntime(); maintained by sensorTask
extern unsigned long motorRuntimeSeconds;

// ISR — registered via attachInterrupt() in code.ino
void IRAM_ATTR pulseCounter();

// ISR spinlock and pulse counter — defined in sensor_task.cpp
extern portMUX_TYPE      flowMux;
extern volatile uint32_t pulseCount;

// SPI-safe averaged voltage read from MCP3208 (holds spiMutex for full sample loop)
float readMCP3208Average(int channel, int samples);

void sensorTask (void* pvParameters); // Core 1, priority 3
void loggingTask(void* pvParameters); // Core 0, priority 1

#endif
