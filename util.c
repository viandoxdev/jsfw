#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

#ifndef __has_builtin
#define __has_builtin(_) 0
#endif

void panicf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    exit(1);
}
