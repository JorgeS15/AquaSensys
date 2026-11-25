// Local time and the event log, faked: the tests set the minute of the day
#pragma once
extern int fakeMinuteOfDay;   // -1 = time unknown
inline int localMinuteOfDay() { return fakeMinuteOfDay; }
void logEvent(const char* type, const char* detail);
