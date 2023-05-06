#ifndef AST_H
#define AST_H
#include "arena_allocator.h"
#include "lexer.h"
#include "source.h"
#include "vector_impl.h"

typedef enum {
    ATNumber,
    ATVersion,
    ATIdent,
    ATHeapArray,
    ATFieldArray,
    ATMaxSize,
    ATFixedSize,
    ATNoSize,
    ATField,
    ATAttribute,
    ATStruct,
    ATMessage,
    ATMessages,
    ATTypeDecl,
    ATConstant,
    ATItems,
} AstTag;

typedef struct {
    AstTag tag;
    Span span;
    Token token;
} AstNumber;

typedef struct {
    AstTag tag;
    Span span;
    Token token;
} AstIdent;

typedef struct {
    AstTag tag;
    Span span;
    AstNumber version;
} AstVersion;

typedef struct {
    AstTag tag;
    Span span;
    AstNumber value;
} AstSize;

typedef struct {
    AstTag tag;
    Span span;
    struct AstType *type;
    AstSize size;
} AstArray;

typedef union {
    AstTag tag;
    AstIdent ident;
    AstArray array;
} AstType;

typedef struct {
    AstTag tag;
    Span span;
    Token name;
    AstType type;
} AstField;

VECTOR_IMPL(AstField, AstFieldVec, ast_field);

typedef struct {
    AstTag tag;
    Span span;
    Token ident;
    AstFieldVec fields;
} AstStruct;

typedef struct {
    AstTag tag;
    Span span;
    Token ident;
    AstFieldVec fields;
} AstMessage;

typedef struct {
    AstTag tag;
    Span span;
    Token ident;
} AstAttribute;

typedef union {
    AstTag tag;
    AstMessage message;
    AstAttribute attribute;
} AstAttributeOrMessage;

VECTOR_IMPL(AstAttributeOrMessage, AstAttributeOrMessageVec, ast_attribute_or_message);

typedef struct {
    AstTag tag;
    Span span;
    Token name;
    AstAttributeOrMessageVec children;
} AstMessages;

typedef struct {
    AstTag tag;
    Span span;
    Token name;
    AstType value;
} AstTypeDecl;

typedef struct {
    AstTag tag;
    Span span;
    Token name;
    AstNumber value;
} AstConstant;

typedef union {
    AstTag tag;
    AstTypeDecl type_decl;
    AstVersion version;
    AstStruct struct_;
    AstMessages messages;
    AstConstant constant;
} AstItem;

VECTOR_IMPL(AstItem, AstItemVec, ast_item);

typedef struct {
    AstTag tag;
    Span span;
    AstItemVec items;
} AstItems;

typedef union {
    AstTag tag;
    AstNumber number;
    AstIdent ident;
    AstVersion version;
    AstSize size;
    AstArray array;
    AstType type;
    AstField field;
    AstStruct struct_;
    AstMessage message;
    AstAttribute attribute;
    AstAttributeOrMessage attribute_or_message;
    AstMessages messages;
    AstTypeDecl type_decl;
    AstConstant constant;
    AstItems items;
} AstNode;

typedef struct {
    AstNode *root;
    ArenaAllocator alloc;
} AstContext;

AstContext ast_init();

void ast_drop(AstContext ctx);

static inline AstNumber ast_number(AstContext ctx, Span span, Token lit) {
    AstNumber res;
    res.tag = ATNumber;
    res.span = span;
    res.token = lit;
    return res;
}

static inline AstIdent ast_ident(AstContext ctx, Span span, Token ident) {
    AstIdent res;
    res.tag = ATIdent;
    res.span = span;
    res.token = ident;
    return res;
}

static inline AstVersion ast_version(AstContext ctx, Span span, AstNumber number) {
    AstVersion res;
    res.tag = ATVersion;
    res.span = span;
    res.version = number;
    return res;
}

static inline AstArray ast_heap_array(AstContext ctx, Span span, AstType *type, AstSize size) {
    AstArray res;
    res.tag = ATHeapArray;
    res.span = span;
    res.type = (struct AstType *)type;
    res.size = size;
    return res;
}

static inline AstArray ast_field_array(AstContext ctx, Span span, AstType *type, AstSize size) {
    AstArray res;
    res.tag = ATFieldArray;
    res.span = span;
    res.type = (struct AstType *)type;
    res.size = size;
    return res;
}

static inline AstSize ast_max_size(AstContext ctx, Span span, AstNumber size) {
    AstSize res;
    res.tag = ATMaxSize;
    res.span = span;
    res.value = size;
    return res;
}

static inline AstSize ast_fixed_size(AstContext ctx, Span span, AstNumber size) {
    AstSize res;
    res.tag = ATFixedSize;
    res.span = span;
    res.value = size;
    return res;
}

static inline AstSize ast_no_size(AstContext ctx, Span span) {
    AstSize res;
    res.tag = ATNoSize;
    res.span = span;
    return res;
}

static inline AstField ast_field(AstContext ctx, Span span, Token name, AstType type) {
    AstField res;
    res.tag = ATField;
    res.span = span;
    res.name = name;
    res.type = type;
    return res;
}

static inline AstStruct ast_struct(AstContext ctx, Span span, Token name, AstFieldVec fields) {
    AstStruct res;
    res.tag = ATStruct;
    res.span = span;
    res.ident = name;
    res.fields = fields;
    return res;
}

static inline AstMessage ast_message(AstContext ctx, Span span, Token name, AstFieldVec fields) {
    AstMessage res;
    res.tag = ATMessage;
    res.span = span;
    res.ident = name;
    res.fields = fields;
    return res;
}

static inline AstAttribute ast_attribute(AstContext ctx, Span span, Token attribute) {
    AstAttribute res;
    res.tag = ATAttribute;
    res.span = span;
    res.ident = attribute;
    return res;
}

static inline AstMessages ast_messages(AstContext ctx, Span span, Token name, AstAttributeOrMessageVec children) {
    AstMessages res;
    res.tag = ATMessages;
    res.span = span;
    res.name = name;
    res.children = children;
    return res;
}

static inline AstConstant ast_constant(AstContext ctx, Span span, Token name, AstNumber value) {
    AstConstant res;
    res.tag = ATConstant;
    res.span = span;
    res.name = name;
    res.value = value;
    return res;
}

static inline AstTypeDecl ast_type_decl(AstContext ctx, Span span, Token name, AstType type) {
    AstTypeDecl res;
    res.tag = ATTypeDecl;
    res.span = span;
    res.name = name;
    res.value = type;
    return res;
}

static inline AstItems ast_items(AstContext ctx, Span span, AstItemVec items) {
    AstItems res;
    res.tag = ATItems;
    res.span = span;
    res.items = items;
    return res;
}

void ast_print(AstNode *node);

static inline const char *ast_tag_to_string(AstTag tag) {
#define _case(c) \
    case AT##c: \
        return #c
    switch (tag) {
        _case(Number);
        _case(Version);
        _case(Ident);
        _case(HeapArray);
        _case(FieldArray);
        _case(MaxSize);
        _case(FixedSize);
        _case(NoSize);
        _case(Field);
        _case(Struct);
        _case(Message);
        _case(Attribute);
        _case(Messages);
        _case(TypeDecl);
        _case(Constant);
        _case(Items);
    }
#undef _case
}

#endif
