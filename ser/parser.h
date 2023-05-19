#ifndef PARSER_H
#define PARSER_H
#include "ast.h"
#include "lexer.h"
#include "source.h"
#include "vector.h"
#include "vector_impl.h"

typedef union {
    TokenType type;
} ParsingErrorData;

typedef enum {
    ParsingErrorNoError,
    ParsingErrorUnexpectedToken,
} ParsingErrorType;

typedef struct {
    Span span;
    ParsingErrorType type;
    ParsingErrorData data;
} ParsingError;

VECTOR_IMPL(ParsingError, ParsingErrorVec, parsing_error);

typedef struct {
    AstContext ctx;
    ParsingErrorVec errors;
} ParsingResult;

ParsingResult parse(TokenVec vec);

void parsing_error_report(Source *src, ParsingError *err);

#endif
