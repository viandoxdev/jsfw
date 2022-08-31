// vi:ft=c
#ifndef UTIL_H_
#define UTIL_H_
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Print a formatted message and exit with code 1
void     panicf(const char *fmt, ...);
uint16_t parse_port(const char *str);

// Test if the bit with index i is set in the byte array bits
static inline bool bit_set(uint8_t *bits, int i) { return bits[i / 8] & (1 << (i % 8)); }
// Align n to the next 8 boundary
static inline size_t align_8(size_t n) { return (((n - 1) >> 3) + 1) << 3; }
uint8_t              parse_hex_digit(char h);

#endif
