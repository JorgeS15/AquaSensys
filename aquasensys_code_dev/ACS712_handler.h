#ifndef ACS712_HANDLER_H
#define ACS712_HANDLER_H

#include <Arduino.h>
#include "MCP3208.h"

// ACS712-05B specifications
#define ACS712_SENSITIVITY 185.0  // mV/A for 5A model
#define ACS712_VREF 5000.0        // 5V reference in mV
#define ACS712_ADC_BITS 12        // MCP3208 is 12-bit
#define ACS712_ADC_MAX 4095       // 2^12 - 1

// Channel assignments for current sensors
#define ACS712_L1_CHANNEL 4
#define ACS712_L2_CHANNEL 5
#define ACS712_L3_CHANNEL 6

// Current measurement structure
struct CurrentReadings {
    float L1;  // Phase 1 current in Amps
    float L2;  // Phase 2 current in Amps
    float L3;  // Phase 3 current in Amps
    float total; // Total current (sum of all phases)
};

// Calibration data structure
struct CalibrationData {
    float offsetL1;  // Offset for L1 in mV
    float offsetL2;  // Offset for L2 in mV
    float offsetL3;  // Offset for L3 in mV
    bool isCalibrated;
};

class ACS712Handler {
private:
    MCP3208* adc;
    float scale;
    uint8_t acFrequency;
    uint8_t numCycles;
    
    // Individual phase offsets in mV
    float offsetL1;
    float offsetL2;
    float offsetL3;
    
    // Calibration status
    bool isCalibrated;
    
    // Read AC current from a specific channel with offset
    float readACCurrent(uint8_t channel, float channelOffset);
    
    // Convert ADC reading to millivolts
    float adcToMillivolts(uint16_t adcValue);
    
    // Read raw RMS voltage in mV (for calibration)
    float readRawRMSVoltage(uint8_t channel);
    
public:
    ACS712Handler();
    
    // Initialize with MCP3208 instance
    void begin(MCP3208* adcInstance, uint8_t frequency = 50, uint8_t cycles = 5);
    
    // Legacy calibration (applies same offset to all phases)
    void setCalibration(float offsetmV = 0.0, float scaleValue = 1000.0);
    
    // Set individual offsets for each phase
    void setIndividualOffsets(float offset1, float offset2, float offset3);
    
    // Get current calibration data
    CalibrationData getCalibrationData();
    
    // Perform automatic calibration (motor must be OFF!)
    // Takes multiple samples and calculates offset for each phase
    // Returns true if successful
    bool performAutoCalibration(uint16_t samples = 100);
    
    // Read current from single phase
    float readCurrent(uint8_t channel);
    
    // Read all three phases
    CurrentReadings readAllPhases();
    
    // Get individual phase currents
    float readL1();
    float readL2();
    float readL3();
    
    // Check if calibrated
    bool getIsCalibrated() { return isCalibrated; }
};

// Global instance
extern ACS712Handler currentSensor;

#endif
