#ifndef FLOW_MATH_H
#define FLOW_MATH_H

#include <stdint.h>

// Flow maths, kept free of Arduino and hardware so the host tests
// (tests/host) can check it.

// Pulses counted over `elapsedMs` -> flow in L/min, for a sensor that follows
// the datasheet form  F = k * Q + offset  (F in Hz, Q in L/min), so
// Q = (F - offset) / k. The offset makes the line cross zero at a small
// positive frequency, so a single stray pulse must not come out as negative
// flow: the result is never below 0.
inline float flowLpmFromPulses(uint32_t pulses, unsigned long elapsedMs, float k, float offset) {
    if (elapsedMs == 0 || pulses == 0 || !(k > 0.0f)) return 0.0f;
    float pulsesPerSecond = (pulses * 1000.0f) / (float)elapsedMs;
    float lpm = (pulsesPerSecond - offset) / k;
    return lpm > 0.0f ? lpm : 0.0f;
}

// Litres that flowed in a window of `elapsedMs` at `lpm`
inline double litresInWindow(float lpm, unsigned long elapsedMs) {
    return lpm > 0.0f ? (double)lpm * (double)elapsedMs / 60000.0 : 0.0;
}

#endif // FLOW_MATH_H
