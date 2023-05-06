#include "arena_allocator.h"

#include "assert.h"
#include "vector.h"

#include <stdlib.h>
#include <string.h>

static ArenaBlock arena_block_alloc(size_t size) {
    size = size < ARENA_BLOCK_SIZE ? ARENA_BLOCK_SIZE : size;
    byte *ptr = malloc(size);
    assert_alloc(ptr);
    return (ArenaBlock){.data = ptr, .size = size, .end = ptr + size};
}

void arena_block_drop(ArenaBlock block) { free(block.data); }

ArenaAllocator arena_init() {
    ArenaBlock block = arena_block_alloc(ARENA_BLOCK_SIZE);
    ArenaBlockVec blocks = vec_init();
    vec_grow(&blocks, 256);
    vec_push(&blocks, block);
    ArenaBlock *last = blocks.data;
    return (ArenaAllocator){.blocks = blocks, .ptr = last->data, .last = last};
}
void *arena_alloc(ArenaAllocator *alloc, size_t size) {
    if (alloc->ptr + size > alloc->last->end) {
        ArenaBlock block = arena_block_alloc(size);
        vec_push(&alloc->blocks, block);
        ArenaBlock *last = &alloc->blocks.data[alloc->blocks.len - 1];
        alloc->ptr = last->data;
        alloc->last = last;
    }

    byte *ptr = alloc->ptr;
    alloc->ptr += size;
    return ptr;
}
void arena_drop(ArenaAllocator arena) { vec_drop(arena.blocks); }
