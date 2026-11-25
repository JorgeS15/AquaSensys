#include "alarms.h"
#include "config_manager.h"
#include "time_log.h"

// Written by the main loop only; other tasks read single words
static volatile bool leak = false;
static const char* volatile leakWhy = "";
static uint32_t runSeconds = 0;      // seconds of flow since it last stopped
static uint32_t quietRunSeconds = 0; // of those, seconds inside the quiet hours
static uint32_t gapSeconds = 0;      // seconds without flow
static float runMaxFlow = 0;         // highest flow in the current run, for the event log

// "HH:MM" -> minutes since midnight, or -1
static int parseHhMm(const String& s) {
    int colon = s.indexOf(':');
    if (colon < 1) return -1;
    int h = s.substring(0, colon).toInt();
    int m = s.substring(colon + 1).toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

// True when `now` (minutes since midnight) falls in [start, end), which may
// wrap past midnight (e.g. 23:00-06:00)
static bool inWindow(int now, int start, int end) {
    if (now < 0 || start < 0 || end < 0 || start == end) return false;
    return start < end ? (now >= start && now < end) : (now >= start || now < end);
}

static void raiseLeak(const char* why) {
    leakWhy = why;
    leak = true;
    char detail[96];
    snprintf(detail, sizeof(detail), "%s: flowing for %lu min; up to %.1f L/min",
             why, (unsigned long)(runSeconds / 60), runMaxFlow);
    logEvent("leak", detail);
}

void updateLeakState(float flowLpm) {
    bool flowing = !isnan(flowLpm) && flowLpm > 0.0f;

    if (flowing) {
        gapSeconds = 0;
        if (flowLpm > runMaxFlow) runMaxFlow = flowLpm;
    } else if (runSeconds == 0) {
        return;                                  // no water, no run going on
    } else if (++gapSeconds >= LEAK_GAP_S) {
        // The water really stopped: start again, and clear the alarm
        if (leak) {
            char detail[64];
            snprintf(detail, sizeof(detail), "water stopped after %lu min",
                     (unsigned long)(runSeconds / 60));
            logEvent("leak_clear", detail);
        }
        leak = false;
        leakWhy = "";
        runSeconds = 0;
        quietRunSeconds = 0;
        gapSeconds = 0;
        runMaxFlow = 0;
        return;
    }
    // A short gap (a pulse missed at a very low flow) is part of the run
    runSeconds++;

    bool quiet = config.leak_quiet_enabled &&
        inWindow(localMinuteOfDay(), parseHhMm(config.leak_quiet_start), parseHhMm(config.leak_quiet_end));
    quietRunSeconds = quiet ? quietRunSeconds + 1 : 0;

    if (!config.leak_alarm_enabled) {
        leak = false;
        leakWhy = "";
        return;
    }
    if (leak) return;

    if (config.leak_max_minutes > 0 && runSeconds >= (uint32_t)config.leak_max_minutes * 60UL) {
        raiseLeak("continuous");
    } else if (quiet && quietRunSeconds >= (uint32_t)max(1, config.leak_quiet_minutes) * 60UL) {
        raiseLeak("quiet_hours");
    }
}

// Set by the control and sensor tasks, cleared from the web and MQTT tasks:
// every change is an atomic read-modify-write, so none of them loses a bit
static uint32_t faults = 0;

const char* faultName(uint32_t bit) {
    switch (bit) {
        case FAULT_DRY_RUN:         return "dry_run";
        case FAULT_OVERCURRENT:     return "overcurrent";
        case FAULT_PHASE_IMBALANCE: return "phase_imbalance";
        case FAULT_PRESSURE:        return "pressure";
        case FAULT_SHORT_CYCLE:     return "short_cycle";
        default:                    return "unknown";
    }
}

void faultSet(uint32_t bit, const char* detail) {
    uint32_t before = __atomic_fetch_or(&faults, bit, __ATOMIC_SEQ_CST);
    if (!(before & bit)) logEvent(faultName(bit), detail);
}

void faultClear(uint32_t bits) {
    __atomic_fetch_and(&faults, ~bits, __ATOMIC_SEQ_CST);
}

static volatile bool shortCycleResetPending = false;

void faultClearAll() {
    if (__atomic_exchange_n(&faults, 0u, __ATOMIC_SEQ_CST)) logEvent("fault_clear", "error cleared");
    // The starts that tripped the alarm would trip it again at once
    shortCycleResetPending = true;
}

// Start times (millis) of the automatic starts in the last hour, oldest first
static const size_t MAX_STARTS = 128;
static uint32_t starts[MAX_STARTS];
static size_t startHead = 0, startCount = 0;
static volatile uint32_t startsHour = 0;
static volatile bool shortCycle = false;
static bool lastMotor = false;
static const uint32_t HOUR_MS = 3600000UL;

void updateShortCycle(bool motorOn, bool automatic) {
    uint32_t now = millis();
    if (shortCycleResetPending) {
        shortCycleResetPending = false;
        startHead = startCount = 0;
        shortCycle = false;
    }
    if (motorOn && !lastMotor && automatic) {
        if (startCount == MAX_STARTS) {            // full: drop the oldest
            startHead = (startHead + 1) % MAX_STARTS;
            startCount--;
        }
        starts[(startHead + startCount) % MAX_STARTS] = now;
        startCount++;
    }
    lastMotor = motorOn;
    // Forget starts older than an hour
    while (startCount && (uint32_t)(now - starts[startHead]) >= HOUR_MS) {
        startHead = (startHead + 1) % MAX_STARTS;
        startCount--;
    }
    startsHour = startCount;

    int limit = config.short_cycle_max_starts;
    bool over = limit > 0 && startCount > (size_t)limit;
    if (over && !shortCycle) {
        char detail[48];
        snprintf(detail, sizeof(detail), "%u starts in the last hour (limit %d)", (unsigned)startCount, limit);
        faultSet(FAULT_SHORT_CYCLE, detail);
    }
    if (!over && shortCycle && !config.short_cycle_stop_pump) {
        faultClear(FAULT_SHORT_CYCLE);   // a warning only: it goes as the rate drops
    }
    // Latched with the error when it stops the pump; faultClearAll() ends it
    shortCycle = over || (shortCycle && config.short_cycle_stop_pump && faultActive(FAULT_SHORT_CYCLE));
}

uint32_t startsLastHour() {
    return startsHour;
}

bool shortCycleAlarm() {
    return shortCycle;
}

bool faultActive(uint32_t bit) {
    return (__atomic_load_n(&faults, __ATOMIC_SEQ_CST) & bit) != 0;
}

uint32_t faultBits() {
    return __atomic_load_n(&faults, __ATOMIC_SEQ_CST);
}

bool leakActive() {
    return leak;
}

const char* leakReason() {
    return leakWhy;
}

uint32_t flowRunMinutes() {
    return runSeconds / 60;
}
