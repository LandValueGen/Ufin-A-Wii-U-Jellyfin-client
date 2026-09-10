// Host stand-in for wut's <coreinit/debug.h>: OSReport goes to stdout.
#pragma once
#include <cstdarg>
#include <cstdio>

static inline void OSReport(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
