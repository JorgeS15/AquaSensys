// Flow maths (code/flow_math.h)
#include "flow_math.h"
#include "check.h"
#include <cmath>

static bool near(double a, double b, double tol = 0.01) { return std::fabs(a - b) <= tol; }

int main() {
    // 3/4": F = 6.6 * Q  ->  66 pulses in 1 s is 10 L/min
    CHECK(near(flowLpmFromPulses(66, 1000, 6.6f, 0.0f), 10.0));
    // The same rate over a 2 s window
    CHECK(near(flowLpmFromPulses(132, 2000, 6.6f, 0.0f), 10.0));
    // 1": F = 8 * Q - 4  ->  76 Hz is 10 L/min
    CHECK(near(flowLpmFromPulses(76, 1000, 8.0f, -4.0f), 10.0));
    // A stray pulse below the offset's zero crossing is no flow, not negative
    CHECK(flowLpmFromPulses(1, 1000, 8.0f, 4.0f) == 0.0f);
    // Nothing to divide by: no time, no pulses, no factor
    CHECK(flowLpmFromPulses(10, 0, 6.6f, 0.0f) == 0.0f);
    CHECK(flowLpmFromPulses(0, 1000, 6.6f, 0.0f) == 0.0f);
    CHECK(flowLpmFromPulses(10, 1000, 0.0f, 0.0f) == 0.0f);

    // 10 L/min for a minute is 10 L; an hour is 600 L; no flow adds nothing
    CHECK(near(litresInWindow(10.0f, 60000), 10.0, 1e-9));
    double total = 0;
    for (int s = 0; s < 3600; s++) total += litresInWindow(10.0f, 1000);
    CHECK(near(total, 600.0, 1e-6));
    CHECK(litresInWindow(0.0f, 1000) == 0.0);
    CHECK(litresInWindow(-1.0f, 1000) == 0.0);
    return checkFailures("flow");
}
