// vi:ft=c
#ifndef JSON_H_
#define JSON_H_
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed, aligned(8))) {
    uint32_t type;
    uint32_t len;
} JSONHeader;

_Static_assert(sizeof(JSONHeader) == 8, "JSONHeader size isn't 8 bytes");
_Static_assert(sizeof(double) == 8, "double size isn't 8 bytes");
_Static_assert(CHAR_BIT / sizeof(char) == 8, "Byte isn't 8 bit");

typedef enum {
    String  = 1,
    Number  = 2,
    Object  = 3,
    Array   = 4,
    Boolean = 5,
    Null    = 6,
} JSONType;

typedef enum {
    NoError          = 0,
    DstOverflow      = 1,
    SrcOverflow      = 2,
    BadKeyword       = 3,
    BadChar          = 4,
    StringBadChar    = 5,
    StringBadUnicode = 6,
    StringBadEscape  = 7,
    NumberBadChar    = 8,
    ObjectBadChar    = 9,
    JERRORNO_MAX     = 10
} JSONError;

#ifdef JSON_C_
static const char *JSONErrorMessage[JERRORNO_MAX + 1] = {
    "No error",
    "Destination buffer is not big enough",
    "Source buffer overflowed before parsing finished",
    "Unknown keyword",
    "Unexpected character",
    "Unexpected character in string",
    "Bad unicoded escape in string",
    "Illegal escape in string",
    "Unexpected character in number",
    "Unexpected character in object",
    "?",
};
#endif

typedef struct {
    char    *path;
    JSONType type;
    size_t   offset;
} JSONAdapter;

void        json_adapt(uint8_t *buf, JSONAdapter *adapters, size_t adapter_count, void *ptr);
int         json_parse(const char *src, size_t src_len, uint8_t *dst, size_t dst_len);
const char *json_strerr();
size_t      json_err_loc();

#endif
