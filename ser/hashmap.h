#ifndef HASHMAP_H
#define HASHMAP_H

typedef unsigned char byte;

#include "gen_vec.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint64_t key;
} Hasher;

// Create new hasher with a pseudo random state
Hasher hasher_init();
// Hash given data with hasher
uint32_t hash(Hasher state, const byte *data, const size_t len);

typedef uint32_t (*HashFunction)(Hasher state, const void *item);
typedef bool (*EqualFunction)(const void *a, const void *b);
typedef void (*DropFunction)(void *item);

typedef struct {
    size_t size;
    size_t aligned_size;
    size_t entry_size;
    size_t cap;
    size_t mask;
    size_t count;
    size_t max;
    Hasher state;
    byte *buckets;
    byte *buckets_end;
    byte *alloc;
    HashFunction hash;
    EqualFunction equal;
    DropFunction drop;
} Hashmap;

typedef struct {
    Hashmap *map;
    GenVec items;
} StableHashmap;

// Initialize a new hashmap
Hashmap *hashmap_init(HashFunction hash, EqualFunction equal, DropFunction drop, size_t data_size);
// Insert value in hashmapn returns true if the value was overwritten
bool hashmap_set(Hashmap *map, const void *item);
// Get value of hashmap, return NULL if not found
void *hashmap_get(Hashmap *map, const void *key);
// Take a value from a hashmap and put it into dst
bool hashmap_take(Hashmap *map, const void *key, void *dst);
// Destroy hashmap
void hashmap_drop(Hashmap *map);
// Delete entry from hasmap
static inline __attribute__((always_inline)) bool hashmap_delete(Hashmap *map, const void *key) {
    return hashmap_take(map, key, NULL);
}
// Check if hashmap contains key
bool hashmap_has(Hashmap *map, const void *key);
// Clear hashmap of all entries
void hashmap_clear(Hashmap *map);
// Iterate hasmap
bool hashmap_iter(Hashmap *map, void *iter);

#define impl_hashmap(prefix, type, hash, equal) \
    uint32_t prefix##_hash(Hasher state, const void *_v) { \
        type *v = (type *)_v; \
        hash \
    } \
    bool prefix##_equal(const void *_a, const void *_b) { \
        type *a = (type *)_a; \
        type *b = (type *)_b; \
        equal \
    } \
    _Static_assert(1, "Semicolon required")
#define impl_hashmap_delegate(prefix, type, delegate, accessor) \
    impl_hashmap( \
        prefix, \
        type, \
        { return delegate##_hash(state, &v->accessor); }, \
        { return delegate##_equal(&a->accessor, &b->accessor); } \
    )

#endif
