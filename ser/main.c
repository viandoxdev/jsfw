#include "ast.h"
#include "codegen_c.h"
#include "hashmap.h"
#include "lexer.h"
#include "log.h"
#include "parser.h"
#include "source.h"

#include <stdio.h>
#include <string.h>

void abort_error(uint32_t error_count) {
    fprintf(stderr, "\033[1;91merror\033[0m: aborting due to previous error%s\n", error_count > 1 ? "s" : "");
    exit(1);
}

typedef enum {
    BackendC,
} Backend;

Backend parse_backend(const char *b) {
    if (strcmp(b, "c") == 0) {
        return BackendC;
    } else {
        log_error("Couldn't parse requested backend: got %s expected one of 'c'.", b);
        exit(1);
    }
}

int main(int argc, char **argv) {
    logger_set_fd(stderr);
    logger_enable_severities(Info | Warning | Error);
    logger_init();

    if (argc != 4) {
        fprintf(stderr, "Expected 3 arguments: ser <source> <lang> <output>\n");
    }

    char *source_path = argv[1];
    Backend back = parse_backend(argv[2]);
    char *output = argv[3];

    Source src;
    SourceError serr = source_open(source_path, &src);

    if (serr != SourceErrorNoError) {
        log_error("Error when opening or reading source");
        exit(1);
    }

    LexingResult lexing_result = lex(&src);
    if (lexing_result.errors.len > 0) {
        for (size_t i = 0; i < lexing_result.errors.len; i++) {
            lexing_error_report(&lexing_result.errors.data[i]);
        }
        abort_error(lexing_result.errors.len);
    }
    vec_drop(lexing_result.errors);

    ParsingResult parsing_result = parse(lexing_result.tokens);

    if (parsing_result.errors.len > 0) {
        for (size_t i = 0; i < parsing_result.errors.len; i++) {
            parsing_error_report(&src, &parsing_result.errors.data[i]);
        }
        abort_error(parsing_result.errors.len);
    }
    vec_drop(parsing_result.errors);

    EvaluationResult evaluation_result = resolve_statics(&parsing_result.ctx);
    if (evaluation_result.errors.len > 0) {
        for (size_t i = 0; i < evaluation_result.errors.len; i++) {
            eval_error_report(&src, &evaluation_result.errors.data[i]);
        }
        abort_error(evaluation_result.errors.len);
    }
    vec_drop(evaluation_result.errors);

    switch (back) {
    case BackendC: {
        char *basename;
        {
            char *last_slash = strrchr(output, '/');
            if (last_slash == NULL) {
                basename = output;
            } else {
                basename = last_slash + 1;
            }
        }
        char *header_path = msprintf("%s.h", output);
        char *source_path = msprintf("%s.c", output);

        FileWriter header = file_writer_init(header_path);
        FileWriter source = file_writer_init(source_path);

        codegen_c((Writer *)&header, (Writer *)&source, basename, &evaluation_result.program);

        file_writer_drop(header);
        file_writer_drop(source);

        free(source_path);
        free(header_path);
        break;
    }
    }

    program_drop(evaluation_result.program);
    ast_drop(parsing_result.ctx);
    vec_drop(lexing_result.tokens);
    source_drop(src);
}
