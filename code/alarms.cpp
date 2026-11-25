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

void faultClearAll() {
    if (__atomic_exchange_n(&faults, 0u, __ATOMIC_SEQ_CST)) logEvent("fault_clear", "error cleared");
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
