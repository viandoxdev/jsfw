#include "hashmap.h"

#include "assert.h"
#include "utils.h"

#include <endian.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if __BYTE_ORDER__ == __LITTLE_ENDIAN
#define U32TO8_LE(p, v) (*(uint32_t *)(p) = v)
#define U8TO32_LE(p) (*(uint32_t *)(p))
#else
#define U32TO8_LE(p, v) \
    do { \
        (p)[0] = (uint8_t)((v)); \
        (p)[1] = (uint8_t)((v) >> 8); \
        (p)[2] = (uint8_t)((v) >> 16); \
        (p)[3] = (uint8_t)((v) >> 24); \
    } while (0)

#define U8TO32_LE(p) (((uint32_t)((p)[0])) | ((uint32_t)((p)[1]) << 8) | ((uint32_t)((p)[2]) << 16) | ((uint32_t)((p)[3]) << 24))
#endif

#define ROTL(x, b) (uint32_t)(((x) << (b)) | ((x) >> (32 - (b))))
#define SIPROUND \
    do { \
        v0 += v1; \
        v1 = ROTL(v1, 5); \
        v1 ^= v0; \
        v0 = ROTL(v0, 16); \
        v2 += v3; \
        v3 = ROTL(v3, 8); \
        v3 ^= v2; \
        v0 += v3; \
        v3 = ROTL(v3, 7); \
        v3 ^= v0; \
        v2 += v1; \
        v1 = ROTL(v1, 13); \
        v1 ^= v2; \
        v2 = ROTL(v2, 16); \
    } while (0)

// Kinda useless check
_Static_assert(sizeof(uint32_t) == 4, "uint32_t isn't 4 bytes");

uint32_t hash(Hasher state, const byte *data, const size_t len) {
    uint32_t v0 = 0, v1 = 0, v2 = UINT32_C(0x6c796765), v3 = UINT32_C(0x74656462);
    uint32_t k0 = U8TO32_LE((byte *)&state.key), k1 = U8TO32_LE(((byte *)&state.key) + 4);
    uint32_t m;
    // Pointer to the end of the last 4 byte block
    const byte *end = data + len - (len % sizeof(uint32_t));
    const int left = len % sizeof(uint32_t);
    uint32_t b = ((uint32_t)len) << 24;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    for (; data != end; data += 4) {
        m = U8TO32_LE(data);
        v3 ^= m;
        for (int i = 0; i < 2; i++) {
            SIPROUND;
        }
        v0 ^= m;
    }

    switch (left) {
    case 3:
        b |= ((uint32_t)data[2]) << 16;
    case 2:
        b |= ((uint32_t)data[1]) << 8;
    case 1:
        b |= ((uint32_t)data[0]);
    }

    v3 ^= b;
    v2 ^= 0xff;

    for (int i = 0; i < 4; i++) {
        SIPROUND;
    }

    return v1 ^ v3;
}

Hasher hasher_init() {
    static Hasher HASHER = {.key = UINT64_C(0x5E3514A61CC01657)};
    static uint64_t COUNT = 0;
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    ts.tv_nsec += COUNT++;
    ts.tv_sec ^= ts.tv_nsec;
    uint64_t k;
    ((uint32_t *)&k)[0] = hash(HASHER, (byte *)&ts.tv_sec, sizeof(ts.tv_sec));
    ((uint32_t *)&k)[1] = hash(HASHER, (byte *)&ts.tv_nsec, sizeof(ts.tv_nsec));
    // return (Hasher){.key = k};
    //  TODO: change that back
    return (Hasher){.key = 113223440};
}

// Must be a power of 2
#define HASHMAP_BASE_CAP 64
#define MAX_ITEMS(cap) (cap / (2))

typedef struct {
    uint32_t hash;
    bool occupied;
} __attribute__((aligned(8))) Bucket;

Hashmap *hashmap_init(HashFunction hash, EqualFunction equal, DropFunction drop, size_t data_size) {
    size_t aligned_size = (((data_size - 1) >> 3) + 1) << 3;
    size_t entry_size = sizeof(Bucket) + aligned_size;
    byte *alloc = malloc(sizeof(Hashmap));
    byte *buckets = malloc(HASHMAP_BASE_CAP * entry_size);
    assert_alloc(alloc);
    assert_alloc(buckets);
    Hashmap *map = (Hashmap *)alloc;
    map->size = data_size;
    map->aligned_size = aligned_size;
    map->entry_size = sizeof(Bucket) + aligned_size;
    map->cap = HASHMAP_BASE_CAP;
    map->mask = HASHMAP_BASE_CAP - 1;
    map->count = 0;
    map->max = MAX_ITEMS(HASHMAP_BASE_CAP);
    map->state = hasher_init();
    map->hash = hash;
    map->equal = equal;
    map->drop = drop;
    map->alloc = alloc;
    map->buckets = buckets;
    map->buckets_end = map->buckets + HASHMAP_BASE_CAP * map->entry_size;

    for (size_t i = 0; i < HASHMAP_BASE_CAP; i++) {
        ((Bucket *)buckets)->occupied = false;
        buckets += map->entry_size;
    }

    return map;
}

// Return the first empty bucket or the first matching bucket
static inline __attribute__((always_inline)) byte *hashmap_bucket(Hashmap *map, const void *item, uint32_t hash, size_t *rindex) {
    int32_t index = hash & map->mask;
    byte *ptr = map->buckets + index * map->entry_size;
    while (((Bucket *)ptr)->occupied && (((Bucket *)ptr)->hash != hash || !map->equal(item, ptr + sizeof(Bucket)))) {
        ptr += map->entry_size;
        index++;
        if (ptr >= map->buckets_end) {
            ptr = map->buckets;
            index = 0;
        }
    }
    if (rindex != NULL) {
        *rindex = index;
    }
    return ptr;
}

