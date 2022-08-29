// vi: set ft=c
#ifndef VEC_H
#define VEC_H
#include<unistd.h>
#include<stdint.h>

#define vec_of(type) vec_new(sizeof(type))

typedef struct {
    void * data;
    size_t cap;
    size_t len;
    size_t stride;
} Vec;

// Create a new vector
Vec vec_new(size_t data_size);
// Create a new vector with an initial capacity
Vec vec_cap(size_t data_size, size_t initial_capacity);
// Push an element into the vector
void vec_push(Vec * v, void * data);
// Pop an element into the vector, and put it in data if it is not null
void vec_pop(Vec * v, void * data);
// Get a pointer to the element at an index, returns NULL if there is no such element
void * vec_get(Vec * v, size_t index);
// Insert an element at any index in the vector (except past the end)
void vec_insert(Vec * v, void * data, size_t index);
// Remove an element from the vector, and put it in data if it is not NULL
void vec_remove(Vec * v, size_t index, void * data);
// Clear the vector
void vec_clear(Vec * v);
// Extend the vector with the content of data
void vec_extend(Vec * v, void * data, size_t len);
// Free the vector
void vec_free(Vec v);

#endif
