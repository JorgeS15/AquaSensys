// Just enough of Arduino.h for the host tests (tests/host/run.sh)
#pragma once
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <algorithm>
using std::max; using std::min; using std::isnan;

class String : public std::string {
public:
    String() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    int indexOf(char c) const { auto p = find(c); return p == npos ? -1 : (int)p; }
    String substring(int a, int b = -1) const { return String(b < 0 ? substr(a) : substr(a, b - a)); }
    long toInt() const { return atol(c_str()); }
    unsigned length() const { return (unsigned)size(); }
};

// The tests move the clock themselves
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
