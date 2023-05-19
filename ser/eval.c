#include "eval.h"

#include "ast.h"
#include "gen_vec.h"
#include "hashmap.h"
#include "vector.h"
#include "vector_impl.h"

#include <stdbool.h>
#include <string.h>

#define PRIMITIF_TO(name, al) \
    const TypeObject PRIMITIF_##name = {.kind = TypePrimitif, .align = _ALIGN_##al, .type.primitif = Primitif_##name}
PRIMITIF_TO(u8, 1);
PRIMITIF_TO(u16, 2);
PRIMITIF_TO(u32, 4);
PRIMITIF_TO(u64, 8);
PRIMITIF_TO(i8, 1);
PRIMITIF_TO(i16, 2);
PRIMITIF_TO(i32, 4);
PRIMITIF_TO(i64, 8);
PRIMITIF_TO(f32, 4);
PRIMITIF_TO(f64, 8);
PRIMITIF_TO(char, 1);
PRIMITIF_TO(bool, 1);
#undef PRIMITIF_TO

void array_drop(Array a) { free(a.type); }

void type_drop(TypeObject t) {
    if (t.kind == TypeArray) {
        array_drop(t.type.array);
    }
}

void struct_drop(StructObject s) { vec_drop(s.fields); }

void message_drop(MessageObject m) { vec_drop(m.fields); }

void messages_drop(MessagesObject m) { vec_drop(m.messages); }

static Alignment max_alignment(Alignment a, Alignment b) {
    if (a.value > b.value) {
        return a;
    } else {
        return b;
    }
}

