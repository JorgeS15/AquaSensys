#include "ACS712_handler.h"

// Global instance
ACS712Handler currentSensor;

ACS712Handler::ACS712Handler() : 
    adc(nullptr), 
    scale(1000.0), 
    offsetL1(0.0),
    offsetL2(0.0),
    offsetL3(0.0),
    acFrequency(50),
    numCycles(5),
    isCalibrated(false) {
}

void ACS712Handler::begin(MCP3208* adcInstance, uint8_t frequency, uint8_t cycles) {
    adc = adcInstance;
    acFrequency = frequency;
    numCycles = cycles;
    
    Serial.println("ACS712 Handler initialized");
    Serial.printf("AC Frequency: %d Hz, Cycles: %d\n", acFrequency, numCycles);
}

void ACS712Handler::setCalibration(float offsetmV, float scaleValue) {
    // Legacy function - sets same offset for all phases
    offsetL1 = offsetmV;
    offsetL2 = offsetmV;
    offsetL3 = offsetmV;
    scale = scaleValue;
    isCalibrated = true;
    
    Serial.printf("ACS712 Calibration - Global Offset: %.2f mV, Scale: %.2f\n", offsetmV, scale);
}

void ACS712Handler::setIndividualOffsets(float offset1, float offset2, float offset3) {
    offsetL1 = offset1;
    offsetL2 = offset2;
    offsetL3 = offset3;
    isCalibrated = true;
    
    Serial.printf("ACS712 Individual Offsets - L1: %.2f mV, L2: %.2f mV, L3: %.2f mV\n", 
                  offset1, offset2, offset3);
}

CalibrationData ACS712Handler::getCalibrationData() {
    CalibrationData data;
    data.offsetL1 = offsetL1;
    data.offsetL2 = offsetL2;
    data.offsetL3 = offsetL3;
    data.isCalibrated = isCalibrated;
    return data;
}

bool ACS712Handler::performAutoCalibration(uint16_t samples) {
    if (!adc) {
        Serial.println("Error: ADC not initialized - cannot calibrate!");
        return false;
    }
    
    Serial.println("\n=== Starting ACS712 Auto-Calibration ===");
    Serial.println("IMPORTANT: Motor must be OFF!");
    Serial.printf("Taking %d samples per phase...\n", samples);
    
    float sumL1 = 0.0;
    float sumL2 = 0.0;
    float sumL3 = 0.0;
    
    // Take multiple samples for each phase
    for (uint16_t i = 0; i < samples; i++) {
        sumL1 += readRawRMSVoltage(ACS712_L1_CHANNEL);
        sumL2 += readRawRMSVoltage(ACS712_L2_CHANNEL);
        sumL3 += readRawRMSVoltage(ACS712_L3_CHANNEL);
        
        // Progress indicator
        if (i % 20 == 0) {
            Serial.print(".");
        }
        
        delay(10); // Small delay between samples
    }
    Serial.println();
    
    // Calculate average RMS voltage for each phase (this is the zero-current offset)
    float avgL1 = sumL1 / samples;
    float avgL2 = sumL2 / samples;
    float avgL3 = sumL3 / samples;
    
    // Convert to current equivalent in mA (what we need to subtract)
    // Current (mA) = RMS Voltage (mV) / Sensitivity (mV/A) * 1000
    offsetL1 = -(avgL1 / ACS712_SENSITIVITY) * 1000.0;
    offsetL2 = -(avgL2 / ACS712_SENSITIVITY) * 1000.0;
    offsetL3 = -(avgL3 / ACS712_SENSITIVITY) * 1000.0;
    
    isCalibrated = true;
    
    Serial.println("=== Calibration Complete ===");
    Serial.printf("L1 Offset: %.2f mV (RMS voltage: %.2f mV)\n", offsetL1, avgL1);
    Serial.printf("L2 Offset: %.2f mV (RMS voltage: %.2f mV)\n", offsetL2, avgL2);
    Serial.printf("L3 Offset: %.2f mV (RMS voltage: %.2f mV)\n", offsetL3, avgL3);
    Serial.println("============================\n");
    
    return true;
}

float ACS712Handler::adcToMillivolts(uint16_t adcValue) {
    // Convert 12-bit ADC value to millivolts (0-5000mV range)
    return (adcValue * ACS712_VREF) / ACS712_ADC_MAX;
}

float ACS712Handler::readRawRMSVoltage(uint8_t channel) {
    if (!adc) {
        return 0.0;
    }
    
    // Calculate number of samples needed
    uint16_t periodMicros = (1000000 / acFrequency);
    uint16_t totalSamples = numCycles * (periodMicros / 1000);
    
    if (totalSamples < 50) totalSamples = 50;
    if (totalSamples > 200) totalSamples = 200;
    
    float sumSquares = 0.0;
    uint16_t validSamples = 0;
    
    for (uint16_t i = 0; i < totalSamples; i++) {
        uint16_t adcValue = adc->analogRead(channel);
        float voltageMv = adcToMillivolts(adcValue);
        
        // Center around 2500mV (half of 5V)
        float centeredMv = voltageMv - 2500.0;
        
        sumSquares += (centeredMv * centeredMv);
        validSamples++;
        
        delayMicroseconds(100);
    }
    
    if (validSamples == 0) {
        return 0.0;
    }
    
    // Calculate RMS voltage in mV
    return sqrt(sumSquares / validSamples);
}

float ACS712Handler::readACCurrent(uint8_t channel, float channelOffset) {
    if (!adc) {
        Serial.println("Error: ADC not initialized!");
        return 0.0;
    }
    
    // Get raw RMS voltage
    float rmsVoltageMv = readRawRMSVoltage(channel);
    
    // Convert to current using sensitivity (185 mV/A for ACS712-05B)
    // Current (A) = RMS Voltage (mV) / Sensitivity (mV/A)
    float currentmA = (rmsVoltageMv / ACS712_SENSITIVITY) * 1000.0;
    
    // Apply calibration offset
    float calibratedCurrentmA = (currentmA + channelOffset) / scale;
    
    // Convert to Amps
    float currentA = calibratedCurrentmA;
    
    // Filter out noise - readings below 0.1A are likely noise
    if (currentA < 0.1) {
        currentA = 0.0;
    }
    
    // Return absolute value
    return abs(currentA);
}

float ACS712Handler::readCurrent(uint8_t channel) {
    // Use appropriate offset based on channel
    float offset = 0.0;
    if (channel == ACS712_L1_CHANNEL) offset = offsetL1;
    else if (channel == ACS712_L2_CHANNEL) offset = offsetL2;
    else if (channel == ACS712_L3_CHANNEL) offset = offsetL3;
    
    return readACCurrent(channel, offset);
}

float ACS712Handler::readL1() {
    return readACCurrent(ACS712_L1_CHANNEL, offsetL1);
}

float ACS712Handler::readL2() {
    return readACCurrent(ACS712_L2_CHANNEL, offsetL2);
}

float ACS712Handler::readL3() {
    return readACCurrent(ACS712_L3_CHANNEL, offsetL3);
}

CurrentReadings ACS712Handler::readAllPhases() {
    CurrentReadings readings;
    
    readings.L1 = readL1();
    readings.L2 = readL2();
    readings.L3 = readL3();
    readings.total = readings.L1 + readings.L2 + readings.L3;
    
    return readings;
}
