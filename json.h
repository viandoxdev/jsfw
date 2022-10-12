// vi:ft=c
#ifndef JSON_H_
#define JSON_H_
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

struct JSONAdapter;

typedef struct {
    char                     *path;
    const struct JSONAdapter *type;
    size_t                    offset;
    // Function setting default value
    void (*default_func)(void *ptr);
    // Optional transformer, can be NULL
    void (*transformer)(void *arg, void *ptr);
} JSONPropertyAdapter;

struct JSONAdapter {
    const JSONPropertyAdapter *props;
    size_t                     size;
    size_t                     prop_count;
};
typedef struct JSONAdapter JSONAdapter;

void        json_adapt(uint8_t *buf, const JSONAdapter *adapter, void *ptr);
int         json_parse(const char *src, size_t src_len, uint8_t *dst, size_t dst_len);
void        json_print_value(uint8_t *buf);
const char *json_strerr(void);
size_t      json_errloc(void);
JSONError   json_errno(void);

extern const JSONAdapter NumberAdapter;
extern const JSONAdapter StringAdapter;
extern const JSONAdapter BooleanAdapter;

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

static const char * JSONTypeName[7] = {
    "[Unknown]",
    "String",
    "Number",
    "Object",
    "Array",
    "Boolean",
    "Null",
};

const JSONAdapter NumberAdapter = {
    .prop_count = Number,
    .props      = NULL,
    .size       = sizeof(double),
};
const JSONAdapter StringAdapter = {
    .prop_count = String,
    .props      = NULL,
    .size       = sizeof(char *),
};
const JSONAdapter BooleanAdapter = {
    .prop_count = Boolean,
    .props      = NULL,
    .size       = sizeof(bool),
};
#endif

#endif
