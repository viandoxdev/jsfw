#include "parser.h"

#include "ast.h"
#include "lexer.h"
#include "vector.h"

#include <stdarg.h>
#include <stdbool.h>

typedef struct {
    TokenVec tokens;
    ParsingErrorVec errors;
    AstContext ctx;
    uint32_t current;
} Parser;

static Parser parser_init(TokenVec tokens) {
    return (Parser){
        .tokens = tokens,
        .ctx = ast_init(),
        .current = 0,
        .errors = vec_init(),
    };
}

inline static ParsingError err_expected(TokenType type, Span span) {
    return (ParsingError){.span = span, .type = ParsingErrorUnexpectedToken, .data.type = type};
}

inline static void add_error(Parser *p, ParsingError err) { vec_push(&p->errors, err); }

inline static Token peek(Parser *p) { return p->tokens.data[p->current]; }

inline static Token previous(Parser *p) { return p->tokens.data[p->current - 1]; }

static bool check(Parser *p, TokenType type) {
    if (peek(p).type == Eof) {
        return type == Eof;
    }
    return peek(p).type == type;
}

static Token advance(Parser *p) {
    if (peek(p).type != Eof)
        p->current++;
    return previous(p);
}

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) {
        advance(p);
        return true;
    }
    return false;
}

static void skip_until(Parser *p, TokenType type) {
    while ((peek(p).type & (type | Eof)) == 0) {
        advance(p);
    }
}

static bool consume(Parser *p, TokenType t, Token *res) {
    if (peek(p).type == t) {
        if (res != NULL) {
            *res = advance(p);
        } else {
            advance(p);
        }
        return true;
    }
    add_error(p, err_expected(t, peek(p).span));
    return false;
}

#define bubble(...) \
    if (!(__VA_ARGS__)) { \
        return false; \
    }

static Location parser_loc(Parser *p) { return p->tokens.data[p->current].span.loc; }

static inline Span span_end(Parser *p, Location start) {
    Span prev = previous(p).span;
    return span_from_to(
        start,
        (Location){.line = prev.loc.line, .column = prev.loc.column + prev.len, .offset = prev.loc.offset + prev.len}
    );
}

static bool parse_number(Parser *p, AstNumber *res) {
    Token t = advance(p);
    if (t.type == Number) {
        *res = ast_number(p->ctx, t.span, t);
        return true;
    }
    if (t.type == Ident) {
        *res = ast_number(p->ctx, t.span, t);
        return true;
    }
    add_error(p, err_expected(Number | Ident, t.span));
    return false;
}

static bool parse_ident(Parser *p, AstIdent *res) {
    Token t = advance(p);
    if (t.type == Ident) {
        *res = ast_ident(p->ctx, t.span, t);
        return true;
    }
    add_error(p, err_expected(Ident, t.span));
    return false;
}

static bool parse_size(Parser *p, AstSize *res) {
    Location start = parser_loc(p);
    if (check(p, RightBracket)) {
        *res = ast_no_size(p->ctx, span_end(p, start));
        return true;
    }
    AstNumber size;
    if (match(p, Caret)) {
        bubble(parse_number(p, &size));
        *res = ast_max_size(p->ctx, span_end(p, start), size);
        return true;
    }
    bubble(parse_number(p, &size));
    *res = ast_fixed_size(p->ctx, span_end(p, start), size);
    return true;
}

static bool parse_type(Parser *p, AstType *res) {
    bubble(parse_ident(p, &res->ident));

    Location start = parser_loc(p);
    TokenType next = peek(p).type;
    while (next == Ampersand || next == LeftBracket) {
        AstType *type = arena_alloc(&p->ctx.alloc, sizeof(AstType));
        *type = *res;
        AstSize size;
        bool heap = match(p, Ampersand);
        bubble(consume(p, LeftBracket, NULL));
        bubble(parse_size(p, &size));
        bubble(consume(p, RightBracket, NULL));
        if (heap || size.tag == ATNoSize) {
            res->array = ast_heap_array(p->ctx, span_end(p, start), type, size);
        } else {
            res->array = ast_field_array(p->ctx, span_end(p, start), type, size);
        }
        next = peek(p).type;
    }
    return true;
}

static bool parse_field(Parser *p, AstField *res) {
    Token name;
    AstType type;
    Location start = parser_loc(p);
    bubble(consume(p, Ident, &name));
    bubble(consume(p, Colon, NULL));
    bubble(parse_type(p, &type));
    *res = ast_field(p->ctx, span_end(p, start), name, type);
    return true;
}

static bool parse_message(Parser *p, AstMessage *res) {
    Token name;
    AstFieldVec fields = vec_init();
    Location start = parser_loc(p);
    bubble(consume(p, Ident, &name));
    bubble(consume(p, LeftBrace, NULL));

    AstField f;
    do {
        if (check(p, RightBrace)) {
            break;
        }
        if (parse_field(p, &f)) {
            vec_push(&fields, f);
        } else {
            skip_until(p, Comma | Ident | RightBrace);
        }
    } while (match(p, Comma));
    bubble(consume(p, RightBrace, NULL));
    *res = ast_message(p->ctx, span_end(p, start), name, fields);
    return true;
}

