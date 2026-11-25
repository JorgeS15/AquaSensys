// Minimal test helper: CHECK() records failures instead of aborting, so a run
// reports every broken expectation. main() returns checkFailures().
#pragma once
#include <cstdio>
inline int& failureCount() { static int n = 0; return n; }
inline int& checkCount() { static int n = 0; return n; }
#define CHECK(cond) do { checkCount()++; if (!(cond)) { failureCount()++; \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
inline int checkFailures(const char* suite) {
    std::printf("%s: %d checks, %d failed\n", suite, checkCount(), failureCount());
    return failureCount() ? 1 : 0;
}
