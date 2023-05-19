#include "lexer.h"

#include "vector.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    uint32_t start;
    uint32_t current;
    Source *src;
    Location loc;
    Location start_loc;
    TokenVec tokens;
    LexingErrorVec errors;
} Lexer;

static inline __attribute__((always_inline)) Token
token(Source *src, TokenType type, const char *lexeme, uint32_t len, uint64_t lit, Location loc) {
    IF_DEBUG(src->ref_count++);
    return (Token){
        .src = src,
        .lit = lit,
        .span.loc = loc,
        .span.len = len,
        .type = type,
        .lexeme = lexeme,
    };
}
static inline __attribute__((always_inline)) LexingError
lexing_error(Source *src, LexingErrorType type, Location loc, uint32_t len) {
    IF_DEBUG(src->ref_count++);
    return (LexingError){
        .src = src,
        .type = type,
        .span.loc = loc,
        .span.len = len,
    };
}

void token_drop(Token t) { IF_DEBUG(t.src->ref_count--); }

void lexing_error_drop(LexingError e) { IF_DEBUG(e.src->ref_count--); }

void lexing_result_drop(LexingResult res) {
    vec_drop(res.tokens);
    vec_drop(res.errors);
}

static Lexer lexer_init(Source *src) {
    TokenVec tokens = vec_init();
    vec_grow(&tokens, 256);
    return (Lexer){
        .start = 0,
        .current = 0,
        .src = src,
        .loc = location(1, 0, 0),
        .start_loc = location(1, 1, 0),
        .tokens = tokens,
        .errors = vec_init(),
    };
}

static void lexer_add_token(Lexer *lex, TokenType type, uint32_t len, uint64_t lit) {
    vec_push(&lex->tokens, token(lex->src, type, &lex->src->str[lex->start], len, lit, lex->start_loc));
}

static void lexer_add_error(Lexer *lex, LexingErrorType type, uint32_t len) {
    vec_push(&lex->errors, lexing_error(lex->src, type, lex->start_loc, len));
}

static char lexer_advance(Lexer *lex) {
    char c = lex->src->str[lex->current++];
    lex->loc.offset = lex->current;
    lex->loc.column++;
    if (c == '\n') {
        lex->loc.line++;
        lex->loc.column = 0;
    }
    return c;
}

static bool lexer_match(Lexer *lex, char exp) {
    if (lex->current >= lex->src->len)
        return false;
    if (lex->src->str[lex->current] != exp)
        return false;
    lexer_advance(lex);
    return true;
}

static bool lexer_match_not(Lexer *lex, char unexp) {
    if (lex->current >= lex->src->len)
        return false;
    if (lex->src->str[lex->current] == unexp)
        return false;
    lexer_advance(lex);
    return true;
}

static char lexer_peek(Lexer *lex) { return lex->src->str[lex->current]; }

inline static bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline static uint64_t to_digit(char c) { return c - '0'; }
inline static bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline static bool is_ident(char c) { return is_ident_start(c) || is_digit(c) || c == '_'; }

static void lexer_scan_number(Lexer *lex) {
    // Get first digit (the one we already passed)
    uint64_t lit = to_digit(lex->src->str[lex->start]);
    uint32_t len = 1;
    char c = lexer_peek(lex);
    bool overflow = false;
    while (is_digit(c)) {
        uint64_t nlit = lit * 10 + to_digit(c);
        if (nlit < lit) { // overflow
            overflow = true;
        }
        lit = nlit;
        lexer_advance(lex);
        c = lexer_peek(lex);
        len++;
    }

    if (overflow) {
        lexer_add_error(lex, LexingErrorNumberLiteralOverflow, len);
    }

    lexer_add_token(lex, Number, len, lit);
}

static uint32_t u32max(uint32_t a, uint32_t b) { return a > b ? a : b; }

static inline __attribute__((always_inline)) void lexer_scan_ident(Lexer *lex) {
    uint32_t len = 1;
    while (is_ident(lexer_peek(lex))) {
        lexer_advance(lex);
        len++;
    }
    const char *s = &lex->src->str[lex->start];
#define handle(x, str) else if (strncmp(str, s, u32max(sizeof(str) - 1, len)) == 0) lexer_add_token(lex, x, len, 0)
    if (false)
        ;
    handle(Messages, "messages");
    handle(Struct, "struct");
    handle(Version, "version");
    handle(Const, "const");
    handle(Type, "type");
    else lexer_add_token(lex, Ident, len, 0);
#undef handle
}

