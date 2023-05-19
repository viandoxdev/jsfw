#ifndef GEN_VEC_H
#define GEN_VEC_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef unsigned char byte;
typedef void (*DropFunction)(void *item);

typedef struct {
    size_t size;
    size_t entry_size;
    size_t cap;
    size_t len;
    size_t count;
    uint64_t gen;
    byte *data;
    size_t last_free;
    DropFunction drop;
} GenVec;

typedef struct {
    uint64_t gen;
    size_t index;
} GenIndex;

GenVec genvec_init(size_t data_size, DropFunction drop);
GenIndex genvec_push(GenVec *v, void *item);
void genvec_remove(GenVec *v, GenIndex idx);
void *genvec_get(GenVec *v, GenIndex idx);
void genvec_drop(GenVec v);

#endif
