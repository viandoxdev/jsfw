#include "log.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE_BUFFER_SIZE 1024
#define SOURCE_BUFFER_SIZE 128
static char BASE_BUFFER[BASE_BUFFER_SIZE] = {0};
static char SOURCE_BUFFER[SOURCE_BUFFER_SIZE] = {0};

// TODO: mutex
static Logger LOGGER = {.sevs = Info | Warning | Error};

void logger_set_fd(FILE *fd) { LOGGER.fd = fd; }

void logger_enable_severities(LogSeverities sevs) { LOGGER.sevs |= sevs; }

void logger_disable_severities(LogSeverities sevs) { LOGGER.sevs &= ~sevs; }

void logger_set_severities(LogSeverities sevs) { LOGGER.sevs = sevs; }

void logger_init() { LOGGER.initialized = true; }

// Logging function, should be rarely called by itself (use the log_* macros instead)
// message takes the form: (file:line?) SEVERITY func > fmt ...
// line can be ignored if negative
void _log_severity(LogSeverity sev, const char *func, const char *file, const int line, char *fmt, ...) {
    if (!LOGGER.initialized) {
        fprintf(stderr, "Trying to log, but the logger hasn't been initialized.\n");
        return;
    }

    // Ignore if the logger doesn't have a configured target or if the severity is ignored.
    if (LOGGER.fd == NULL || !(LOGGER.sevs & sev)) {
        return;
    }

    // format source in second half of buffer
    int source_len;
    if (line >= 0) {
        source_len = snprintf(SOURCE_BUFFER, SOURCE_BUFFER_SIZE, "(%s:%d)", file, line);
    } else {
        source_len = snprintf(SOURCE_BUFFER, SOURCE_BUFFER_SIZE, "(%s)", file);
    }

    // Keep track of width for alignment
    if (source_len > LOGGER.source_width) {
        LOGGER.source_width = source_len;
    }

    // "format" severity
    const char *sev_str;
    switch (sev) {
    case Trace:
        sev_str = "\033[0;35mTRACE";
        break;
    case Debug:
        sev_str = "\033[0;34mDEBUG";
        break;
    case Info:
        sev_str = "\033[0;32mINFO ";
        break;
    case Warning:
        sev_str = "\033[0;33mWARN ";
        break;
    case Error:
        sev_str = "\033[0;31mERROR";
        break;
    default:
        sev_str = "\033[0;31m?????";
        break;
    }

    // no format for func since there is nothing to do

    // SAFETY: func should always come from the __func__ macro, which shouldn't allow buffer overflow.
    int func_len = strlen(func);

    // Keep track of width for alignment
    if (func_len > LOGGER.func_width) {
        LOGGER.func_width = func_len;
    }

    // Final string buffer
    char *buf = BASE_BUFFER;
    int prefix_len = snprintf(
        buf,
        BASE_BUFFER_SIZE / 2,
        "\033[0;2m%-*s %s \033[0;1m%-*s \033[0;2m> ",
        LOGGER.source_width,
        SOURCE_BUFFER,
        sev_str,
        LOGGER.func_width,
        func
    );

    const char *suffix = "\033[0m\n";
    const int suffix_len = 5;

    // max slice of the buffer used by the message
    char *str = buf + prefix_len;
    int str_size = BASE_BUFFER_SIZE - prefix_len - suffix_len; // -1 for the trailing newline

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(str, str_size, fmt, args);
    va_end(args);

    // Make sure we have enough space in the BASE_BUFFER, allocate if we don't
    if (len >= str_size) {
        buf = malloc(prefix_len + len + suffix_len);
        str = buf + prefix_len;

        if (buf == NULL) {
            fprintf(stderr, "Couldn't allocate buffer (Out of memory ?), aborting...\n");
            exit(1);
        }

        // Copy over prefix into new buffer
        memcpy(buf, BASE_BUFFER, prefix_len * sizeof(char));

        va_start(args, fmt);
        vsnprintf(str, len + 1, fmt, args);
        va_end(args);
    }

    memcpy(buf + prefix_len + len, suffix, suffix_len * sizeof(char));

    fwrite(buf, 1, prefix_len + len + suffix_len, LOGGER.fd);

#ifdef LOG_FLUSH
    fflush(LOGGER.fd);
#endif

    if (buf != BASE_BUFFER) {
        free(buf);
    }
}