static void lexer_scan(Lexer *lex) {
    char c = lexer_advance(lex);
    switch (c) {
    case '(':
        lexer_add_token(lex, LeftParen, 1, 0);
        break;
    case ')':
        lexer_add_token(lex, RightParen, 1, 0);
        break;
    case '{':
        lexer_add_token(lex, LeftBrace, 1, 0);
        break;
    case '}':
        lexer_add_token(lex, RightBrace, 1, 0);
        break;
    case '[':
        lexer_add_token(lex, LeftBracket, 1, 0);
        break;
    case ']':
        lexer_add_token(lex, RightBracket, 1, 0);
        break;
    case ',':
        lexer_add_token(lex, Comma, 1, 0);
        break;
    case ';':
        lexer_add_token(lex, Semicolon, 1, 0);
        break;
    case '&':
        lexer_add_token(lex, Ampersand, 1, 0);
        break;
    case '^':
        lexer_add_token(lex, Caret, 1, 0);
        break;
    case ':':
        lexer_add_token(lex, Colon, 1, 0);
        break;
    case '=':
        lexer_add_token(lex, Equal, 1, 0);
        break;
    case '#':
        lexer_add_token(lex, Hash, 1, 0);
    case '/':
        if (lexer_match(lex, '/')) {
            while (lexer_match_not(lex, '\n'))
                ;
        }
        break;
    case ' ':
    case '\t':
    case '\n':
    case '\r':
        break;
    default:
        if (is_digit(c)) {
            lexer_scan_number(lex);
        } else if (is_ident_start(c)) {
            lexer_scan_ident(lex);
        } else {
            // Try to merge with the last error if possible
            if (lex->errors.len > 0) {
                LexingError *last_err = &lex->errors.data[lex->errors.len - 1];
                if (last_err->span.loc.line == lex->loc.line && last_err->type == LexingErrorUnexpectedCharacter &&
                    last_err->span.loc.column + last_err->span.len == lex->start_loc.column) {
                    last_err->span.len++;
                    break;
                }
            }
            lexer_add_error(lex, LexingErrorUnexpectedCharacter, 1);
        }
    }
}

static void lexer_lex(Lexer *lex) {
    while (lex->current < lex->src->len) {
        lex->start = lex->current;
        lex->start_loc = lex->loc;
        lexer_scan(lex);
    }

    lex->start = lex->current;
    lex->start_loc = lex->loc;

    lexer_add_token(lex, Eof, 0, 0);
}

static LexingResult lexer_finish(Lexer lex) {
    return (LexingResult){
        .errors = lex.errors,
        .tokens = lex.tokens,
    };
}

LexingResult lex(Source *src) {
    Lexer lex = lexer_init(src);
    lexer_lex(&lex);
    return lexer_finish(lex);
}

void lexing_error_report(LexingError *le) {
    ReportSpan span = {.span = le->span, .sev = ReportSeverityError};
#define report(fmt, ...) source_report(le->src, le->span.loc, ReportSeverityError, &span, 1, NULL, fmt, __VA_ARGS__);
    switch (le->type) {
    case LexingErrorUnexpectedCharacter:
        report("Unexpected character%s '%.*s'", le->span.len > 1 ? "s" : "", le->span.len, &le->src->str[le->span.loc.offset]);
        break;
    case LexingErrorNumberLiteralOverflow:
        report("number literal '%.*s' overflows max value of %lu", le->span.len, &le->src->str[le->span.loc.offset], UINT64_MAX);
        break;
    default:
        break;
    }
#undef report
}

char *token_type_string(TokenType t) {
    TokenType types[TOKEN_TYPE_COUNT];
    size_t count = 0;
#define handle(type) \
    if (t & type) \
    types[count++] = type
    handle(LeftParen);
    handle(RightParen);
    handle(LeftBrace);
    handle(RightBrace);
    handle(LeftBracket);
    handle(RightBracket);
    handle(Comma);
    handle(Semicolon);
    handle(Ampersand);
    handle(Caret);
    handle(Colon);
    handle(Equal);
    handle(Ident);
    handle(Number);
    handle(Messages);
    handle(Struct);
    handle(Version);
    handle(Const);
    handle(Type);
    handle(Eof);
#undef handle
    CharVec str = vec_init();
    for (size_t i = 0; i < count; i++) {
        if (i == 0) {
        } else if (i == count - 1) {
            vec_push_array(&str, " or ", 4);
        } else {
            vec_push_array(&str, ", ", 2);
        }

        switch (types[i]) {
        case LeftParen:
            vec_push_array(&str, "'('", 3);
            break;
        case RightParen:
            vec_push_array(&str, "')'", 3);
            break;
        case LeftBrace:
            vec_push_array(&str, "'{'", 3);
            break;
        case RightBrace:
            vec_push_array(&str, "'}'", 3);
            break;
        case LeftBracket:
            vec_push_array(&str, "'['", 3);
            break;
        case RightBracket:
            vec_push_array(&str, "']'", 3);
            break;
        case Comma:
            vec_push_array(&str, "','", 3);
            break;
        case Semicolon:
            vec_push_array(&str, "';'", 3);
            break;
        case Ampersand:
            vec_push_array(&str, "'&'", 3);
            break;
        case Caret:
            vec_push_array(&str, "'^'", 3);
            break;
        case Colon:
            vec_push_array(&str, "':'", 3);
            break;
        case Equal:
            vec_push_array(&str, "'='", 3);
            break;
        case Hash:
            vec_push_array(&str, "'#'", 3);
            break;
        case Ident:
            vec_push_array(&str, "identifier", 10);
            break;
        case Number:
            vec_push_array(&str, "number literal", 15);
            break;
        case Messages:
            vec_push_array(&str, "keyword messages", 16);
            break;
        case Struct:
            vec_push_array(&str, "keyword struct", 14);
            break;
        case Version:
            vec_push_array(&str, "keyword version", 15);
            break;
        case Const:
            vec_push_array(&str, "keyword const", 13);
            break;
        case Type:
            vec_push_array(&str, "keyword type", 12);
            break;
        case Eof:
            vec_push_array(&str, "end of file", 11);
            break;
        }
    }

    vec_push(&str, '\0');
    return str.data;
}