static bool parse_attribute(Parser *p, AstAttribute *res) {
    Token ident;
    Location start = parser_loc(p);
    bubble(consume(p, Hash, NULL));
    bubble(consume(p, LeftBracket, NULL));
    bubble(consume(p, Ident, &ident));
    bubble(consume(p, RightBracket, NULL));
    *res = ast_attribute(p->ctx, span_end(p, start), ident);
    return true;
}

static bool parse_attribute_or_message(Parser *p, AstAttributeOrMessage *res) {
    if (check(p, Hash)) {
        return parse_attribute(p, &res->attribute);
    } else if (check(p, Ident)) {
        return parse_message(p, &res->message);
    } else {
        vec_push(&p->errors, err_expected(Hash | Ident, peek(p).span));
        return false;
    }
}

static bool parse_version(Parser *p, AstVersion *res) {
    AstNumber ver;
    Location start = parser_loc(p);
    bubble(consume(p, Version, NULL));
    bubble(consume(p, LeftParen, NULL));
    bubble(parse_number(p, &ver));
    bubble(consume(p, RightParen, NULL));
    bubble(consume(p, Semicolon, NULL));
    *res = ast_version(p->ctx, span_end(p, start), ver);
    return true;
}

static bool parse_struct(Parser *p, AstStruct *res) {
    Token name;
    AstFieldVec fields = vec_init();
    Location start = parser_loc(p);
    bubble(consume(p, Struct, NULL));
    bubble(consume(p, Ident, &name));
    bubble(consume(p, LeftBrace, NULL));

    AstField f;
    do {
        if (check(p, RightBrace)) {
            break;
        }
        if (parse_field(p, &f)) {
            vec_push(&fields, f);
        } else {
            skip_until(p, Comma | Ident | RightBrace);
        }
    } while (match(p, Comma));
    bubble(consume(p, RightBrace, NULL));
    *res = ast_struct(p->ctx, span_end(p, start), name, fields);
    return true;
}

static bool parse_type_decl(Parser *p, AstTypeDecl *res) {
    Token name;
    AstType type;
    Location start = parser_loc(p);
    bubble(consume(p, Type, NULL));
    bubble(consume(p, Ident, &name));
    bubble(consume(p, Equal, NULL));
    bubble(parse_type(p, &type));
    bubble(consume(p, Semicolon, NULL));
    *res = ast_type_decl(p->ctx, span_end(p, start), name, type);
    return true;
}

static bool parse_messages(Parser *p, AstMessages *res) {
    AstAttributeOrMessageVec children = vec_init();
    AstAttributeOrMessage child;
    Token name;
    Location start = parser_loc(p);
    bubble(consume(p, Messages, NULL));
    bubble(consume(p, Ident, &name));
    bubble(consume(p, LeftBrace, NULL));
    while (!match(p, RightBrace)) {
        if (parse_attribute_or_message(p, &child)) {
            vec_push(&children, child);
        } else {
            skip_until(p, RightBrace | Hash | Ident);
        }
    }
    *res = ast_messages(p->ctx, span_end(p, start), name, children);
    return true;
}

static bool parse_constant(Parser *p, AstConstant *res) {
    Token name;
    AstNumber value;
    Location start = parser_loc(p);
    bubble(consume(p, Const, NULL));
    bubble(consume(p, Ident, &name));
    bubble(consume(p, Equal, NULL));
    bubble(parse_number(p, &value));
    bubble(consume(p, Semicolon, NULL));
    *res = ast_constant(p->ctx, span_end(p, start), name, value);
    return true;
}

static bool parse_item(Parser *p, AstItem *res) {
    switch (peek(p).type) {
    case Version:
        return parse_version(p, &res->version);
    case Struct:
        return parse_struct(p, &res->struct_);
    case Type:
        return parse_type_decl(p, &res->type_decl);
    case Messages:
        return parse_messages(p, &res->messages);
    case Const:
        return parse_constant(p, &res->constant);
    default:
        // TODO: error handling
        advance(p);
        return false;
    }
}

static bool parse_items(Parser *p, AstItems *res) {
    AstItemVec items = vec_init();
    AstItem item;
    Location start = parser_loc(p);
    while (!check(p, Eof)) {
        if (parse_item(p, &item)) {
            vec_push(&items, item);
        } else {
            skip_until(p, Version | Struct | Type | Messages | Const);
        }
    }
    *res = ast_items(p->ctx, span_end(p, start), items);
    return true;
}

ParsingResult parse(TokenVec vec) {
    Parser p = parser_init(vec);
    AstNode *items = arena_alloc(&p.ctx.alloc, sizeof(AstNode));
    parse_items(&p, &items->items);
    p.ctx.root = items;
    return (ParsingResult){.ctx = p.ctx, .errors = p.errors};
}

void parsing_error_report(Source *src, ParsingError *err) {
    ReportSpan span = {.span = err->span, .sev = ReportSeverityError};
#define report(fmt, ...) source_report(src, err->span.loc, ReportSeverityError, &span, 1, NULL, fmt, __VA_ARGS__);
    switch (err->type) {
    case ParsingErrorUnexpectedToken: {
        char *type = token_type_string(err->data.type);
        span.message = msprintf("expected %s", type);
        report("Expected %s, found '%.*s'", type, err->span.len, &src->str[err->span.loc.offset]);
        free((char *)span.message);
        free(type);
        break;
    }
    default:
        break;
    }
#undef report
}
