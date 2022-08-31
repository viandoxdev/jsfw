#include "util.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

uint16_t parse_port(const char *str) {
    long long n = atoll(str);
    if (n <= 0 || n > UINT16_MAX)
        panicf("Invalid port: Expected a number in the range 1..%d, got '%s'\n", UINT16_MAX, str);
    return n;
}

uint8_t parse_hex_digit(char h) {
    if (h >= '0' && h <= '9')
        return h - '0';
    else if (h >= 'a' && h <= 'f')
        return h - 'a' + 10;
    else if (h >= 'A' && h <= 'F')
        return h - 'A' + 10;
    else
        return 0;
}
