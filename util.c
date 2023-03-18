#include "util.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

double timespec_to_double(struct timespec *ts) {
    double secs = ts->tv_sec;
    secs += ts->tv_nsec / 1000000000;
    return secs;
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

// Defaults for json parsing
void default_to_null(void *ptr) { *(void **)ptr = NULL; }
void default_to_false(void *ptr) { *(bool *)ptr = false; }
void default_to_zero_u8(void *ptr) { *(uint8_t *)ptr = 0; }
void default_to_zero_u32(void *ptr) { *(uint32_t *)ptr = 0; }
void default_to_zero_u64(void *ptr) { *(uint64_t *)ptr = 0; }
void default_to_zero_size(void *ptr) { *(size_t *)ptr = 0; }
void default_to_zero_double(void *ptr) { *(double *)ptr = 0.0; }
void default_to_one_size(void *ptr) { *(size_t *)ptr = 1; }
void default_to_negative_one_i32(void *ptr) { *(int32_t *)ptr = -1; }

// Transformers for json parsing
void tsf_numsec_to_timespec(void *arg, void *ptr) {
    double seconds = *(double *)arg;

    struct timespec ts;
    ts.tv_sec  = floor(seconds);
    ts.tv_nsec = (seconds - floor(seconds)) * 1000000000;

    *(struct timespec *)ptr = ts;
}

void tsf_numsec_to_intms(void *arg, void *ptr) {
    double seconds   = *(double *)arg;
    *(uint32_t *)ptr = seconds * 1000;
}

void tsf_uniq_to_u64(void *arg, void *ptr) {
    char *s = *(char **)arg;
    if (strnlen(s, 18) != 17) {
        printf("JSON: wrong length for uniq, expected 'xx:xx:xx:xx:xx:xx'\n");
        free(s);
        return;
    }
    uint64_t mac = 0;
    for (int i = 0; i < 17; i++) {
        char    c     = s[i];
        uint8_t digit = 0;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else if (c == ':')
            continue;
        else {
            printf("JSON: unexpected character '%c' in uniq at position %i (%s)\n", c, i, s);
            free(s);
            return;
        }

        mac <<= 4;
        mac |= digit;
    }
    free(s);
    *(uint64_t *)ptr = mac;
}

void tsf_hex_to_i32(void *arg, void *ptr) {
    char   *s = *(char **)arg;
    char   *f = s;
    char    c;
    int32_t res = 0;
    while ((c = *s++) != '\0') {
        uint8_t digit = 0;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else {
            printf("JSON: unexpected character '%c' in hex string\n", c);
            free(f);
            return;
        }
        res <<= 4;
        res |= digit;
    }
    free(f);
    *(int32_t *)ptr = res;
}

void tsf_double_to_size(void *arg, void *ptr) {
    double d       = *(double *)arg;
    *(size_t *)ptr = d;
}

static uint8_t hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else
        return 16;
}

void tsf_hex_to_color(void *arg, void *ptr) {
    char *s   = *(char **)arg;
    int   len = strnlen(s, 8);
    if (len != 7 || s[0] != '#') {
        printf("JSON: bad hex color format expected '#RRGGBB' or '#rrggbb', got '%s'\n", s);
        free(s);
        return;
    }

    uint8_t *color = ptr;

    for (int i = 0; i < 3; i++) {
        uint8_t digits[2] = {hex_digit(s[1 + 2 * i]), hex_digit(s[2 + 2 * i])};

        if (digits[0] == 16 || digits[1] == 16) {
            printf("JSON: illegal character in hex color: '%.7s'\n", s);
            free(s);
            return;
        }

        color[i] = (digits[0] << 4) | digits[1];
    }

    free(s);
}

void tsf_num_to_u8_clamp(void *arg, void *ptr) {
    double n        = *(double *)arg;
    *(uint8_t *)ptr = n > 255.0 ? 255.0 : n < 0.0 ? 0.0 : n;
}

void tsf_num_to_int(void *arg, void *ptr) {
    double n    = *(double *)arg;
    *(int *)ptr = n;
}
