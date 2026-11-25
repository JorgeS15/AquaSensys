// The config fields alarms.cpp reads, with the firmware's defaults
#pragma once
#include "Arduino.h"
struct Config {
    bool leak_alarm_enabled = true;
    int leak_max_minutes = 60;
    bool leak_quiet_enabled = false;
    String leak_quiet_start = "01:00";
    String leak_quiet_end = "06:00";
    int leak_quiet_minutes = 5;
    int short_cycle_max_starts = 20;
    bool short_cycle_stop_pump = false;
};
extern Config config;
