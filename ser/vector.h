#ifndef VECTOR_H
#define VECTOR_H
#include "arena_allocator.h"
#include "ast.h"
#include "codegen.h"
#include "eval.h"
#include "lexer.h"
#include "parser.h"
#include "source.h"
#include "utils.h"

// This files contains the generic vector macro, which are generated according the VECTOR_IMPL_LIST

// clang-format: off
#define VECTOR_IMPL_LIST \
    (Token, TokenVec, token, token_drop), (LexingError, LexingErrorVec, lexing_error, lexing_error_drop), \
        (AstItem, AstItemVec, ast_item), (AstField, AstFieldVec, ast_field), \
        (AstAttributeOrMessage, AstAttributeOrMessageVec, ast_attribute_or_message), \
        (ArenaBlock, ArenaBlockVec, arena_block, arena_block_drop), (ParsingError, ParsingErrorVec, parsing_error), \
        (Field, FieldVec, field, field_drop), (EvalError, EvalErrorVec, eval_error), \
        (const char *, ConstStringVec, const_string), (StringSlice, StringSliceVec, string_slice), (char, CharVec, char), \
        (CharVec, CharVec2, char_vec, _vec_char_drop), (ReportSpan, ReportSpanVec, report_span), \
        (StyledString, StyledStringVec, styled_string, styled_string_drop), (Span, SpanVec, span), \
        (void *, PointerVec, pointer), (SpannedStringSlice, SpannedStringSliceVec, spanned_string_slice), \
        (MessageObject, MessageObjectVec, message_object, message_drop), \
        (MessagesObject, MessagesObjectVec, messages_object, messages_drop), (uint64_t, UInt64Vec, uint64), \
        (FieldAccessor, FieldAccessorVec, field_accessor, field_accessor_drop)
#include "vector_impl.h"
// clang-format: on
#endif
