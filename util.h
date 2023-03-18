// vi:ft=c
#ifndef UTIL_H_
#define UTIL_H_
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

// Print a formatted message and exit with code 1
void     panicf(const char *fmt, ...);
uint16_t parse_port(const char *str);

// Test if the bit with index i is set in the byte array bits
static inline bool bit_set(uint8_t *bits, int i) { return bits[i / 8] & (1 << (i % 8)); }
// Align n to the next 8 boundary
static inline size_t align_8(size_t n) { return (((n - 1) >> 3) + 1) << 3; }
// Align n to the next 4 boundary
static inline size_t align_4(size_t n) { return (((n - 1) >> 2) + 1) << 2; }
// Align n to the next 2 boundary
static inline size_t align_2(size_t n) { return (((n - 1) >> 1) + 1) << 1; }
uint8_t              parse_hex_digit(char h);
// Convert timespec to double in seconds mostly for printing.
double timespec_to_double(struct timespec *ts);

void default_to_null(void *ptr);
void default_to_false(void *ptr);
void default_to_zero_u8(void *ptr);
void default_to_zero_u32(void *ptr);
void default_to_zero_u64(void *ptr);
void default_to_zero_size(void *ptr);
void default_to_zero_double(void *ptr);
void default_to_one_size(void *ptr);
void default_to_negative_one_i32(void *ptr);

void tsf_numsec_to_timespec(void *arg, void *ptr);
void tsf_numsec_to_intms(void *arg, void *ptr);
void tsf_uniq_to_u64(void *arg, void *ptr);
void tsf_hex_to_i32(void *arg, void *ptr);
void tsf_double_to_size(void *arg, void *ptr);
void tsf_hex_to_color(void *arg, void *ptr);
void tsf_num_to_u8_clamp(void *arg, void *ptr);
void tsf_num_to_int(void *arg, void *ptr);

#endif
