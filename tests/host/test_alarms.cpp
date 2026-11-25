// Leak detection, pump short-cycling and fault causes (code/alarms.cpp)
#include "config_manager.h"
#include "time_log.h"
#include "alarms.h"
#include "check.h"
#include <cstring>

Config config;
int fakeMinuteOfDay = -1;
uint32_t fakeMillis = 0;
int events = 0;
void logEvent(const char*, const char*) { events++; }

// `seconds` passes of the sensor task at a steady flow
static void flowFor(float lpm, int seconds) {
    for (int i = 0; i < seconds; i++) updateLeakState(lpm);
}
static void stopWater() { flowFor(0, LEAK_GAP_S); }

static void testLeak() {
    // Normal use: a 15 min shower, a pause, 20 min more
    flowFor(8, 15 * 60); flowFor(0, 5 * 60); flowFor(8, 20 * 60);
    CHECK(!leakActive());
    stopWater();

    // A small constant flow: no alarm at 59 min, alarm past 60
    flowFor(0.5f, 59 * 60);
    CHECK(!leakActive());
    flowFor(0.5f, 61);
    CHECK(leakActive());
    CHECK(strcmp(leakReason(), "continuous") == 0);
    CHECK(flowRunMinutes() >= 60);

    // A gap shorter than LEAK_GAP_S keeps it; a real stop clears it
    flowFor(0, LEAK_GAP_S - 1); flowFor(0.5f, 5);
    CHECK(leakActive());
    stopWater();
    CHECK(!leakActive());
    CHECK(flowRunMinutes() == 0);

    // A very low flow that reads zero every other second is still continuous
    for (int i = 0; i < 61 * 60; i++) updateLeakState(i % 2 ? 0.2f : 0.0f);
    CHECK(leakActive());
    stopWater();

    // A reading that can't be taken (NAN) is not flow
    flowFor(NAN, 61 * 60);
    CHECK(!leakActive());

    // Quiet hours, 01:00-06:00: 5 min of flow at 02:00
    config.leak_quiet_enabled = true;
    fakeMinuteOfDay = 2 * 60;
    flowFor(3, 4 * 60);
    CHECK(!leakActive());
    flowFor(3, 61);
    CHECK(leakActive());
    CHECK(strcmp(leakReason(), "quiet_hours") == 0);
    stopWater();

    // Without the time the quiet-hours rule waits
    fakeMinuteOfDay = -1;
    flowFor(3, 10 * 60);
    CHECK(!leakActive());
    stopWater();

    // A window across midnight, 23:00-06:00, at 23:30; and not at noon
    config.leak_quiet_start = "23:00";
    fakeMinuteOfDay = 23 * 60 + 30;
    flowFor(3, 5 * 60 + 1);
    CHECK(leakActive());
    stopWater();
    fakeMinuteOfDay = 12 * 60;
    flowFor(3, 10 * 60);
    CHECK(!leakActive());
    stopWater();
    config.leak_quiet_enabled = false;

    // leak_max_minutes 0 turns the continuous rule off; disabled = never
    config.leak_max_minutes = 0;
    flowFor(1, 120 * 60);
    CHECK(!leakActive());
    stopWater();
    config.leak_max_minutes = 60;
    config.leak_alarm_enabled = false;
    flowFor(1, 70 * 60);
    CHECK(!leakActive());
    stopWater();
    config.leak_alarm_enabled = true;
}

// One start-stop cycle of `periodMs`, as the control task would report it
static void cycle(uint32_t periodMs, bool automatic = true) {
    updateShortCycle(true, automatic);
    fakeMillis += periodMs / 2;
    updateShortCycle(false, automatic);
    fakeMillis += periodMs / 2;
    updateShortCycle(false, automatic);
}

static void testShortCycle() {
    fakeMillis = 0xFFFF0000u;   // across the millis() wrap
    for (int i = 0; i < 20; i++) cycle(170000);
    CHECK(startsLastHour() == 20);
    CHECK(!shortCycleAlarm());
    cycle(1000);
    CHECK(shortCycleAlarm());
    CHECK(faultActive(FAULT_SHORT_CYCLE));

    // As a warning, it goes once the starts age out of the hour
    fakeMillis += 3600000;
    updateShortCycle(false, true);
    CHECK(startsLastHour() == 0);
    CHECK(!shortCycleAlarm());
    CHECK(!faultActive(FAULT_SHORT_CYCLE));

    // Starts in manual mode don't count
    for (int i = 0; i < 50; i++) cycle(1000, false);
    CHECK(startsLastHour() == 0);

    // Stopping the pump latches it until the error is cleared, which also
    // forgets the starts so far
    config.short_cycle_stop_pump = true;
    for (int i = 0; i < 21; i++) cycle(1000);
    CHECK(shortCycleAlarm());
    fakeMillis += 3600000;
    updateShortCycle(false, true);
    CHECK(shortCycleAlarm());
    CHECK(faultActive(FAULT_SHORT_CYCLE));
    faultClearAll();
    updateShortCycle(false, true);
    CHECK(!shortCycleAlarm());
    CHECK(startsLastHour() == 0);
    config.short_cycle_stop_pump = false;

    // 0 turns it off; the start history is bounded
    config.short_cycle_max_starts = 0;
    for (int i = 0; i < 200; i++) cycle(1000);
    CHECK(!shortCycleAlarm());
    CHECK(startsLastHour() == 128);
    config.short_cycle_max_starts = 20;
}

static void testFaults() {
    faultClearAll();
    int before = events;
    faultSet(FAULT_DRY_RUN, "test");
    faultSet(FAULT_DRY_RUN, "again");             // already set: logged once
    CHECK(events == before + 1);
    faultSet(FAULT_OVERCURRENT, "test");
    CHECK(faultBits() == (FAULT_DRY_RUN | FAULT_OVERCURRENT));
    faultClear(FAULT_DRY_RUN);
    CHECK(!faultActive(FAULT_DRY_RUN));
    CHECK(faultActive(FAULT_OVERCURRENT));
    faultClearAll();
    CHECK(faultBits() == 0);
    CHECK(strcmp(faultName(FAULT_PHASE_IMBALANCE), "phase_imbalance") == 0);
    CHECK(strcmp(faultName(FAULT_SHORT_CYCLE), "short_cycle") == 0);
}

int main() {
    testLeak();
    testShortCycle();
    testFaults();
    return checkFailures("alarms");
}