static bool hashmap_insert(Hashmap *map, const void *item, uint32_t hash) {
    byte *ptr = hashmap_bucket(map, item, hash, NULL);
    Bucket *bucket = (Bucket *)ptr;
    void *dst = ptr + sizeof(Bucket);
    bool replace = bucket->occupied;
    if (map->drop != NULL && replace) {
        map->drop(dst);
    }

    bucket->hash = hash;
    bucket->occupied = true;
    memcpy(dst, item, map->size);
    if (!replace) {
        map->count++;
    }
    return replace;
}

// Grow hashmap to double the size
static void hashmap_grow(Hashmap *map) {
    byte *old_buckets = map->buckets;
    size_t old_cap = map->cap;

    map->cap *= 2;
    map->mask = map->cap - 1;
    map->count = 0;
    map->max = MAX_ITEMS(map->cap);
    map->buckets = malloc(map->cap * map->entry_size);
    assert_alloc(map->buckets);
    map->buckets_end = map->buckets + map->cap * map->entry_size;

    for (byte *ptr = map->buckets; ptr < map->buckets_end; ptr += map->entry_size) {
        ((Bucket *)ptr)->occupied = false;
    }

    byte *ptr = old_buckets;
    for (size_t i = 0; i < old_cap; i++) {
        Bucket *bucket = (Bucket *)ptr;
        void *item = ptr + sizeof(Bucket);
        if (bucket->occupied) {
            hashmap_insert(map, item, bucket->hash);
        }
        ptr += map->entry_size;
    }

    free(old_buckets);
}

bool hashmap_set(Hashmap *map, const void *item) {
    if (map->count >= map->max) {
        hashmap_grow(map);
    }

    uint32_t hash = map->hash(map->state, item);
    return hashmap_insert(map, item, hash);
}

void *hashmap_get(Hashmap *map, const void *key) {
    uint32_t hash = map->hash(map->state, key);
    byte *ptr = hashmap_bucket(map, key, hash, NULL);
    Bucket *bucket = (Bucket *)ptr;
    void *res = ptr + sizeof(Bucket);
    if (!bucket->occupied) {
        return NULL;
    } else {
        return res;
    }
}

bool hashmap_has(Hashmap *map, const void *key) {
    uint32_t hash = map->hash(map->state, key);
    byte *ptr = hashmap_bucket(map, key, hash, NULL);
    Bucket *bucket = (Bucket *)ptr;

    return bucket->occupied;
}

bool hashmap_take(Hashmap *map, const void *key, void *dst) {
    uint32_t hash = map->hash(map->state, key);
    byte *ptr = hashmap_bucket(map, key, hash, NULL);
    Bucket *bucket = (Bucket *)ptr;
    void *item = ptr + sizeof(Bucket);

    if (!bucket->occupied) {
        return false;
    }

    map->count--;
    if (dst == NULL && map->drop != NULL) {
        map->drop(item);
    } else if (dst != NULL) {
        memcpy(dst, item, map->size);
    }

    byte *nptr = ptr;
    while (true) {
        // Kinda jank ? better solution ?
        size_t index = (uintptr_t)(ptr - map->buckets) / map->entry_size;

        nptr += map->entry_size;
        if (nptr >= map->buckets_end) {
            nptr = map->buckets;
        }

        while (((Bucket *)nptr)->occupied && (((Bucket *)nptr)->hash & map->mask) > index) {
            nptr += map->entry_size;
            if (nptr >= map->buckets_end) {
                nptr = map->buckets;
            }
        }

        if (!((Bucket *)nptr)->occupied) {
            bucket->occupied = false;
            return true;
        }

        *bucket = *(Bucket *)nptr;
        memcpy(item, nptr + sizeof(Bucket), map->size);

        ptr = nptr;
        bucket = (Bucket *)ptr;
        item = ptr + sizeof(Bucket);
    }
}

void hashmap_clear(Hashmap *map) {
    if (map->count == 0)
        return;

    for (byte *ptr = map->buckets; ptr < map->buckets_end; ptr += map->entry_size) {
        if (map->drop != NULL) {
            map->drop(ptr + sizeof(Bucket));
        }
        ((Bucket *)ptr)->occupied = false;
    }
    map->count = 0;
}

bool hashmap_iter(Hashmap *map, void *iter_) {
    void **iter = (void **)iter_;
    if (*iter == NULL) {
        if (map->count == 0) {
            return false;
        }
        byte *ptr = map->buckets;
        while (!((Bucket *)ptr)->occupied) {
            ptr += map->entry_size;
        }
        *iter = ptr + sizeof(Bucket);
        return true;
    }

    byte *ptr = ((byte *)(*iter)) - sizeof(Bucket);
    ptr += map->entry_size;
    if (ptr >= map->buckets_end)
        return false;
    while (!((Bucket *)ptr)->occupied) {
        ptr += map->entry_size;
        if (ptr >= map->buckets_end) {
            return false;
        }
    }

    *iter = ptr + sizeof(Bucket);
    return true;
}

void hashmap_drop(Hashmap *map) {
    if (map->drop != NULL) {
        byte *ptr = map->buckets;
        for (size_t i = 0; i < map->cap; i++) {
            Bucket *bucket = (Bucket *)ptr;
            if (bucket->occupied) {
                void *item = ptr + sizeof(Bucket);
                map->drop(item);
            }
            ptr += map->entry_size;
        }
    }

    free(map->buckets);
    free(map->alloc);
}
