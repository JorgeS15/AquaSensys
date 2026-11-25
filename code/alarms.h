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

#endif // ALARMS_H
