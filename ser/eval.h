#ifndef EVAL_H
#define EVAL_H
#include "ast.h"
#include "source.h"
#include "utils.h"
#include "vector_impl.h"

#include <stdbool.h>
#include <stdint.h>

#define _ALIGN_1 \
    { .po2 = 0, .mask = 0, .value = 1 }
#define _ALIGN_2 \
    { .po2 = 1, .mask = 1, .value = 2 }
#define _ALIGN_4 \
    { .po2 = 2, .mask = 3, .value = 4 }
#define _ALIGN_8 \
    { .po2 = 3, .mask = 7, .value = 8 }

#define ALIGN_1 ((Alignment)_ALIGN_1)
#define ALIGN_2 ((Alignment)_ALIGN_2)
#define ALIGN_4 ((Alignment)_ALIGN_4)
#define ALIGN_8 ((Alignment)_ALIGN_8)

typedef struct {
    uint8_t po2;
    uint8_t mask;
    uint8_t value;
} Alignment;

static inline uint32_t align(uint32_t v, Alignment align) { return (((v - 1) >> align.po2) + 1) << align.po2; }

typedef enum {
    SizingMax,
    SizingFixed,
} Sizing;

typedef enum {
    Primitif_u8,
    Primitif_u16,
    Primitif_u32,
    Primitif_u64,
    Primitif_i8,
    Primitif_i16,
    Primitif_i32,
    Primitif_i64,
    Primitif_f32,
    Primitif_f64,
    Primitif_char,
    Primitif_bool,
} PrimitifType;

typedef struct {
    Sizing sizing;
    uint64_t size;
    bool heap;
    struct TypeObject *type;
} Array;

void array_drop(Array a);

typedef enum {
    TypeArray,
    TypePrimitif,
    TypeStruct,
} TypeKind;

// Definition of StructObject used by TypeUnion
// Must match with later StructObject
struct StructObject {
    StringSlice name;
    AnyVec fields;
};

typedef union {
    Array array;
    PrimitifType primitif;
    struct StructObject struct_;
} TypeUnion;

typedef struct TypeObject {
    TypeKind kind;
    Alignment align;
    TypeUnion type;
} TypeObject;

void type_drop(TypeObject t);

typedef struct {
    StringSlice name;
    Span name_span;
    TypeObject *type;
} Field;

VECTOR_IMPL(Field, FieldVec, field);

typedef struct {
    StringSlice name;
    FieldVec fields;
} StructObject;

void struct_drop(StructObject s);

typedef struct {
    StringSlice name;
    TypeObject *value;
} TypeDef;

void type_decl_drop(TypeDef t);

typedef enum : uint32_t {
    AttrNone = 0,
    Attr_versioned = 1 << 0,
} Attributes;

static const uint32_t ATTRIBUTES_COUNT = 1;

typedef struct {
    StringSlice name;
    FieldVec fields;
    Attributes attributes;
} MessageObject;

void message_drop(MessageObject msg);

VECTOR_IMPL(MessageObject, MessageObjectVec, message_object, message_drop);

typedef struct {
    StringSlice name;
    MessageObjectVec messages;
    uint64_t version;
} MessagesObject;

void messages_drop(MessagesObject msg);

VECTOR_IMPL(MessagesObject, MessagesObjectVec, messages_object, messages_drop);

typedef struct {
    StringSlice name;
    bool valid;
    uint64_t value;
} Constant;

typedef struct {
    UInt64Vec indices;
    // Size of the field, or 0 if it isn't constant
    uint64_t size;
    TypeObject *type;
} FieldAccessor;

void field_accessor_drop(FieldAccessor fa);
FieldAccessor field_accessor_clone(FieldAccessor *fa);

VECTOR_IMPL(FieldAccessor, FieldAccessorVec, field_accessor, field_accessor_drop);

typedef struct {
    FieldAccessorVec fields;
    TypeObject *type;
} Layout;

Layout type_layout(TypeObject *to);

void layout_drop(void *l);

typedef struct {
    Hashmap *typedefs;
    Hashmap *layouts;
    MessagesObjectVec messages;
    PointerVec type_objects;
} Program;

void program_drop(Program p);

typedef enum {
    EETDuplicateDefinition,
    EETUnknown,
    EETCycle,
    EETInfiniteStruct,
    EETEmptyType,
} EvalErrorTag;

typedef struct {
    EvalErrorTag tag;
    Span first;
    Span second;
    StringSlice ident;
    AstTag type;
} EvalErrorDuplicateDefinition;

typedef struct {
    EvalErrorTag tag;
    Span span;
    StringSlice ident;
    AstTag type;
} EvalErrorUnknown;

typedef struct {
    EvalErrorTag tag;
    SpanVec spans;
    StringSliceVec idents;
    AstTag type;
} EvalErrorCycle;

typedef struct {
    EvalErrorTag tag;
    SpannedStringSliceVec structs;
    SpannedStringSliceVec fields;
} EvalErrorInfiniteStruct;

typedef struct {
    EvalErrorTag tag;
    Span span;
    StringSlice ident;
    AstTag type;
} EvalErrorEmptyType;

typedef union {
    EvalErrorTag tag;
    EvalErrorDuplicateDefinition dup;
    EvalErrorUnknown unk;
    EvalErrorCycle cycle;
    EvalErrorInfiniteStruct infs;
    EvalErrorEmptyType empty;
} EvalError;

void eval_error_drop(EvalError err);
void eval_error_report(Source *src, EvalError *err);

VECTOR_IMPL(EvalError, EvalErrorVec, eval_error, eval_error_drop);

typedef struct {
    EvalErrorVec errors;
    Program program;
} EvaluationResult;

EvaluationResult resolve_statics(AstContext *ctx);

extern const TypeObject PRIMITIF_u8;
extern const TypeObject PRIMITIF_u16;
extern const TypeObject PRIMITIF_u32;
extern const TypeObject PRIMITIF_u64;
extern const TypeObject PRIMITIF_i8;
extern const TypeObject PRIMITIF_i16;
extern const TypeObject PRIMITIF_i32;
extern const TypeObject PRIMITIF_i64;
extern const TypeObject PRIMITIF_f32;
extern const TypeObject PRIMITIF_f64;
extern const TypeObject PRIMITIF_char;
extern const TypeObject PRIMITIF_bool;

#endif
