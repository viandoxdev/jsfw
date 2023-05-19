#ifndef UTILS_H
#define UTILS_H

#ifdef NDEBUG
#define IF_DEBUG(...)
#else
#define IF_DEBUG(...) __VA_ARGS__
#endif

#include <stdbool.h>
#include <stdint.h>

typedef unsigned char byte;

#include "hashmap.h"
#include "vector_impl.h"

#define STRING_SLICE(str) ((StringSlice){.ptr = str, .len = sizeof(str) - 1})

typedef struct {
    const char *ptr;
    uint32_t len;
} StringSlice;

bool string_slice_equal(const void *a, const void *b);
uint32_t string_slice_hash(Hasher state, const void *item);

bool pointer_equal(const void *a, const void *b);
uint32_t pointer_hash(Hasher state, const void *item);

VECTOR_IMPL(void *, PointerVec, pointer);
VECTOR_IMPL(const char *, ConstStringVec, const_string);
VECTOR_IMPL(char, CharVec, char);
VECTOR_IMPL(uint64_t, UInt64Vec, uint64);
VECTOR_IMPL(CharVec, CharVec2, char_vec, _vec_char_drop);
VECTOR_IMPL(StringSlice, StringSliceVec, string_slice);

// Styled strings are very mutable strings
typedef struct {
    CharVec chars;
    ConstStringVec styles;
} StyledString;

StyledString styled_string_init();
void styled_string_clear(StyledString *str);
void styled_string_set(StyledString *str, size_t index, const char *style, const char *s, size_t len);
void styled_string_set_style(StyledString *str, size_t index, const char *style, size_t len);
void styled_string_fill(StyledString *str, size_t index, char fill, size_t len);
size_t styled_string_available_space(StyledString *str, size_t from, size_t stop_at);
void styled_string_push(StyledString *str, const char *style, const char *s);
char *styled_string_build(StyledString *str);
void styled_string_drop(StyledString str);
// Printf to an allocated string
char *msprintf(const char *fmt, ...);
void charvec_push_str(CharVec *v, const char *str);
void charvec_format(CharVec *v, const char *fmt, ...);

VECTOR_IMPL(StyledString, StyledStringVec, styled_string, styled_string_drop);

#endif
