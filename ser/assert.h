#ifndef ASSERT_H
#define ASSERT_H

#include "log.h"

// Basic assertion macro (always checks)
#ifdef LOG_DISABLE
#define assert(c, fmt, ...) \
    do { \
        if (!(c)) { \
            fprintf(stderr, fmt "\n" __VA_OPT__(, ) __VA_ARGS__); \
            exit(1); \
        } \
    } while (false)
#else // LOG_DISABLE
#define assert(c, ...) \
    do { \
        if (!(c)) { \
            log_error(__VA_ARGS__); \
            exit(1); \
        } \
    } while (false)
#endif // LOG_DISABLE

// Only check if NDEBUG isn't defined
#ifdef NDEBUG
#define debug_assert(c, ...) (void)0
#else
#define debug_assert(c, ...) assert(c, __VA_ARGS__)
#endif

#define assert_eq(a, b, ...) assert(a == b, __VA_ARGS__)

// Assert allocation succeeded (var != NULL)
#define assert_alloc(var) debug_assert(var != NULL, "Failed to allocate memory for " #var " (Out of memory ?)")

#endif
