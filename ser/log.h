#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Bit field of severities
typedef uint32_t LogSeverities;

// The logger
typedef struct {
    bool initialized;
    FILE *fd;
    LogSeverities sevs;
    int source_width;
    int func_width;
} Logger;

// A message's severity Error > Warning > Info > Debug > Trace
typedef enum : LogSeverities {
    Trace = 1 << 0,
    Debug = 1 << 1,
    Info = 1 << 2,
    Warning = 1 << 3,
    Error = 1 << 4,
} LogSeverity;

// Needs to be here but log_* macros should be used instead
void _log_severity(LogSeverity sev, const char *func, const char *file, const int line, char *fmt, ...);

// Set the file desciptor for the logger
void logger_set_fd(FILE *fd);
void logger_enable_severities(LogSeverities sevs);
void logger_disable_severities(LogSeverities sevs);
void logger_set_severities(LogSeverities sevs);
void logger_init();

#ifdef LOG_DISABLE
#define log_trace(...) (void)0
#define log_debug(...) (void)0
#define log_info(...) (void)0
#define log_warn(...) (void)0
#define log_error(...) (void)0
#else
#define log_trace(...) _log_severity(Trace, __func__, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) _log_severity(Debug, __func__, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...) _log_severity(Info, __func__, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) _log_severity(Warning, __func__, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) _log_severity(Error, __func__, __FILE__, __LINE__, __VA_ARGS__)
#endif

#endif
