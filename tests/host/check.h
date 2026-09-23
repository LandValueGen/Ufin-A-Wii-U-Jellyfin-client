// Minimal test assertions for the host test binaries -- no framework
// dependency, just counters and a non-zero exit on failure.
#pragma once
#include <cstdio>
#include <cmath>
#include <string>

namespace check {
static int failures = 0;
static int passes = 0;

inline void report(bool ok, const char* expr, const char* file, int line) {
    if (ok) {
        passes++;
    } else {
        failures++;
        fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
    }
}

inline int finish(const char* suite) {
    printf("%s: %d passed, %d failed\n", suite, passes, failures);
    return failures == 0 ? 0 : 1;
}
} // namespace check

#define CHECK(expr) check::report((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::report(((a) == (b)), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) check::report((std::fabs((double)(a) - (double)(b)) <= (eps)), \
                                            #a " ~= " #b, __FILE__, __LINE__)
#define CHECK_STR(a, b) check::report((std::string(a) == std::string(b)), #a " == " #b, __FILE__, __LINE__)
