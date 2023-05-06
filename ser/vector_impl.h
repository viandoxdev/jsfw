#ifndef VECTOR_IMPL_H
#define VECTOR_IMPL_H
#include "assert.h"
#include "macro_utils.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define _VECTOR_MAP_ADD(m, a, fst, ...) m(a, fst) __VA_OPT__(DEFER1(_VECTOR__MAP_ADD)()(m, a, __VA_ARGS__))
#define _VECTOR__MAP_ADD() _VECTOR_MAP_ADD

#define VECTOR_IMPL(T, V, qualifier, ...) \
    typedef struct { \
        T *data; \
        size_t len; \
        size_t cap; \
    } V; \
    __attribute__((unused)) static inline V _vec_##qualifier##_init() { return (V){.data = NULL, .len = 0, .cap = 0}; } \
    __attribute__((unused)) static inline void _vec_##qualifier##_drop(V vec) { \
        __VA_OPT__({ \
            for (size_t i = 0; i < vec.len; i++) { \
                __VA_ARGS__(vec.data[i]); \
            } \
        }) \
        if (vec.data != NULL) { \
            free(vec.data); \
        } \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_grow(V *vec, size_t cap) { \
        if (cap <= vec->cap) \
            return; \
        if (vec->data == NULL || vec->cap == 0) { \
            vec->data = malloc(cap * sizeof(T)); \
            assert_alloc(vec->data); \
            vec->cap = cap; \
            return; \
        } \
        if (cap < 2 * vec->cap) { \
            cap = 2 * vec->cap; \
        } \
        if (cap < 4) { \
            cap = 4; \
        } \
        T *newp = realloc(vec->data, cap * sizeof(T)); \
        assert_alloc(newp); \
        vec->data = newp; \
        vec->cap = cap; \
    } \
    __attribute__((unused)) static inline V _vec_##qualifier##_init_with_cap(size_t cap) { \
        V vec = {.data = NULL, .len = 0, .cap = 0}; \
        _vec_##qualifier##_grow(&vec, cap); \
        return vec; \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_shrink(V *vec) { \
        if (vec->len > 0) { \
            T *newp = realloc(vec->data, vec->len); \
            assert_alloc(newp); \
            vec->data = newp; \
            vec->cap = vec->len; \
        } else { \
            free(vec->data); \
            vec->data = NULL; \
            vec->cap = 0; \
        } \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_push(V *vec, T val) { \
        _vec_##qualifier##_grow(vec, vec->len + 1); \
        vec->data[vec->len++] = val; \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_push_array(V *vec, T const *vals, size_t count) { \
        _vec_##qualifier##_grow(vec, vec->len + count); \
        for (size_t i = 0; i < count; i++) { \
            vec->data[vec->len++] = vals[i]; \
        } \
    } \
    __attribute__((unused)) static inline V _vec_##qualifier##_clone(V *vec) { \
        if (vec->len == 0) { \
            return (V){.data = NULL, .len = 0, .cap = 0}; \
        } \
        V res = {.data = NULL, .len = 0, .cap = 0}; \
        _vec_##qualifier##_grow(&res, vec->len); \
        _vec_##qualifier##_push_array(&res, vec->data, vec->len); \
        return res; \
    } \
    __attribute__((unused)) static inline bool _vec_##qualifier##_pop_opt(V *vec, T *val) { \
        if (vec->len == 0) \
            return false; \
        vec->len--; \
        if (val != NULL) { \
            *val = vec->data[vec->len]; \
        } \
        __VA_OPT__(else { __VA_ARGS__(vec->data[vec->len]); }) \
        return true; \
    } \
    __attribute__((unused)) static inline T _vec_##qualifier##_pop(V *vec) { \
        debug_assert(vec->len > 0, "Popping zero length %s", #V); \
        return vec->data[--vec->len]; \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_clear(V *vec) { \
        __VA_OPT__({ \
            for (size_t i = 0; i < vec->len; i++) { \
                __VA_ARGS__(vec->data[i]); \
            } \
        }) \
        vec->len = 0; \
    } \
    __attribute__((unused)) static inline T _vec_##qualifier##_get(V *vec, size_t index) { \
        debug_assert(index < vec->len, "Out of bound index, on %s (index is %lu, but length is %lu)", #V, index, vec->len); \
        return vec->data[index]; \
    } \
    __attribute__((unused)) static inline bool _vec_##qualifier##_get_opt(V *vec, size_t index, T *val) { \
        if (index >= vec->len) { \
            return false; \
        } else if (val != NULL) { \
            *val = vec->data[index]; \
        } \
        return true; \
    } \
    __attribute__((unused)) static inline T _vec_##qualifier##_take(V *vec, size_t index) { \
        debug_assert(index < vec->len, "Out of bound index, on %s (index is %lu but length is %lu)", #V, index, vec->len); \
        T res = vec->data[index]; \
        if (index != vec->len - 1) \
            memmove(&vec->data[index], &vec->data[index + 1], (vec->len - index) * sizeof(T)); \
        vec->len--; \
        return res; \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_fill_range(V *vec, size_t from, size_t to, T item) { \
        debug_assert(from <= vec->len, "Can't start fill past the end of a vector"); \
        _vec_##qualifier##_grow(vec, to); \
        for (size_t i = from; i < to; i++) { \
            vec->data[i] = item; \
        } \
        vec->len = vec->len > to ? vec->len : to; \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_fill(V *vec, T item) { \
        _vec_##qualifier##_fill_range(vec, 0, vec->len, item); \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_insert(V *vec, size_t index, T val) { \
        debug_assert(index <= vec->len, "Can't insert past the end of vector"); \
        _vec_##qualifier##_grow(vec, vec->len + 1); \
        if (index < vec->len) { \
            memmove(&vec->data[index + 1], &vec->data[index], (vec->len - index) * sizeof(T)); \
        } \
        vec->data[index] = val; \
        vec->len++; \
    } \
    __attribute__((unused) \
    ) static inline void _vec_##qualifier##_insert_array(V *vec, size_t index, T const *vals, size_t count) { \
        debug_assert(index <= vec->len, "Can't insert past the end of vector"); \
        _vec_##qualifier##_grow(vec, vec->len + count); \
        if (index < vec->len) { \
            memmove(&vec->data[index + count], &vec->data[index], (vec->len - index) * sizeof(T)); \
        } \
        for (size_t i = 0; i < count; i++) { \
            vec->data[index + i] = vals[i]; \
        } \
        vec->len += count; \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_set_array(V *vec, size_t index, T const *vals, size_t count) { \
        debug_assert(index <= vec->len, "Can't start set past the end of vector"); \
        _vec_##qualifier##_grow(vec, index + count); \
        for (size_t i = 0; i < count; i++) { \
            vec->data[index + i] = vals[i]; \
        } \
        vec->len = vec->len > (index + count) ? vec->len : (index + count); \
    } \
    __attribute__((unused)) static inline void _vec_##qualifier##_splice(V *vec, size_t index, size_t count) { \
        debug_assert(index < vec->len, "Can't splice past end of vector"); \
        if (count == 0) \
            return; \
        if (index + count < vec->len) { \
            memmove(&vec->data[index], &vec->data[index + count], (vec->len - index - count) * sizeof(T)); \
        } \
        vec->len -= count; \
    } \
    _Static_assert(1, "Semicolon required")

typedef struct {
    void *data;
    size_t len;
    size_t cap;
} AnyVec;

#endif

#ifdef VECTOR_IMPL_LIST

#define _VECTOR_GEN(a, x) DEFER1(_VECTOR__GEN)(a, _VECTOR__GEN_CLOSE x
#define _VECTOR__GEN_CLOSE(a, b, c, ...) a, b, c __VA_OPT__(,) __VA_ARGS__)
#define _VECTOR__GEN(x, _, V, qualifier, ...) \
    V: \
    _vec_##qualifier##x,

#define _VECTOR_GENERIC(expr, x) _Generic(expr, EVAL(CALL(_VECTOR_MAP_ADD, _VECTOR_GEN, _##x, VECTOR_IMPL_LIST)) AnyVec: (void)0)

#define vec_init() \
    { .data = NULL, .len = 0, .cap = 0 }
#define vec_drop(vec) _VECTOR_GENERIC(vec, drop)(vec)
#define vec_grow(vec, len) _VECTOR_GENERIC(*(vec), grow)(vec, len)
#define vec_shrink(vec) _VECTOR_GENERIC(*(vec), shrink)(vec)
#define vec_push(vec, val) _VECTOR_GENERIC(*(vec), push)(vec, val)
#define vec_push_array(vec, vals, count) _VECTOR_GENERIC(*(vec), push_array)(vec, vals, count)
#define vec_clone(vec) _VECTOR_GENERIC(*(vec), clone)(vec)
#define vec_pop_opt(vec, val) _VECTOR_GENERIC(*(vec), pop_opt)(vec, val)
#define vec_pop(vec) _VECTOR_GENERIC(*(vec), pop)(vec)
#define vec_clear(vec) _VECTOR_GENERIC(*(vec), clear)(vec)
#define vec_get(vec, index) _VECTOR_GENERIC(*(vec), get)(vec, index)
#define vec_get_opt(vec, index, val) _VECTOR_GENERIC(*(vec), get_opt)(vec, index, val)
#define vec_take(vec, index) _VECTOR_GENERIC(*(vec), take)(vec, index)
#define vec_fill_range(vec, from, to, item) _VECTOR_GENERIC(*(vec), fill_range)(vec, from, to, item)
#define vec_fill(vec, item) _VECTOR_GENERIC(*(vec), fill)(vec, item)
#define vec_insert(vec, index, val) _VECTOR_GENERIC(*(vec), insert)(vec, index, val)
#define vec_insert_array(vec, index, vals, count) _VECTOR_GENERIC(*(vec), insert_array)(vec, index, vals, count)
#define vec_set_array(vec, index, vals, count) _VECTOR_GENERIC(*(vec), set_array)(vec, index, vals, count)
#define vec_splice(vec, index, count) _VECTOR_GENERIC(*(vec), splice)(vec, index, count)
#define vec_foreach(vec, var, expr) \
    do { \
        for (size_t _i = 0; _i < (vec)->len; _i++) { \
            typeof(*(vec)->data) var = (vec)->data[_i]; \
            expr; \
        } \
    } while (false)

#endif
