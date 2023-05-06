#ifndef LEXER_H
#define LEXER_H
#include "source.h"
#include "vector_impl.h"

#include <stdint.h>

typedef enum : uint32_t {
    LeftParen = 1 << 0,
    RightParen = 1 << 1,
    LeftBrace = 1 << 2,
    RightBrace = 1 << 3,
    LeftBracket = 1 << 4,
    RightBracket = 1 << 5,
    Comma = 1 << 6,
    Semicolon = 1 << 7,
    Ampersand = 1 << 8,
    Caret = 1 << 9,
    Colon = 1 << 10,
    Equal = 1 << 11,
    Hash = 1 << 12,
    Ident = 1 << 13,
    Number = 1 << 14,
    Messages = 1 << 15,
    Struct = 1 << 16,
    Version = 1 << 17,
    Const = 1 << 18,
    Type = 1 << 19,
    Eof = 1 << 20,
} TokenType;

#define TOKEN_TYPE_COUNT 21

typedef struct {
    // The type of the token
    TokenType type;
    // Span of the lexeme (line, columnn, offset, length)
    Span span;
    // A pointer to the start of the lexeme (not null terminated)
    const char *lexeme;
    // Pointer to the source object
    Source *src;
    // In the case of a Number token: the parsed number
    uint64_t lit;
} Token;

typedef enum : uint32_t {
    LexingErrorNoError,
    LexingErrorUnexpectedCharacter,
    LexingErrorNumberLiteralOverflow,
} LexingErrorType;

typedef struct {
    Source *src;
    Span span;
    LexingErrorType type;
} LexingError;
// Destroy the token
void token_drop(Token t);
// Destroy lexing error
void lexing_error_drop(LexingError e);

VECTOR_IMPL(Token, TokenVec, token, token_drop);
VECTOR_IMPL(LexingError, LexingErrorVec, lexing_error, lexing_error_drop);

typedef struct {
    TokenVec tokens;
    LexingErrorVec errors;
} LexingResult;

LexingResult lex(Source *src);

void lexing_result_drop(LexingResult res);

void lexing_error_report(LexingError *le);

__attribute__((unused)) static inline const char *token_type_name(TokenType t) {
#define _case(type) \
    case type: \
        return #type
    switch (t) {
        _case(LeftParen);
        _case(RightParen);
        _case(LeftBrace);
        _case(RightBrace);
        _case(LeftBracket);
        _case(RightBracket);
        _case(Comma);
        _case(Semicolon);
        _case(Ampersand);
        _case(Caret);
        _case(Colon);
        _case(Equal);
        _case(Hash);
        _case(Ident);
        _case(Number);
        _case(Messages);
        _case(Struct);
        _case(Version);
        _case(Const);
        _case(Type);
        _case(Eof);
    }
#undef _case
}
char *token_type_string(TokenType t);
#endif
