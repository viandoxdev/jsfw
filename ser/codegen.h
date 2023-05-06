#ifndef CODEGEN_H
#define CODEGEN_H
#include "eval.h"
#include "vector.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Struct used to define the relative alignment when working with structs
typedef struct {
    Alignment align;
    uint8_t offset;
} CurrentAlignment;

typedef struct {
    void (*write)(void *w, const char *data, size_t len);
    void (*format)(void *w, const char *fmt, va_list args);
} Writer;

typedef struct {
    Writer w;
    CharVec buf;
} BufferedWriter;

typedef struct {
    Writer w;
    FILE *fd;
} FileWriter;

BufferedWriter buffered_writer_init();
void buffered_writer_drop(BufferedWriter w);
FileWriter file_writer_init(const char *path);
FileWriter file_writer_from_fd(FILE *fd);
void file_writer_drop(FileWriter w);

void wt_write(Writer *w, const char *data, size_t len);
void wt_format(Writer *w, const char *fmt, ...);

// Define the structs of a program in the correct order (respecting direct dependencies)
void define_structs(Program *p, Writer *w, void (*define)(Writer *w, StructObject *));
char *pascal_to_snake_case(StringSlice str);

// Check if c is aligned to alignment to
static inline bool calign_is_aligned(CurrentAlignment c, Alignment to) {
    assert(to.value <= c.align.value, "Can't know if calign is aligned to aligment if major alignment is less");
    return (c.offset & to.mask) == 0;
}
// Add offset to the offset of c
static inline CurrentAlignment calign_add(CurrentAlignment c, uint8_t offset) {
    c.offset += offset;
    c.offset &= c.align.mask;
    return c;
}
// Compute the number of bytes of padding needed to be aligned to a from c.
static inline uint8_t calign_to(CurrentAlignment c, Alignment a) {
    assert(a.value <= c.align.value, "Can't align when major alignment is less than requested alignment");
    return (-c.offset) & a.mask;
}

#endif