static char *attributes_to_string(Attributes attrs, bool and) {
    uint32_t count = 0;
    Attributes attributes[ATTRIBUTES_COUNT];
#define handle(a) \
    if (attrs & Attr_##a) \
        attributes[count++] = Attr_##a;
    handle(versioned);
#undef handle
    CharVec res = vec_init();
    for (size_t i = 0; i < count; i++) {
        if (i == 0) {
        } else if (i < count - 1) {
            vec_push_array(&res, ", ", 2);
        } else if (and) {
            vec_push_array(&res, " and ", 5);
        } else {
            vec_push_array(&res, " or ", 4);
        }

#define _case(x) \
    case Attr_##x: \
        vec_push_array(&res, #x, sizeof(#x) - 1); \
        break
        switch (attributes[i]) {
            _case(versioned);
        default:
            vec_push_array(&res, "(invalid attribute)", 19);
            break;
        }
#undef _case
    }
    vec_push(&res, '\0');
    return res.data;
}

static inline EvalError err_duplicate_def(Span first, Span second, AstTag type, StringSlice ident) {
    return (EvalError){
        .dup = {.tag = EETDuplicateDefinition, .first = first, .second = second, .type = type, .ident = ident}
    };
}

static inline EvalError err_unknown(Span span, AstTag type, StringSlice ident) {
    return (EvalError){
        .unk = {.tag = EETUnknown, .span = span, .type = type, .ident = ident}
    };
}

static inline EvalError err_empty(Span span, AstTag type, StringSlice ident) {
    return (EvalError){
        .empty = {.tag = EETEmptyType, .span = span, .type = type, .ident = ident}
    };
}

void eval_error_report(Source *src, EvalError *err) {
    switch (err->tag) {
    case EETUnknown: {
        EvalErrorUnknown unk = err->unk;
        ReportSpan span = {.span = unk.span, .sev = ReportSeverityError};
        const char *type = "<invalid type>";
        switch (unk.type) {
        case ATConstant:
            type = "constant";
            break;
        case ATAttribute:
            type = "attribute";
            break;
        default:
            type = "identifier";
            break;
        }
        char *help = NULL;
        if (unk.type == ATAttribute) {
            char *attributes = attributes_to_string(~0, false);
            help = msprintf("expected %s", attributes);
            free(attributes);
        }
        source_report(
            src,
            unk.span.loc,
            ReportSeverityError,
            &span,
            1,
            help,
            "Unknown %s '%.*s'",
            type,
            unk.ident.len,
            unk.ident.ptr
        );
        if (help != NULL) {
            free(help);
        }
        break;
    }
    case EETDuplicateDefinition: {
        EvalErrorDuplicateDefinition dup = err->dup;
        ReportSpan spans[] = {
            {.span = dup.first,
             .sev = ReportSeverityNote,
             .message = msprintf("first definition of '%.*s' here", dup.ident.len, dup.ident.ptr)},
            {.span = dup.second, .sev = ReportSeverityError, .message = "redefined here"}
        };
        const char *type = "<invalid type>";
        switch (dup.type) {
        case ATConstant:
            type = "constant";
            break;
        case ATIdent:
            type = "identifier";
            break;
        case ATField:
            type = "field";
            break;
        default:
            break;
        }
        source_report(
            src,
            dup.second.loc,
            ReportSeverityError,
            spans,
            2,
            NULL,
            "Duplicate definition of %s '%.*s'",
            type,
            dup.ident.len,
            dup.ident.ptr
        );
        free((char *)spans[0].message);
        break;
    }
    case EETCycle: {
        EvalErrorCycle cycle = err->cycle;
        const char *type = "<invalid type>";
        if (cycle.type == ATConstant) {
            type = "constant";
        } else if (cycle.type == ATTypeDecl) {
            type = "type declaration";
        }
        // Check if the spans are ordered
        bool spans_ascending = true;
        bool spans_descending = true;
        for (size_t i = 1; i < cycle.spans.len; i++) {
            int comp = span_compare(&cycle.spans.data[i - 1], &cycle.spans.data[i]);
            spans_ascending = spans_ascending && comp >= 0;
            spans_descending = spans_descending && comp <= 0;
            if (!spans_ascending && !spans_descending)
                break;
        }
        bool ordered = spans_ascending | spans_descending;
        if (ordered) {
            // If they are, we can print the info on a span each (less noisy output)
            ReportSpanVec spans = vec_init();
            vec_grow(&spans, cycle.spans.len);

            for (size_t i = 0; i < cycle.spans.len; i++) {
                ReportSeverity sev;
                char *message;
                StringSlice name = cycle.idents.data[i];
                StringSlice next_name = cycle.idents.data[(i + 1) % cycle.idents.len];

                if (cycle.spans.len == 1) { // Special case for a constant equal to itself
                    sev = ReportSeverityError;
                    message = msprintf("%.*s requires evaluating itself", name.len, name.ptr);
                } else if (i == 0) { // First equality
                    sev = ReportSeverityError;
                    message = msprintf("%.*s requires evaluating %.*s", name.len, name.ptr, next_name.len, next_name.ptr);
                } else if (i < cycle.spans.len - 1) {
                    sev = ReportSeverityNote;
                    message = msprintf("... which requires %.*s ...", next_name.len, next_name.ptr);
                } else { // Looparound
                    sev = ReportSeverityNote;
                    message = msprintf("... which again requires %.*s", next_name.len, next_name.ptr);
                }

                vec_push(&spans, ((ReportSpan){.span = cycle.spans.data[i], .sev = sev, .message = message}));
            }

            source_report(
                src,
                cycle.spans.data[0].loc,
                ReportSeverityError,
                spans.data,
                spans.len,
                NULL,
                "cycle detected when evaluating %s '%.*s'",
                type,
                cycle.idents.data[0].len,
                cycle.idents.data[0].ptr
            );

            for (size_t i = 0; i < spans.len; i++) {
                free((char *)spans.data[i].message);
            }
            vec_drop(spans);
        } else {
            // If they aren't we have to use a report per span (because the lines are not ordered)
            ReportSpan span;

            span.span = cycle.spans.data[0];
            span.sev = ReportSeverityError;
            if (cycle.spans.len >= 2) {
                span.message = NULL;
            } else {
                span.message = msprintf("%.*s requires evaluating itself", cycle.idents.data[0].len, cycle.idents.data[0].ptr);
            }

            source_report(
                src,
                cycle.spans.data[0].loc,
                ReportSeverityError,
                &span,
                1,
                NULL,
                "cycle detected when evaluating %s '%.*s'",
                type,
                cycle.idents.data[0].len,
                cycle.idents.data[0].ptr
            );

            if (span.message != NULL) {
                free((char *)span.message);
            }

            span.sev = ReportSeverityNote;
            span.message = NULL;
            for (size_t i = 1; i < cycle.idents.len; i++) {
                span.span = cycle.spans.data[i];
                StringSlice name = cycle.idents.data[i];

                if (i == cycle.idents.len - 1) {
                    span.message =
                        msprintf("which again requires evaluating %.*s", cycle.idents.data[0].len, cycle.idents.data[0].ptr);
                }
                source_report(
                    src,
                    span.span.loc,
                    ReportSeverityNote,
                    &span,
                    1,
                    NULL,
                    "... which requires evaluating %.*s ...",
                    name.len,
                    name.ptr
                );
            }
            free((char *)span.message);
        }
        break;
    }
    case EETInfiniteStruct: {
        EvalErrorInfiniteStruct infs = err->infs;
        ReportSpanVec spans = vec_init();
        CharVec structs = vec_init();
        vec_grow(&spans, infs.fields.len + infs.structs.len);

        vec_push(&structs, '\'');
        SpannedStringSlice last_struct = infs.structs.data[infs.structs.len - 1];
        vec_push_array(&structs, last_struct.slice.ptr, last_struct.span.len);
        vec_push(&structs, '\'');
        for (int i = infs.structs.len - 2; i >= 0; i--) {
            if (i == 1) {
                vec_push_array(&structs, " and ", 5);
            } else {
                vec_push_array(&structs, ", ", 2);
            }
            SpannedStringSlice s = infs.structs.data[i];
            vec_push(&structs, '\'');
            vec_push_array(&structs, s.slice.ptr, s.slice.len);
            vec_push(&structs, '\'');
        }
        vec_push(&structs, '\0');

        for (int i = infs.structs.len - 1; i >= 0; i--) {
            ReportSpan span[] = {
                {.sev = ReportSeverityError, .message = NULL,                      .span = infs.structs.data[i].span},
                {.sev = ReportSeverityNote,  .message = "recursive without limit", .span = infs.fields.data[i].span }
            };
            vec_push_array(&spans, span, 2);
        }

        source_report(
            src,
            infs.structs.data[0].span.loc,
            ReportSeverityError,
            spans.data,
            spans.len,
            "insert some limiting indirection ('[]', '&[]', or '&[^max size]') to break the cycle",
            "recursive struct%s %s ha%s infinite size",
            infs.structs.len > 1 ? "s" : "",
            structs.data,
            infs.structs.len > 1 ? "ve" : "s"
        );
        vec_drop(spans);
        vec_drop(structs);
        break;
    }
    case EETEmptyType: {
        EvalErrorEmptyType empty = err->empty;
        char *type = "<invalid type>";
        if (empty.type == ATStruct) {
            type = "struct";
        } else if (empty.type == ATMessage) {
            type = "message";
        }

        ReportSpan span = {.span = empty.span, .sev = ReportSeverityError, .message = "zero sized types aren't allowed"};

        source_report(
            src,
            empty.span.loc,
            ReportSeverityError,
            &span,
            1,
            NULL,
            "%s '%.*s' doesn't have any field",
            type,
            empty.ident.len,
            empty.ident.ptr
        );
        break;
    }
    }
    fprintf(stderr, "\n");
}

void eval_error_drop(EvalError err) {
    switch (err.tag) {
    case EETCycle:
        vec_drop(err.cycle.idents);
        vec_drop(err.cycle.spans);
        break;
    case EETInfiniteStruct:
        vec_drop(err.infs.structs);
        vec_drop(err.infs.fields);
    default:
        break;
    }
}

static inline StringSlice string_slice_from_token(Token t) { return (StringSlice){.ptr = t.lexeme, .len = t.span.len}; }

static SpannedStringSlice sss_from_token(Token t) {
    return (SpannedStringSlice){.slice.ptr = t.lexeme, .slice.len = t.span.len, .span = t.span};
}

typedef struct {
    Hashmap *constants;
    Hashmap *typedefs;
    Hashmap *layouts;
    Hashmap *unresolved;
    Hashmap *names;
    PointerVec type_objects;
    AstItemVec *items;
    EvalErrorVec errors;
    MessagesObjectVec messages;
} EvaluationContext;

typedef struct {
    StringSlice constant;
    Token value;
    Span name_span;
    Span span;
} UnresolvedConstant;

typedef struct {
    StringSlice type;
    AstNode value;
    Span name_span;
    Span span;
} UnresolvedTypeDef;

typedef struct {
    TypeObject *type;
    StringSlice name;
} TypeName;

impl_hashmap_delegate(unconst, UnresolvedConstant, string_slice, constant);
impl_hashmap_delegate(const, Constant, string_slice, name);
impl_hashmap_delegate(untypd, UnresolvedTypeDef, string_slice, type);
impl_hashmap_delegate(typedef, TypeDef, string_slice, name);
impl_hashmap(
    layout, Layout, { return hash(state, (byte *)&v->type, sizeof(TypeObject *)); }, { return a->type == b->type; }
);
impl_hashmap(
    typename, TypeName, { return hash(state, (byte *)&v->type, sizeof(TypeObject *)); }, { return a->type == b->type; }
);

static uint64_t get_ast_number_value(EvaluationContext *ctx, AstNumber number) {
    if (number.token.type == Number) {
        return number.token.lit;
    } else { // The token is an Ident
        StringSlice ident = string_slice_from_token(number.token);
        Constant *c = hashmap_get(ctx->constants, &(Constant){.name = ident});
        if (c != NULL) {
            // If the constant is invalid we make up a value to continue checking for errors
            // (Since it is invalid there already has been at least one and we know this code
            // can't go to the next stage)
            return c->valid ? c->value : 0;
        } else {
            // This constant doesn't exist: raise an error and return dummy value to continue
            vec_push(&ctx->errors, err_unknown(number.token.span, ATConstant, ident));
            return 0;
        }
    }
}

static Sizing ast_size_to_sizing(EvaluationContext *ctx, AstSize size, uint64_t *value) {
    if (size.tag == ATMaxSize) {
        *value = get_ast_number_value(ctx, size.value);
        return SizingMax;
    } else if (size.tag == ATFixedSize) {
        *value = get_ast_number_value(ctx, size.value);
        return SizingFixed;
    } else {
        *value = UINT16_MAX;
        return SizingMax;
    }
}

static void _type_print(Hashmap *type_set, TypeObject *type) {
    if (type == NULL) {
        fprintf(stderr, "(invalid)");
        return;
    }

    if (hashmap_set(type_set, &type)) {
        if (type->kind == TypeStruct) {
            fprintf(stderr, "%.*s", type->type.struct_.name.len, type->type.struct_.name.ptr);
        } else {
            fprintf(stderr, "(recursion)");
        }
        return;
    };

    if (type->kind == TypePrimitif) {
#define _case(t) \
    case Primitif_##t: \
        fprintf(stderr, #t); \
        break
        switch (type->type.primitif) {
            _case(u8);
            _case(u16);
            _case(u32);
            _case(u64);
            _case(i8);
            _case(i16);
            _case(i32);
            _case(i64);
            _case(f32);
            _case(f64);
            _case(char);
            _case(bool);
        }
#undef _case
    } else if (type->kind == TypeArray) {
        _type_print(type_set, (TypeObject *)type->type.array.type);
        if (type->type.array.heap)
            fprintf(stderr, "&");
        if (type->type.array.sizing == SizingFixed)
            fprintf(stderr, "[%lu]", type->type.array.size);
        else if (type->type.array.sizing == SizingMax)
            fprintf(stderr, "[^%lu]", type->type.array.size);
        else
            fprintf(stderr, "[]");
    } else {
        StructObject s = *(StructObject *)&type->type.struct_;
        fprintf(stderr, "{ ");
        for (size_t i = 0; i < s.fields.len; i++) {
            fprintf(stderr, "%.*s: ", s.fields.data[i].name.len, s.fields.data[i].name.ptr);
            _type_print(type_set, s.fields.data[i].type);
            if (i < s.fields.len - 1) {
                fprintf(stderr, ", ");
            }
        }
        fprintf(stderr, " }");
    }
}

__attribute__((unused)) static void type_print(TypeObject *type) {
    Hashmap *type_set = hashmap_init(pointer_hash, pointer_equal, NULL, sizeof(TypeObject *));
    _type_print(type_set, type);
    hashmap_drop(type_set);
}

static TypeObject *resolve_type(EvaluationContext *ctx, SpannedStringSlice name);

static TypeObject *ast_type_to_type_obj(EvaluationContext *ctx, AstType type) {
    if (type.tag == ATHeapArray || type.tag == ATFieldArray) {
        TypeObject *res = malloc(sizeof(TypeObject));
        assert_alloc(res);
        vec_push(&ctx->type_objects, res);
        res->kind = TypeArray;
        res->type.array.heap = type.tag == ATHeapArray;
        res->type.array.sizing = ast_size_to_sizing(ctx, type.array.size, &res->type.array.size);
        res->type.array.type = (struct TypeObject *)ast_type_to_type_obj(ctx, *(AstType *)type.array.type);
        res->align.value = 0;
        return res;
    } else { // Otherwise the type is an identifier
        return resolve_type(ctx, sss_from_token(type.ident.token));
    }
}

static TypeObject *resolve_type(EvaluationContext *ctx, SpannedStringSlice name) {
    TypeDef *type_def = hashmap_get(ctx->typedefs, &(TypeDef){.name = name.slice});
    if (type_def != NULL) { // Type is already resolved
        return type_def->value;
    }

    // Type isn't defined anywhere
    if (ctx->unresolved == NULL || !hashmap_has(ctx->unresolved, &(UnresolvedTypeDef){.type = name.slice})) {
        vec_push(&ctx->errors, err_unknown(name.span, ATIdent, name.slice));
        return NULL;
    }

    UnresolvedTypeDef *untd = hashmap_get(ctx->unresolved, &(UnresolvedTypeDef){.type = name.slice});

    if (untd->value.tag == ATIdent || untd->value.tag == ATFieldArray || untd->value.tag == ATHeapArray) {
        hashmap_set(ctx->typedefs, &(TypeDef){.name = name.slice, .value = NULL});
        TypeObject *value = ast_type_to_type_obj(ctx, *(AstType *)&untd->value);
        hashmap_set(ctx->typedefs, &(TypeDef){.name = name.slice, .value = value});
        return value;
    } else { // Otherwise the value is a struct
        AstStruct str = untd->value.struct_;
        TypeObject *value = malloc(sizeof(TypeObject));
        {
            FieldVec fields = vec_init();
            vec_grow(&fields, str.fields.len);
            assert_alloc(value);
            vec_push(&ctx->type_objects, value);
            value->kind = TypeStruct;
            value->type.struct_.fields = *(AnyVec *)&fields;
            value->type.struct_.name = name.slice;
            value->type.struct_.has_funcs = false;
            value->align.value = 0;
            hashmap_set(ctx->typedefs, &(TypeDef){.name = name.slice, .value = value});
        }
        StructObject *stro = (StructObject *)&value->type.struct_;

        for (size_t i = 0; i < str.fields.len; i++) {
            Field f;
            f.name = string_slice_from_token(str.fields.data[i].name);
            f.name_span = str.fields.data[i].name.span;
            f.type = ast_type_to_type_obj(ctx, str.fields.data[i].type);
            vec_push(&stro->fields, f);
        }

        return value;
    }
}

// Check struct object for direct recursion, returns true if the struct contains a reference to rec somewhere
static bool check_for_recursion(
    EvaluationContext *ctx, EvalErrorInfiniteStruct *err, Hashmap *checked, Hashmap *invalids, TypeObject *rec, StructObject *str
) {
    // Shortcircuit if we already checked this struct
    // This also avoids running into recursion
    // (In the case of invalids there already has been an error, so we don't produce another)
    if (hashmap_set(checked, &str) || hashmap_has(invalids, &str)) {
        return false;
    }

    for (size_t i = 0; i < str->fields.len; i++) {
        Field f = str->fields.data[i];

        TypeObject *type = f.type;
        if (type == NULL)
            continue;

        // Non heap arrays work very much the same as regular fields, Fixed size array as well (with added indirection)
        while (type->kind == TypeArray && (!type->type.array.heap || type->type.array.sizing == SizingFixed)) {
            type = type->type.array.type;
        }

        // Anything else won't recurse: primitives can't, and heap arrays add indirection
        // (Field arrays have been eliminated above)
        if (type->kind != TypeStruct) {
            continue;
        }

        // If we got here the type is a struct
        StructObject *obj = (StructObject *)&type->type.struct_;

        if (type == rec || check_for_recursion(ctx, err, checked, invalids, rec, obj)) {
            // The struct contains rec

            UnresolvedTypeDef *unr = hashmap_get(ctx->unresolved, &(UnresolvedTypeDef){.type = str->name});
            SpannedStringSlice struct_ = {.slice = unr->type, .span = unr->name_span};
            AstField af = unr->value.struct_.fields.data[i];
            // af can be either ATFieldArray or ATIdent
            while (af.type.tag == ATFieldArray || af.type.tag == ATHeapArray) {
                af.type = *(AstType *)af.type.array.type;
            }
            SpannedStringSlice field = sss_from_token(af.type.ident.token);

            vec_push(&err->structs, struct_);
            vec_push(&err->fields, field);

            hashmap_set(invalids, &str);

            return true;
        }
    }

    return false;
}

static Alignment resolve_alignment(TypeObject *type, Hashmap *seen) {
    // Check if the type has already been resolved
    if (type->align.value != 0) {
        return type->align;
    }

    // Avoid cycles: if we already have seen this type (but not resolved), no need to check it again
    // (since we're computing the max)
    if (hashmap_set(seen, &type)) {
        return ALIGN_1;
    }

    if (type->kind == TypeStruct) {
        Alignment res = ALIGN_1;
        StructObject *s = (StructObject *)&type->type.struct_;
        for (size_t i = 0; i < s->fields.len; i++) {
            res = max_alignment(res, resolve_alignment(s->fields.data[i].type, seen));
        }
        return res;
    }

    // Type is type array (since primitive already have an alignment)
    debug_assert(type->kind == TypeArray, "");

    if (type->type.array.sizing == SizingMax) {
        uint64_t size = type->type.array.size;
        Alignment res;
        if (size <= UINT8_MAX) {
            res = ALIGN_1;
        } else if (size <= UINT16_MAX) {
            res = ALIGN_2;
        } else if (size <= UINT32_MAX) {
            res = ALIGN_4;
        } else {
            res = ALIGN_8;
        }
        res = max_alignment(res, resolve_alignment(type->type.array.type, seen));
        return res;
    }

    // Type is fixed size array
    return resolve_alignment(type->type.array.type, seen);
}

void field_accessor_drop(FieldAccessor fa) { vec_drop(fa.indices); }
FieldAccessor field_accessor_clone(FieldAccessor *fa) {
    return (FieldAccessor){.type = fa->type, .size = fa->size, .indices = vec_clone(&fa->indices)};
}

void layout_drop(void *l) { vec_drop(((Layout *)l)->fields); }

static void add_fields(FieldAccessorVec *v, TypeObject *t, const uint64_t *base, size_t len) {
    if (t->kind == TypePrimitif) {
        FieldAccessor fa = {.indices = vec_init()};
#define _case(typ, n) \
    case Primitif_##typ: \
        fa.size = n; \
        fa.type = (TypeObject *)&PRIMITIF_##typ; \
        break;
        switch (t->type.primitif) {
            _case(bool, 1);
            _case(char, 1);
            _case(i8, 1);
            _case(u8, 1);
            _case(i16, 2);
            _case(u16, 2);
            _case(i32, 4);
            _case(u32, 4);
            _case(f32, 4);
            _case(i64, 8);
            _case(u64, 8);
            _case(f64, 8);
        }
#undef _case
        vec_push_array(&fa.indices, base, len);
        vec_push(v, fa);
    } else if (t->kind == TypeStruct) {
        StructObject *s = (StructObject *)&t->type.struct_;
        UInt64Vec new_base = vec_init();
        vec_grow(&new_base, len + 1);
        vec_push_array(&new_base, base, len);
        vec_push(&new_base, 0);
        for (size_t i = 0; i < s->fields.len; i++) {
            new_base.data[len] = i;
            add_fields(v, s->fields.data[i].type, new_base.data, new_base.len);
        }
        vec_drop(new_base);
    } else { // Type is array
        if (t->type.array.sizing == SizingMax) {
            FieldAccessor fa = {.indices = vec_init()};
            FieldAccessor fl = {.indices = vec_init()};
            vec_grow(&fa.indices, len + 1);
            vec_grow(&fl.indices, len + 1);
            vec_push_array(&fa.indices, base, len);
            vec_push_array(&fl.indices, base, len);
            vec_push(&fa.indices, 1);
            vec_push(&fl.indices, 0);

            fa.size = 0;
            fa.type = t->type.array.type;

            uint64_t size = t->type.array.size;
            if (size <= UINT8_MAX) {
                fl.size = 1;
                fl.type = (TypeObject *)&PRIMITIF_u8;
            } else if (size <= UINT16_MAX) {
                fl.size = 2;
                fl.type = (TypeObject *)&PRIMITIF_u16;
            } else if (size <= UINT32_MAX) {
                fl.size = 4;
                fl.type = (TypeObject *)&PRIMITIF_u32;
            } else {
                fl.size = 8;
                fl.type = (TypeObject *)&PRIMITIF_u64;
            }

            vec_push(v, fa);
            vec_push(v, fl);
        } else {
            UInt64Vec new_base = vec_init();
            vec_grow(&new_base, len + 1);
            vec_push_array(&new_base, base, len);
            vec_push(&new_base, 0);
            for (size_t i = 0; i < t->type.array.size; i++) {
                new_base.data[len] = i;
                add_fields(v, t->type.array.type, new_base.data, new_base.len);
            }
            vec_drop(new_base);
        }
    }
}

static int fa_compare(const void *a, const void *b) {
    const FieldAccessor *fa = (const FieldAccessor *)a;
    const FieldAccessor *fb = (const FieldAccessor *)b;
    if (fb->size != 0 && fa->size == 0)
        return 1;
    if (fa->size != 0 && fb->size == 0)
        return -1;
    return (int)fb->type->align.value - (int)fa->type->align.value;
}

Layout type_layout(TypeObject *type) {
    Layout l = {.type = type, .fields = vec_init()};
    add_fields(&l.fields, type, NULL, 0);
    qsort(l.fields.data, l.fields.len, sizeof(FieldAccessor), fa_compare);
    return l;
}

static void resolve_types(EvaluationContext *ctx) {
    AstItemVec *items = ctx->items;
    Hashmap *untypds = hashmap_init(untypd_hash, untypd_equal, NULL, sizeof(UnresolvedTypeDef));

    ctx->unresolved = untypds;

    // Get the unresolved type definitions in the map and report duplicates
    for (int i = 0; i < items->len; i++) {
        if (items->data[i].tag != ATStruct && items->data[i].tag != ATTypeDecl) {
            continue;
        }

        UnresolvedTypeDef td;
        if (items->data[i].tag == ATTypeDecl) {
            AstTypeDecl t = items->data[i].type_decl;
            td.type = string_slice_from_token(t.name);
            td.span = t.span;
            td.name_span = t.name.span;
            td.value.type = t.value;
        } else {
            AstStruct s = items->data[i].struct_;
            td.type = string_slice_from_token(s.ident);
            td.span = s.span;
            td.name_span = s.ident.span;
            td.value.struct_ = s;

            if (s.fields.len == 0) {
                vec_push(&ctx->errors, err_empty(s.ident.span, ATStruct, td.type));
            }
        }

        UnresolvedTypeDef *original = hashmap_get(untypds, &td);
        if (original != NULL) {
            vec_push(&ctx->errors, err_duplicate_def(original->name_span, td.name_span, ATIdent, original->type));
            vec_take(items, i);
            i--;
            // Update value to last definition
            hashmap_set(untypds, &td);
        } else {
            hashmap_set(untypds, &td);
        }
    }

    // Check for type declarations cycles / and resolve type declarations (give them a value)
    for (int i = 0; i < items->len; i++) {
        if (items->data[i].tag != ATTypeDecl) {
            continue;
        }

        hashmap_clear(ctx->names);

        AstTypeDecl td = items->data[i].type_decl;
        StringSlice name = string_slice_from_token(td.name);
        hashmap_set(ctx->names, &name);
        bool valid = true;
        AstType value = td.value;

        SpanVec spans = vec_init();
        StringSliceVec idents = vec_init();
        vec_push(&spans, td.span);
        vec_push(&idents, name);
        while (true) {
            // Skip indirections
            while (value.tag == ATFieldArray || value.tag == ATHeapArray) {
                value = *(AstType *)value.array.type;
            }
            // Value is now an AstIdent.
            SpannedStringSlice next = sss_from_token(value.ident.token);

            if (hashmap_set(ctx->names, &next.slice)) {
                // We evaluate to a type we've already visited: cycle

                size_t index;
                // Loop over idents (members of the cycle), set them as invalid and find the index
                // of the first member of the cycle, the members before aren't actually part of it:
                // A = B, B = C, C = B, A isn't part of the cycle (B <-> C) and shouldn't be reported
                // (but is invalid)
                for (size_t i = 0; i < idents.len; i++) {
                    if (string_slice_equal(&idents.data[i], &next.slice)) {
                        index = i;
                    }

                    hashmap_set(ctx->typedefs, &(TypeDef){.name = idents.data[i], .value = NULL});
                }

                vec_splice(&spans, 0, index);
                vec_splice(&idents, 0, index);

                EvalErrorCycle cycle;
                cycle.tag = EETCycle;
                cycle.type = ATTypeDecl;
                cycle.spans = spans;
                cycle.idents = idents;
                // reinitialize the vectors to be dropped at the end.
                // vec_init doesn't do any allocation so this is free
                spans = (SpanVec)vec_init();
                idents = (StringSliceVec)vec_init();

                EvalError err = {.cycle = cycle};

                vec_push(&ctx->errors, err);
                break;
            }

            TypeDef *resolved = hashmap_get(ctx->typedefs, &(TypeDef){.name = next.slice});
            if (resolved != NULL) {
                // The type declaration evaluates to a resolved type (a primitif type, or an invalid type)
                if (resolved->value == NULL) {
                    // the type it evaluates to is invalid, so it is too
                    valid = false;
                    break;
                }
                // The type it evaluates to is valid: the type declaration doesn't contain any cycle
                break;
            }

            UnresolvedTypeDef *unr = hashmap_get(untypds, &(UnresolvedTypeDef){.type = next.slice});
            if (unr == NULL) { // The type evaluates to an unknown identifier
                // Report error and set as invalid
                vec_push(&ctx->errors, err_unknown(next.span, ATIdent, next.slice));
                valid = false;
                break;
            }

            if (unr->value.tag == ATStruct) {
                // The type declarations evaluates to an (unresolved) struct: it can't cycle
                break;
            } else {
                // The type declarations evaluates to another type declarations: we continue checking
                vec_push(&spans, unr->span);
                vec_push(&idents, next.slice);
                value = unr->value.type;
            }
        }

        vec_drop(spans);
        vec_drop(idents);

        if (!valid) {
            // Set invalid
            hashmap_set(ctx->typedefs, &(TypeDef){.name = name, .value = NULL});
        }
    }

    hashmap_clear(ctx->names);

    // Resolves types (this accepts recursive types)
    for (int i = 0; i < items->len; i++) {
        if (items->data[i].tag != ATStruct && items->data[i].tag != ATTypeDecl) {
            continue;
        }

        SpannedStringSlice name;
        if (items->data[i].tag == ATStruct) {
            name = sss_from_token(items->data[i].struct_.ident);
        } else {
            name = sss_from_token(items->data[i].type_decl.name);
        }

        resolve_type(ctx, name);
    }

    Hashmap *checked = hashmap_init(pointer_hash, pointer_equal, NULL, sizeof(StructObject *));
    Hashmap *invalids = hashmap_init(pointer_hash, pointer_equal, NULL, sizeof(StructObject *));
    // Check for recursive types without indirections (infinite size)
    for (int i = 0; i < items->len; i++) {
        // TypeDecl can't be recursive
        if (items->data[i].tag != ATStruct) {
            continue;
        }

        TypeDef *td = hashmap_get(ctx->typedefs, &(TypeDef){.name = string_slice_from_token(items->data[i].struct_.ident)});
        TypeObject *start = td->value;
        StructObject *str = (StructObject *)&start->type.struct_;

        EvalErrorInfiniteStruct err = {.tag = EETInfiniteStruct, .fields = vec_init(), .structs = vec_init()};
        if (check_for_recursion(ctx, &err, checked, invalids, start, str)) {
            EvalError e = {.infs = err};
            vec_push(&ctx->errors, e);
        };
        hashmap_clear(checked);
    }

    // Check structs for duplicate fields
    Hashmap *names = hashmap_init(sss_hash, sss_equal, NULL, sizeof(SpannedStringSlice));
    for (int i = 0; i < items->len; i++) {
        if (items->data[i].tag != ATStruct) {
            continue;
        }

        TypeDef *td = hashmap_get(ctx->typedefs, &(TypeDef){.name = string_slice_from_token(items->data[i].struct_.ident)});
        StructObject *str = (StructObject *)&td->value->type.struct_;
        for (size_t i = 0; i < str->fields.len; i++) {
            Field f = str->fields.data[i];
            SpannedStringSlice *prev = hashmap_get(names, &(SpannedStringSlice){.slice = f.name});
            if (prev != NULL) {
                vec_push(&ctx->errors, err_duplicate_def(prev->span, f.name_span, ATField, f.name));
                continue;
            }
            hashmap_set(names, &(SpannedStringSlice){.slice = f.name, .span = f.name_span});
        }
        hashmap_clear(names);
    }
    hashmap_drop(names);

    hashmap_drop(checked);
    hashmap_drop(invalids);
    hashmap_drop(untypds);
    ctx->unresolved = NULL;
}

static void resolve_constants(EvaluationContext *ctx) {
    AstItemVec *items = ctx->items;
    Hashmap *unconsts = hashmap_init(unconst_hash, unconst_equal, NULL, sizeof(UnresolvedConstant));
    Hashmap *names = ctx->names;
    Hashmap *constants = ctx->constants;

    // Load unresolved constants into map (and check for duplicates)
    for (int i = 0; i < items->len; i++) {
        if (items->data[i].tag != ATConstant) {
            continue;
        }

        AstConstant c = items->data[i].constant;
        UnresolvedConstant constant =
            {.constant = string_slice_from_token(c.name), .name_span = c.name.span, .span = c.span, .value = c.value.token};
        UnresolvedConstant *original = hashmap_get(unconsts, &constant);

        if (original != NULL) {
            vec_push(&ctx->errors, err_duplicate_def(original->name_span, constant.name_span, ATConstant, original->constant));
            vec_take(items, i);
            i--;
            // Update value to last
            hashmap_set(unconsts, &constant);
        } else {
            hashmap_set(unconsts, &constant);
        }
    }

    for (size_t i = 0; i < items->len; i++) {
        if (items->data[i].tag != ATConstant) {
            continue;
        }

        UnresolvedConstant *unc =
            hashmap_get(unconsts, &(UnresolvedConstant){.constant = string_slice_from_token(items->data[i].constant.name)});
        hashmap_clear(names);
        hashmap_set(names, &unc->constant);
        Token value = unc->value;
        while (value.type == Ident) {
            StringSlice ident = string_slice_from_token(value);
            Constant *resolved = hashmap_get(constants, &(Constant){.name = ident});
            // If the constant is set to another that is already resolved
            if (resolved != NULL) {
                if (!resolved->valid) {
                    // If the constant is invalid, break here, we know we won't be resolving this
                    break;
                }
                // We expect a token out of this loop, but we don't have one here, so we make one up that works
                // only value.lit and value.type are read
                value.type = Number;
                value.lit = resolved->value;
                break;
            }

            if (hashmap_has(names, &ident)) { // Cycle detected on ident
                EvalErrorCycle cycle;
                cycle.tag = EETCycle;
                cycle.type = ATConstant;
                cycle.spans = (SpanVec)vec_init();
                cycle.idents = (StringSliceVec)vec_init();

                // Walk the cycle again, keeping track of the spans, and marking every member
                // as invalid
                UnresolvedConstant *start = hashmap_get(unconsts, &(UnresolvedConstant){.constant = ident});
                UnresolvedConstant *cur = start;
                do {
                    vec_push(&cycle.spans, cur->span);
                    vec_push(&cycle.idents, cur->constant);
                    hashmap_set(constants, &(Constant){.name = cur->constant, .value = 0, .valid = false});
                    cur = hashmap_get(unconsts, &(UnresolvedConstant){.constant = string_slice_from_token(cur->value)});
                } while (cur != start);

                EvalError err = {.cycle = cycle};

                vec_push(&ctx->errors, err);
                break;
            }

            // Get the constant the current is set to
            UnresolvedConstant *c = hashmap_get(unconsts, &(UnresolvedConstant){.constant = ident});
            if (c == NULL) { // Constant doesn't exist
                // throw error and mark invalid
                vec_push(&ctx->errors, err_unknown(unc->value.span, ATConstant, ident));
                break;
            }

            hashmap_set(names, &ident);
            value = c->value;
        }

        if (value.type == Ident) { // Constant couldn't be resolved
            hashmap_set(constants, &(Constant){.name = unc->constant, .value = 0, .valid = false});
        } else {
            hashmap_set(constants, &(Constant){.name = unc->constant, .value = value.lit, .valid = true});
        }
    }

    hashmap_drop(unconsts);
    hashmap_clear(names);
}

static void resolve_messages(EvaluationContext *ctx) {
    AstItemVec *items = ctx->items;
    Hashmap *names = hashmap_init(sss_hash, sss_equal, NULL, sizeof(SpannedStringSlice));
    Hashmap *field_names = hashmap_init(sss_hash, sss_equal, NULL, sizeof(SpannedStringSlice));

    ctx->messages = (MessagesObjectVec)vec_init();
    uint64_t version = ~0;
    for (size_t i = 0; i < items->len; i++) {
        if (items->data[i].tag == ATVersion) {
            AstVersion v = items->data[i].version;
            version = get_ast_number_value(ctx, v.version);
            continue;
        }
        if (items->data[i].tag != ATMessages) {
            continue;
        }
        AstMessages m = items->data[i].messages;
        SpannedStringSlice name = sss_from_token(m.name);
        Attributes attrs = AttrNone;

        MessagesObject res;
        res.name = name.slice;
        res.messages = (MessageObjectVec)vec_init();
        res.version = version;

        SpannedStringSlice *prev_name = hashmap_get(names, &name);
        if (prev_name != NULL) {
            vec_push(&ctx->errors, err_duplicate_def(prev_name->span, name.span, ATIdent, name.slice));
        } else {
            hashmap_set(names, &name);
        }

        for (size_t j = 0; j < m.children.len; j++) {
            if (m.children.data[j].tag == ATAttribute) {
                AstAttribute attr = m.children.data[j].attribute;
                const char *a = attr.ident.lexeme;
                uint32_t len = attr.ident.span.len;
#define _case(x) \
    if (strncmp(#x, a, sizeof(#x) - 1 > len ? sizeof(#x) - 1 : len) == 0) { \
        attrs |= Attr_##x; \
        continue; \
    }
                _case(versioned);

                // If we get to here none of the above matched
                vec_push(&ctx->errors, err_unknown(attr.ident.span, ATAttribute, string_slice_from_token(attr.ident)));
#undef _case
            } else {
                AstMessage msg = m.children.data[j].message;

                SpannedStringSlice name = sss_from_token(msg.ident);

                SpannedStringSlice *prev_name = hashmap_get(names, &name);
                if (prev_name != NULL) {
                    vec_push(&ctx->errors, err_duplicate_def(prev_name->span, name.span, ATIdent, name.slice));
                } else {
                    hashmap_set(names, &name);
                }

                MessageObject message;
                message.name = name.slice;
                message.attributes = attrs;
                message.fields = (FieldVec)vec_init();
                vec_grow(&message.fields, msg.fields.len);

                for (size_t k = 0; k < msg.fields.len; k++) {
                    Field f;
                    f.name = string_slice_from_token(msg.fields.data[k].name);
                    f.name_span = msg.fields.data[k].name.span;
                    f.type = ast_type_to_type_obj(ctx, msg.fields.data[k].type);
                    vec_push(&message.fields, f);

                    SpannedStringSlice *prev = hashmap_get(field_names, &(SpannedStringSlice){.slice = f.name});
                    if (prev != NULL) {
                        vec_push(&ctx->errors, err_duplicate_def(prev->span, f.name_span, ATField, f.name));
                        continue;
                    }
                    hashmap_set(field_names, &(SpannedStringSlice){.slice = f.name, .span = f.name_span});
                }

                hashmap_clear(field_names);

                vec_push(&res.messages, message);

                // Reset attributes after a message
                attrs = AttrNone;
            }
        }

        vec_push(&ctx->messages, res);
        version = ~0;
    }

    hashmap_drop(names);
    hashmap_drop(field_names);
}

void resolve_additional_type_info(EvaluationContext *ctx) {
    // Resolve alignment of all living type objects
    Hashmap *seen = hashmap_init(pointer_hash, pointer_equal, NULL, sizeof(TypeObject *));
    for (size_t i = 0; i < ctx->type_objects.len; i++) {
        ((TypeObject *)ctx->type_objects.data[i])->align = resolve_alignment(ctx->type_objects.data[i], seen);
        hashmap_clear(seen);
    }

    // Compute type layouts
    Hashmap *layouts = hashmap_init(layout_hash, layout_equal, layout_drop, sizeof(Layout));
    for (size_t i = 0; i < ctx->type_objects.len; i++) {
        Layout l = type_layout(ctx->type_objects.data[i]);
        hashmap_set(layouts, &l);
    }
#define _case(x) \
    { \
        Layout l = type_layout((TypeObject *)&PRIMITIF_##x); \
        hashmap_set(layouts, &l); \
    }
    _case(u8);
    _case(u16);
    _case(u32);
    _case(u64);
    _case(i8);
    _case(i16);
    _case(i32);
    _case(i64);
    _case(char);
    _case(bool);
#undef _case

    ctx->layouts = layouts;

    hashmap_drop(seen);
}

void program_drop(Program p) {
    for (size_t i = 0; i < p.type_objects.len; i++) {
        TypeObject *ptr = p.type_objects.data[i];
        if (ptr->kind == TypeStruct) {
            StructObject *str = (StructObject *)&ptr->type.struct_;
            vec_drop(str->fields);
        }
        free(ptr);
    }
    vec_drop(p.type_objects);

    hashmap_drop(p.typedefs);
    hashmap_drop(p.layouts);
    vec_drop(p.messages);
}

// Resolve statics of an AST (constants and type declarations);
EvaluationResult resolve_statics(AstContext *ctx) {
    EvaluationContext ectx;
    // resolved constants: value is a number, and the constant may be invalid
    ectx.constants = hashmap_init(const_hash, const_equal, NULL, sizeof(Constant));
    ectx.typedefs = hashmap_init(typedef_hash, typedef_equal, NULL, sizeof(TypeDef));

    // Set of names used to check for cycles
    ectx.names = hashmap_init(string_slice_hash, string_slice_equal, NULL, sizeof(StringSlice));
    ectx.unresolved = NULL;
    ectx.items = &ctx->root->items.items;
    ectx.errors = (EvalErrorVec)vec_init();
    ectx.type_objects = (PointerVec)vec_init();

    {
#define add_prim(type_name, type_size) \
    do { \
        hashmap_set( \
            ectx.typedefs, \
            &(TypeDef){.name.ptr = #type_name, .name.len = sizeof(#type_name) - 1, .value = (TypeObject *)&PRIMITIF_##type_name} \
        ); \
    } while (0)
        add_prim(u8, 1);
        add_prim(u16, 2);
        add_prim(u32, 4);
        add_prim(u64, 8);
        add_prim(i8, 1);
        add_prim(i16, 2);
        add_prim(i32, 4);
        add_prim(i64, 8);
        add_prim(f32, 4);
        add_prim(f64, 8);
        add_prim(char, 1);
        add_prim(bool, 1);
#undef add_prim
    }

    resolve_constants(&ectx);
    resolve_types(&ectx);
    resolve_messages(&ectx);
    resolve_additional_type_info(&ectx);

    hashmap_drop(ectx.names);
    hashmap_drop(ectx.constants);

    Program p;
    p.typedefs = ectx.typedefs;
    p.layouts = ectx.layouts;
    p.type_objects = ectx.type_objects;
    p.messages = ectx.messages;

    return (EvaluationResult){.program = p, .errors = ectx.errors};
}
