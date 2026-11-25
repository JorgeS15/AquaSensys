#ifndef ALARMS_H
#define ALARMS_H

#include <Arduino.h>

// Alarms worked out from the readings, once a second, by the main loop. The
// state functions are safe to call from the web and MQTT tasks.

// ---- Leak detection (every unit) ----
// Two rules, each on its own:
//   continuous: water has not stopped for leak_max_minutes. A dripping tap or a
//     running cistern never lets the flow reach zero; normal use does. A gap
//     shorter than LEAK_GAP_S (a pulse missed at a very low flow) doesn't count
//     as stopping.
//   quiet hours: between leak_quiet_start and leak_quiet_end, water has been
//     flowing for leak_quiet_minutes. Needs the real time (NTP).
// The alarm clears once the water has stopped for LEAK_GAP_S.
void updateLeakState(float flowLpm);
bool leakActive();
// "continuous" or "quiet_hours" while active, "" otherwise
const char* leakReason();
// Minutes the water has been flowing without stopping (0 once it has stopped)
uint32_t flowRunMinutes();

static const uint32_t LEAK_GAP_S = 30;

// ---- Pump short-cycling (Controller) ----
// A pump that starts many times an hour usually means a pressure tank that has
// lost its air charge, or a leak; it wears the motor and the relay. Starts
// made by the automatic control are counted over the last hour; above
// short_cycle_max_starts the FAULT_SHORT_CYCLE alarm is set. Without
// short_cycle_stop_pump it is a warning that clears once the rate drops; with
// it, it latches the error (and stops the pump) until the error is cleared,
// which also forgets the starts counted so far.
// Called by the control task on every pass, with the motor state it decided.
void updateShortCycle(bool motorOn, bool automatic);
uint32_t startsLastHour();
bool shortCycleAlarm();

// ---- Fault causes ----
// "System Error" (the `error` flag) says that something is wrong; these say
// what. A Controller latches them, like the error: they stay set until the
// error is cleared (main switch off and on, or from Home Assistant). On a
// Monitor the pressure fault follows the live reading.
enum FaultBit : uint32_t {
    FAULT_DRY_RUN         = 1u << 0,   // motor on, no flow for 10 s
    FAULT_OVERCURRENT     = 1u << 1,   // total current above max_current
    FAULT_PHASE_IMBALANCE = 1u << 2,   // phases further apart than max_phase_imbalance
    FAULT_PRESSURE        = 1u << 3,   // pressure below 1 bar or above 5 bar
    FAULT_SHORT_CYCLE     = 1u << 4,   // too many pump starts an hour
};
static const uint32_t FAULT_ALL = 0x1F;

// Sets a cause; logs an event the first time. `detail` is free text.
void faultSet(uint32_t bit, const char* detail);
// Clears causes (without logging); faultClearAll() logs that the error was cleared.
void faultClear(uint32_t bits);
void faultClearAll();
bool faultActive(uint32_t bit);
uint32_t faultBits();
// Short name of a single cause: "dry_run", "overcurrent", ...
const char* faultName(uint32_t bit);

#endif // ALARMS_H
