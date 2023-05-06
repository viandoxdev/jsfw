#ifndef ARENA_ALLOCATOR_H
#define ARENA_ALLOCATOR_H
#include "utils.h"
#include "vector_impl.h"

#include <stdint.h>
#include <stdlib.h>

#define ARENA_BLOCK_SIZE 4096

typedef struct {
    size_t size;
    byte *data;
    byte *end;
} ArenaBlock;

void arena_block_drop(ArenaBlock block);

VECTOR_IMPL(ArenaBlock, ArenaBlockVec, arena_block, arena_block_drop);

// Simple growing arena allocator
typedef struct {
    ArenaBlockVec blocks;
    ArenaBlock *last;
    byte *ptr;
} ArenaAllocator;

// Create a new arena allocator
ArenaAllocator arena_init();
// Allocate size bytes in the arena
void *arena_alloc(ArenaAllocator *alloc, size_t size);
// Destroy the arena, freeing its memory
void arena_drop(ArenaAllocator arena);

#endif
