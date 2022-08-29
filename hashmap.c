#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "hashmap.h"
#include "util.h"

uint32_t seed = 0;

void init_seed() {
    if (seed)
        return;
    seed = random();
}

// All the code from here:
// Is taken from the internet because I needed a simple hash function
// --------------------------------------------------------

#define PRIME1 0x9E3779B1U /*!< 0b10011110001101110111100110110001 */
#define PRIME2 0x85EBCA77U /*!< 0b10000101111010111100101001110111 */
#define PRIME3 0xC2B2AE3DU /*!< 0b11000010101100101010111000111101 */
#define PRIME4 0x27D4EB2FU /*!< 0b00100111110101001110101100101111 */
#define PRIME5 0x165667B1U /*!< 0b00010110010101100110011110110001 */

uint32_t xxhash32(uint8_t *data, size_t len) {
    size_t end = len;
    size_t offset = 0;
    uint32_t h32;
    if (len >= 16) {
        int limit = end - 16;
        uint32_t v1 = seed + PRIME1 + PRIME2;
        uint32_t v2 = seed + PRIME2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - PRIME1;

        do {
            v1 += (*(uint32_t *)(data + offset)) * PRIME2;
            v1 = rotl(v1, 13);
            v1 *= PRIME1;
            offset += 4;
            v2 += (*(uint32_t *)(data + offset)) * PRIME2;
            v2 = rotl(v2, 13);
            v2 *= PRIME1;
            offset += 4;
            v3 += (*(uint32_t *)(data + offset)) * PRIME2;
            v3 = rotl(v3, 13);
            v3 *= PRIME1;
            offset += 4;
            v4 += (*(uint32_t *)(data + offset)) * PRIME2;
            v4 = rotl(v4, 13);
            v4 *= PRIME1;
            offset += 4;
        } while (offset <= limit);
        // main loop ends
        // mix
        h32 = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    } else {
        h32 = seed + PRIME5;
    }

    if (end > 4) {
        for (h32 += len; offset <= end - 4; offset += 4) {
            h32 += (*(uint32_t *)(data + offset)) * PRIME3;
            h32 = rotl(h32, 17) * PRIME4;
        }
    }

    while (offset < end) {
        h32 += (data[offset] & 255) * PRIME5;
        h32 = rotl(h32, 11) * PRIME1;
        ++offset;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME2;
    h32 ^= h32 >> 13;
    h32 *= PRIME3;
    h32 ^= h32 >> 16;
    return h32;
}

// --------------------------------------------------------
// To here

// Po2
#define INIT_BUCKET_COUNT 32
#define HEADER_SIZE sizeof(void *)
#define GROW_THRESHOLD 75
// The size of an entry for a data size of <size>
#define ENTRY(size) (HEADER_SIZE + size)
// Each entry is composed of a 8 bytes header and the data (or garbage if there
// is none). The 8 bytes header is either a non null pointer to the key or zero
// if the bucket is empty

static inline void handle_alloc_error() {
    printf("Error when allocating (OOM?)\n");
    exit(1);
}

Map map_new(size_t data_size) {
    Map map = {};
    map.bucket_count = INIT_BUCKET_COUNT;
    map.data_size = data_size;
    map.used = 0;
    map.buckets = calloc(INIT_BUCKET_COUNT, ENTRY(data_size));
    map.mask = INIT_BUCKET_COUNT - 1;
    if (!map.buckets)
        handle_alloc_error();
    return map;
}

// Double the size of the map
void map_grow(Map *map) {
    size_t entry_size = ENTRY(map->data_size);

    size_t new_bucket_count = map->bucket_count * 2;
    void *new_alloc = calloc(new_bucket_count, entry_size);
    size_t new_mask = (map->mask << 1) + 1;
    if (!new_alloc)
        handle_alloc_error();
    void *current = map->buckets;

    for (int i = 0; i < map->bucket_count; i++) {
        void *header = *(void **)current;
        if (header == NULL)
            continue; // skip if unused
        size_t len = *(size_t *)header;
        void *key = header + sizeof(size_t);

        uint32_t hash = xxhash32(key, len);
        uint32_t index = hash & new_mask;
        void *dst_bucket = new_alloc + index * entry_size;

        while (*(void **)dst_bucket != NULL) {
            dst_bucket += entry_size;
            index++;
            if (index == new_bucket_count) {
                index = 0;
                dst_bucket = new_alloc;
            }
        }

        *(void **)dst_bucket = header; // set key of new bucket
        memcpy(dst_bucket + HEADER_SIZE, current + HEADER_SIZE,
               map->data_size); // set value of new bucket

        current += entry_size;
    }

    free(map->buckets);
    map->buckets = new_alloc;
    map->mask = new_mask;
    map->bucket_count = new_bucket_count;
}

void map_insert(Map *map, uint8_t *key, size_t key_len, void *data) {
    uint32_t hash = xxhash32(key, key_len);
    size_t index = hash & map->mask;
    size_t entry_size = ENTRY(map->data_size);
    void *bucket = map->buckets + index * entry_size;
    // Go to next empty bucket
    while (*(void **)bucket != NULL) {
        bucket += entry_size;
        index++;
        if (index == map->bucket_count) {
            index = 0;
            bucket = map->buckets;
        }
    }
    // memory for the key: the size + the data
    void *key_buf = malloc(sizeof(size_t) + key_len);
    if (!key_buf)
        handle_alloc_error();
    *(size_t *)key_buf = key_len;                   // write key_len
    memcpy(key_buf + sizeof(size_t), key, key_len); // write key

    *(void **)bucket = key_buf;
    memcpy(bucket + HEADER_SIZE, data, map->data_size);
    map->used++;

    if (map->used * 100 / map->bucket_count > GROW_THRESHOLD) {
        map_grow(map);
    }
}

inline static void *map_find(Map *map, uint8_t *key, size_t key_len) {
    uint32_t hash = xxhash32(key, key_len);
    size_t index = hash & map->mask;
    size_t entry_size = ENTRY(map->data_size);
    void *bucket = map->buckets + index * entry_size;
    size_t start_index = index;
    while (1) {
        void *header = *(void **)bucket;
        if (header) {
            size_t cur_key_len = *(size_t *)header;
            void *cur_key = header + sizeof(size_t);
            if (cur_key_len == key_len && memcmp(cur_key, key, key_len) == 0)
                break; // We found the bucket (usally the first one)
        }

        bucket += entry_size;
        index++;
        // Go back to begining if we went to the end;
        if (index == map->bucket_count) {
            index = 0;
            bucket = map->buckets;
        }
        // If we went over every entry without finding the bucket
        if (index == start_index) {
            return NULL;
        }
    }
    return bucket;
}

bool map_contains(Map *map, uint8_t *key, size_t key_len) {
    return map_find(map, key, key_len) != NULL;
}

void map_get(Map *map, uint8_t *key, size_t key_len, void *data) {
    void *bucket = map_find(map, key, key_len);
    if (!bucket) {
        printf("ERR (map_get): key not in map\n");
        return;
    }
    memcpy(data, bucket + HEADER_SIZE, map->data_size);
}

void map_remove(Map *map, uint8_t *key, size_t key_len, void *data) {
    void *bucket = map_find(map, key, key_len);
    if (!bucket) {
        printf("ERR (map_remove): key not in map\n");
        return;
    }

    if (data != NULL)
        memcpy(data, bucket + HEADER_SIZE, map->data_size);

    void *key_ptr = *(void **)bucket;
    free(key_ptr);
    *(void **)bucket = NULL;
    map->used--;
}

static inline size_t map_next_index_from(Map *map, size_t from) {
    if (from >= map->bucket_count)
        return -1;
    for (int i = from; i < map->bucket_count; i++) {
        void *bucket = map->buckets + i * ENTRY(map->data_size);
        if (*(void **)bucket != NULL)
            return i;
    }
    return -1;
}

MapIter map_iter(Map *map) {
    MapIter iter;
    iter.map = map;
    iter.next = map_next_index_from(map, 0);
    return iter;
}

bool map_iter_has_next(MapIter *iter) { return iter->next != -1; }

void map_iter_next(MapIter *iter, void **key, size_t *key_len, void **data) {
    void *bucket =
        iter->map->buckets + ENTRY(iter->map->data_size) * iter->next;
    if (key)
        *key = (*(void **)bucket) + sizeof(size_t);
    if (key_len)
        *key_len = **(size_t **)bucket;
    if (data)
        *data = bucket + HEADER_SIZE;
    iter->next = map_next_index_from(iter->map, iter->next + 1);
}

void _center(size_t length, char *format, uintmax_t value) {
    size_t l = snprintf(NULL, 0, format, value);
    char str[l + 1];
    int padleft = (length - l) / 2;
    int padright = length - l - padleft;
    snprintf(str, l + 1, format, value);
    printf("%*s%s%*s", padleft, "", str, padright, "");
}

void _centers(size_t length, char * s) {
    size_t l = strlen(s);
    char str[l + 1];
    int padleft = (length - l) / 2;
    int padright = length - l - padleft;
    snprintf(str, l + 1, "%s", s);
    printf("%*s%s%*s", padleft, "", str, padright, "");
}

void map_debug(Map map, char *format) {
    printf("%s", "┌────────┬───────────────┬───────────┬─────────┐\n");
    printf("%s", "│  map   │ buckets count │ data size │ members │\n");
    printf("%s", "├────────┼───────────────┼───────────┼─────────┤\n");
    printf("%s", "│ values │");
    _center(15, "%lu", map.bucket_count);
    printf("%s", "│");
    _center(11, "%lu", map.data_size);
    printf("%s", "│");
    _center(9, "%lu", map.used);
    printf("%s", "│\n");
    printf("%s", "└────────┴───────────────┴───────────┴─────────┘\n");
    bool last_occ = true;
    bool last_n = false;
    for (size_t i = 0; i < map.bucket_count; i++) {
        void * e = map.buckets + map.data_size * i;
        bool locc = last_occ;
        last_occ = *(void**)e != NULL;
        if (!locc && !last_occ)
            continue;
        if (locc && !last_occ) {
            if (i == 0) {
                printf("%s", "┌──────────────────────────────────────────┐\n");
            } else if (!last_n) {
                printf("%s",
                       "\033[1A├────┴──────────────────┴──────────────────┤\n");
            } else {
                printf("%s",
                       "\033[1A┌────┴──────────────────┴──────────────────┤\n");
            }
            printf("%s", "│                   .....                  │\n");
            printf("%s", "├────┬──────────────────┬──────────────────┤\n");
            continue;
        }
        printf("%s", "│");
        _center(4, "%lu", i);
        printf("%s", "│");
        void * ptr = *(void**)e;
        _centers(18, ptr == NULL ? "N/A" : ptr + sizeof(size_t));
        printf("%s", "│");
        _center(18, format, *(uint32_t *)(e + HEADER_SIZE));
        printf("%s", "│\n");
        printf("├────┼──────────────────┼──────────────────┤\n");
    }
    if (last_occ) {
        printf("\033[1A└────┴──────────────────┴──────────────────┘\n");
    } else {
        printf("\033[1A└──────────────────────────────────────────┘\n");
    }
    printf("\n\n");
}
