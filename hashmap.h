#ifndef HASHMAP_H
#define HASHMAP_H
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

// A hashmap
typedef struct {
    // Pointer to allocation for buckets
    void * buckets;
    // Typically sizeof(T) for Map<_, T>
    size_t data_size;
    // Always a power of 2
    size_t bucket_count;
    // How many used buckets are there
    size_t used;
    size_t mask;
} Map;

typedef struct {
    Map * map;
    size_t next;
} MapIter;

// Create a new map of a type
#define map_of(type) map_new(sizeof(type))
// Create a new map holding value of a size
Map map_new(size_t data_size);
// Insert a key value pair in the map
void map_insert(Map * map, uint8_t * key, size_t key_len, void * data);
// Test if a key exist within a map
bool map_contains(Map * map, uint8_t * key, size_t key_len);
// Get the value of a key in the map into data
void map_get(Map * map, uint8_t * key, size_t key_len, void * data);
// Remove a key value pair from the map, if data is not NULL, write value to it.
void map_remove(Map * map, uint8_t * key, size_t key_len, void * data);
// Get an iterator of the map
MapIter map_iter(Map * map);
// Test if there is a next
bool map_iter_has_next(MapIter * iter);
// Get the next value, if key is not NULL, put a pointer to the key in key, if key_len is not NULL
// put the len of the key in key_len, if data is not null, put a pointer to the data in data.
void map_iter_next(MapIter * iter, void ** key, size_t * key_len, void ** data);

// map_insert with a string key
static inline void map_insert_str(Map * map, const char * key, void * data) {
    size_t len = strlen(key) + 1;
    map_insert(map, (void *)key, len, data);
}
// map_contains with a string key
static inline bool map_contains_str(Map * map, const char * key) {
    size_t len = strlen(key) + 1;
    return map_contains(map, (void *)key, len);
}
// map_get with a string key
static inline void map_get_str(Map * map, const char * key, void * data) {
    size_t len = strlen(key) + 1;
    map_get(map, (void *)key, len, data);
}
// map_remove with a string key
static inline void map_remove_str(Map * map, const char * key, void * data) {
    size_t len = strlen(key) + 1;
    map_remove(map, (void *)key, len, data);
}

void map_debug(Map map, char *format);
#endif
