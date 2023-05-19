#include "utils.h"

#include "vector.h"

#include <stdarg.h>
#include <string.h>

bool string_slice_equal(const void *_a, const void *_b) {
    const StringSlice *a = (StringSlice *)_a;
    const StringSlice *b = (StringSlice *)_b;
    if (a->len != b->len) {
        return false;
    }
    uint32_t len = a->len < b->len ? a->len : b->len;
    return strncmp(a->ptr, b->ptr, len) == 0;
}

uint32_t string_slice_hash(Hasher state, const void *_item) {
    const StringSlice *item = (StringSlice *)_item;
    return hash(state, (byte *)item->ptr, item->len);
}

bool pointer_equal(const void *_a, const void *_b) {
    const void *a = *(void **)_a;
    const void *b = *(void **)_b;
    return a == b;
}
uint32_t pointer_hash(Hasher state, const void *item) { return hash(state, (byte *)item, sizeof(void *)); }

StyledString styled_string_init() {
    return (StyledString){
        .chars = vec_init(),
        .styles = vec_init(),
    };
}

char *msprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char *res = malloc(len + 1);
    assert_alloc(res);
    va_start(args, fmt);
    vsnprintf(res, len + 1, fmt, args);
    va_end(args);
    return res;
}

void styled_string_set(StyledString *str, size_t index, const char *style, const char *s, size_t len) {
    if (index > str->chars.len) {
        vec_fill_range(&str->chars, str->chars.len, index, ' ');
        vec_fill_range(&str->styles, str->styles.len, index, NULL);
    }
    vec_set_array(&str->chars, index, s, len);
    vec_fill_range(&str->styles, index, index + len, NULL);
    str->styles.data[index] = style;
    // Reset the style if there are characters after
    if (style != NULL && str->chars.len > index + len) {
        str->styles.data[index + len] = "\033[0m";
    }
}

void styled_string_set_style(StyledString *str, size_t index, const char *style, size_t len) {
    if (index > str->chars.len) {
        vec_fill_range(&str->chars, str->chars.len, index, ' ');
        vec_fill_range(&str->styles, str->styles.len, index, NULL);
    }
    if (index + len > str->chars.len) {
        vec_fill_range(&str->chars, str->chars.len, index + len, ' ');
    }
    vec_fill_range(&str->styles, index, index + len, NULL);
    str->styles.data[index] = style;
    // Reset the style if there are characters after
    if (style != NULL && str->chars.len > index + len && str->styles.data[index + len] == NULL) {
        str->styles.data[index + len] = "\033[0m";
    }
}

void styled_string_clear(StyledString *str) {
    vec_clear(&str->chars);
    vec_clear(&str->styles);
}

void styled_string_fill(StyledString *str, size_t index, char fill, size_t len) {
    vec_fill_range(&str->chars, index, index + len, fill);
}

void styled_string_push(StyledString *str, const char *style, const char *s) {
    size_t len = strlen(s);
    vec_push_array(&str->chars, s, len);
    size_t index = str->styles.len;
    vec_fill_range(&str->styles, index, str->chars.len, NULL);
    str->styles.data[index] = style;
}

char *styled_string_build(StyledString *str) {
    CharVec res = vec_init();
    vec_grow(&res, str->chars.len + 1);
    for (size_t i = 0; i < str->chars.len; i++) {
        const char *style = str->styles.data[i];
        if (style != NULL) {
            int len = strlen(style);
            vec_push_array(&res, style, len);
        }
        vec_push(&res, str->chars.data[i]);
    }
    vec_push_array(&res, "\033[0m\0", 5);
    return res.data;
}

size_t styled_string_available_space(StyledString *str, size_t from, size_t stop_at) {
    // We always have more space past the end of the string
    if (from >= str->chars.len)
        return stop_at;
    size_t space = 0;
    char *c = &str->chars.data[from];
    char *end = &str->chars.data[str->chars.len];
    while (space < stop_at && c < end && *c == ' ') {
        space++;
        c++; // Blasphemy
    }
    if (c == end || space == stop_at) {
        // We either found enough space, or we got the end of the string (infinite space)
        return stop_at;
    }
    return space;
}

void styled_string_drop(StyledString str) {
    vec_drop(str.styles);
    vec_drop(str.chars);
}

void charvec_push_str(CharVec *v, const char *str) { vec_push_array(v, str, strlen(str)); }

void charvec_format(CharVec *v, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    vec_grow(v, v->len + len + 1);
    va_start(args, fmt);
    vsnprintf(&v->data[v->len], len + 1, fmt, args);
    va_end(args);
    v->len += len;
}
