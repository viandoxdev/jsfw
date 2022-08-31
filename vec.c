#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INIT_CAP 8

static void handle_alloc_error() {
    printf("Error when allocating memory.\n");
    exit(2);
}

Vec vec_new(size_t data_size) { return vec_cap(data_size, INIT_CAP); }

Vec vec_cap(size_t data_size, size_t initial_capacity) {
    Vec v;
    v.cap    = initial_capacity;
    v.len    = 0;
    v.stride = data_size;
    v.data   = malloc(data_size * initial_capacity);
    return v;
}

static inline void vec_grow(Vec *v, size_t cap) {
    if (v->cap >= cap) {
        return;
    }

    size_t new_cap  = cap > v->cap * 2 ? cap : v->cap * 2;
    void  *new_data = realloc(v->data, new_cap * v->stride);
    if (new_data == NULL) {
        handle_alloc_error();
    }

    v->data = new_data;
    v->cap  = new_cap;
}

void vec_push(Vec *v, void *data) {
    vec_grow(v, v->len + 1);
    memcpy(v->data + v->stride * v->len++, data, v->stride);
}

void vec_pop(Vec *v, void *data) {
    if (v->len == 0) {
        printf("ERR(vec_pop): Trying to pop an element from an empty vector\n");
        return;
    }
    if (data != NULL) {
        memcpy(data, v->data + v->stride * (v->len - 1), v->stride);
    }
    v->len--;
}

void *vec_get(Vec *v, size_t index) {
    if (index >= v->len) {
        return NULL;
    }
    return v->data + index * v->stride;
}

void vec_insert(Vec *v, void *data, size_t index) {
    if (index > v->len) {
        printf("ERR(vec_insert): Trying to insert past the end of the vector.\n");
        return;
    }
    vec_grow(v, v->len + 1);

    void *slot = v->data + index * v->stride;
    if (index < v->len) {
        memmove(slot + v->stride, slot, (v->len - index) * v->stride);
    }
    memcpy(slot, data, v->stride);
    v->len++;
}

void vec_remove(Vec *v, size_t index, void *data) {
    if (v->len == 0) {
        printf("ERR(vec_remove): Trying to remove an element from an empty vector\n");
        return;
    }
    if (index >= v->len) {
        printf("ERR(vec_remove): Trying to remove past the end of the vector\n");
        return;
    }

    void *slot = v->data + index * v->stride;
    if (data != NULL) {
        memcpy(data, slot, v->stride);
    }
    if (index < --v->len) {
        memmove(slot, slot + v->stride, (v->len - index) * v->stride);
    }
}

void vec_clear(Vec *v) { v->len = 0; }

void vec_extend(Vec *v, void *data, size_t len) {
    if (len == 0) {
        return;
    }
    vec_grow(v, v->len + len);
    memcpy(v->data + v->stride * v->len, data, v->stride * len);
    v->len += len;
}

void vec_free(Vec v) { free(v.data); }
