#include "gen_vec.h"

#include "assert.h"

#include <string.h>

#define NONE (~0)

typedef struct {
    uint64_t gen;
    size_t next_free;
} Entry;

GenVec genvec_init(size_t data_size, DropFunction drop) {
    GenVec res;
    res.len = 0;
    res.count = 0;
    res.size = data_size;
    res.entry_size = data_size + sizeof(uint64_t);
    res.drop = drop;
    res.cap = 0;
    res.data = NULL;
    res.last_free = NONE;
    res.gen = 1;
    return res;
}

static void genvec_grow(GenVec *v, size_t cap) {
    if (v->cap >= cap)
        return;
    cap = v->cap * 2 > cap ? v->cap * 2 : cap;
    if (v->cap != 0) {
        v->data = realloc(v->data, cap * v->entry_size);
    } else {
        v->data = malloc(cap * v->entry_size);
    }
    assert_alloc(v->data);
    v->cap = cap;
}

GenIndex genvec_push(GenVec *v, void *item) {
    if (v->last_free == NONE) {
        genvec_grow(v, v->len + 1);
        byte *ptr = v->data + v->len++ * v->entry_size;
        ((Entry *)ptr)->gen = v->gen;
        memcpy(ptr + sizeof(Entry), item, v->size);
        v->count++;
        return (GenIndex){.gen = v->gen, .index = v->len - 1};
    } else {
        size_t index = v->last_free;
        byte *ptr = v->data + index * v->entry_size;
        Entry *entry = (Entry *)ptr;
        v->last_free = entry->next_free;
        entry->gen = v->gen;
        memcpy(ptr + sizeof(Entry), item, v->size);
        v->count++;
        return (GenIndex){.gen = v->gen, .index = index};
    }
}

void genvec_remove(GenVec *v, GenIndex idx) {
    byte *ptr = v->data + idx.index * v->entry_size;
    Entry *entry = (Entry *)ptr;
    if (!entry->gen || entry->gen != idx.gen)
        return;
    entry->gen = 0;
    entry->next_free = v->last_free;
    v->last_free = idx.index;
    if (v->drop != NULL) {
        v->drop(ptr + sizeof(Entry));
    }
    v->count--;
    v->gen++;
}

void *genvec_get(GenVec *v, GenIndex idx) {
    byte *ptr = v->data + idx.index * v->entry_size;
    Entry *entry = (Entry *)ptr;
    if (!entry->gen || entry->gen != idx.gen)
        return NULL;
    return ptr + sizeof(Entry);
}

void genvec_drop(GenVec v) {
    if (v.cap >= 0) {
        free(v.data);
    }
}
