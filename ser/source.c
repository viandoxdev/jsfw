#include "source.h"

#include "assert.h"
#include "vector.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

uint32_t sss_hash(Hasher state, const void *v) {
    SpannedStringSlice *sss = (SpannedStringSlice *)v;
    return string_slice_hash(state, &sss->slice);
}
bool sss_equal(const void *a, const void *b) {
    SpannedStringSlice *sa = (SpannedStringSlice *)a;
    SpannedStringSlice *sb = (SpannedStringSlice *)b;
    return string_slice_equal(sa, sb);
}

Source source_init(const char *str, uint32_t len) {
    char *ptr = malloc(len + 1);
    assert_alloc(ptr);
    strncpy(ptr, str, len);
    ptr[len] = '\0';
    // Will initlalize ref_count to 0 in DEBUG mode as well
    return (Source){.str = ptr, .len = len, .path = NULL};
}
SourceError source_from_file(FILE *f, Source *src) {
    fseek(f, 0, SEEK_END);
    uint64_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *ptr = malloc(len + 1);

    if (fread(ptr, 1, len, f) != len) {
        return SourceErrorReadFailed;
    }

    IF_DEBUG(src->ref_count = 0);
    src->str = ptr;
    src->len = len;
    src->path = NULL;
    return SourceErrorNoError;
}
SourceError source_open(const char *path, Source *src) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return SourceErrorOpenFailed;
    }

    SourceError err = source_from_file(f, src);
    fclose(f);

    if (err == SourceErrorNoError) {
        char *p = strdup(path);
        assert_alloc(p);
        src->path = p;
    }

    return err;
}
void source_drop(Source src) {
    IF_DEBUG({
        if (src.ref_count > 0) {
            log_error("Trying to destroy currently used source, leaking instead");
            return;
        }
    });
    if (src.path != NULL) {
        free((char *)src.path);
    }
    free((char *)src.str);
}

int span_compare(const void *sa, const void *sb) {
    Span *a = (Span *)sa;
    Span *b = (Span *)sb;
    int line = a->loc.line - b->loc.line;
    if (line != 0)
        return line;
    int column = b->loc.column - a->loc.column;
    if (column != 0)
        return column;
    return a->len - b->len;
}

static int report_span_compare(const void *va, const void *vb) {
    ReportSpan *a = (ReportSpan *)va;
    ReportSpan *b = (ReportSpan *)vb;
    return span_compare(&a->span, &b->span);
}

void source_report(
    const Source *src,
    Location loc,
    ReportSeverity sev,
    const ReportSpan *pspans,
    uint32_t span_count,
    const char *help,
    const char *fmt,
    ...
) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char *message = malloc(len + 1);
    assert_alloc(message);
    va_start(args, fmt);
    vsnprintf(message, len + 1, fmt, args);
    va_end(args);

    ReportSpanVec spans = vec_init();
    vec_push_array(&spans, pspans, span_count);
    qsort(spans.data, spans.len, sizeof(ReportSpan), report_span_compare);

    const char *s;
    switch (sev) {
    case ReportSeverityError:
        s = "\033[91merror";
        break;
    case ReportSeverityWarning:
        s = "\033[93mwarning";
        break;
    case ReportSeverityNote:
        s = "\033[92mnote";
        break;
    default:
        s = "?????";
        break;
    }
    const char *file;
    if (src->path == NULL) {
        file = "<unknown source>";
    } else {
        file = src->path;
    }

    uint32_t last_line, first_line;
    if (spans.len > 0) {
        last_line = spans.data[spans.len - 1].span.loc.line;
        first_line = spans.data[0].span.loc.line;
    } else {
        last_line = loc.line;
        first_line = loc.line;
    }

    uint32_t pad = floor(log10(last_line)) + 2;

    fprintf(
        stderr,
        "\033[1m%s\033[0;1m: %s\n%*s\033[94m--> \033[0m%s:%d:%d\n%*s\033[1;94m|\n",
        s,
        message,
        pad - 1,
        "",
        file,
        loc.line,
        loc.column,
        pad,
        ""
    );

    free(message);

    // The line of the span
    StyledString line_str = styled_string_init();
    // Extra lines used when no more space in the sub
    StyledStringVec sub_strs = vec_init();
    uint32_t line_length;
    uint32_t offset;

    last_line = first_line - 1;
    for (uint32_t i = 0; i < spans.len; i++) {
        ReportSpan rspan = spans.data[i];
        Span span = rspan.span;

        offset = span.loc.offset - span.loc.column;
        uint32_t line_end_off = offset;
        while (line_end_off < src->len && src->str[line_end_off] != '\n') {
            line_end_off++;
        }

        uint32_t line_delta = span.loc.line - last_line;

        line_length = line_end_off - offset;
        last_line = span.loc.line;

        styled_string_clear(&line_str);
        vec_clear(&sub_strs);
        vec_push(&sub_strs, styled_string_init());
        styled_string_set(&line_str, 0, NULL, &src->str[offset], line_length);

        while (i < spans.len && spans.data[i].span.loc.line == last_line) {
            ReportSpan rspan = spans.data[i];
            Span span = rspan.span;
            ReportSeverity span_sev = rspan.sev;

            const char *sev_style = "\033[1;97m";
            char underline = ' ';
            switch (span_sev) {
            case ReportSeverityError:
                sev_style = "\033[1;91m";
                underline = '^';
                break;
            case ReportSeverityWarning:
                sev_style = "\033[1;93m";
                underline = '^';
                break;
            case ReportSeverityNote:
                sev_style = "\033[1;94m";
                underline = '-';
                break;
            }

            styled_string_set_style(&line_str, span.loc.column, sev_style, span.len);
            styled_string_set_style(sub_strs.data, span.loc.column, sev_style, span.len);
            styled_string_fill(&sub_strs.data[0], span.loc.column, underline, span.len);

            // Not a loop, but I want break;
            while (rspan.message != NULL) {
                int mlen = strlen(rspan.message);
                size_t index = span.loc.column + span.len + 1;
                if (styled_string_available_space(&sub_strs.data[0], index, mlen + 1) > mlen) {
                    styled_string_set(&sub_strs.data[0], index, sev_style, rspan.message, mlen);
                    // We got the message in
                    break;
                }

                index = span.loc.column;

                // We never put any message on the second sub string, so it needs to exist if we put one on the third
                if (sub_strs.len == 1) {
                    vec_push(&sub_strs, styled_string_init());
                }

                // Start looking at the third sub line
                size_t line = 2;
                while (true) {
                    // The line doesn't exist yet: it is available
                    if (line >= sub_strs.len) {
                        vec_push(&sub_strs, styled_string_init());
                        break;
                    }
                    // Check if the subline is ok
                    if (styled_string_available_space(&sub_strs.data[line], index, mlen + 1) > mlen) {
                        break;
                    }
                    // We couldn't find any space, continue on the next line.
                    line++;
                }

                for (size_t l = 1; l < line; l++) {
                    styled_string_set(&sub_strs.data[l], index, sev_style, "|", 1);
                }

                styled_string_set(&sub_strs.data[line], index, sev_style, rspan.message, mlen);
                break;
            }

            i++;
        }
        // We exited the loop, i points to a span on the next line or to the end of spans
        // Se decrement it because it'll get reincremented by the outer for loop
        i--;

        // Print elipsies if we skipped more than a line
        if (line_delta > 2) {
            fprintf(stderr, "\033[1;94m...\n");
        } else if (line_delta > 1) {
            uint32_t off_end = offset - 1;
            uint32_t off_start = off_end;
            while (src->str[off_start - 1] != '\n' && off_start > 0) {
                off_start--;
            }
            uint32_t len = off_end - off_start;
            fprintf(stderr, "\033[1;94m%*d |\033[0m  %.*s\n", pad - 1, last_line - 1, len, &src->str[off_start]);
        }

        char *line = styled_string_build(&line_str);
        fprintf(stderr, "\033[1;94m%*d |\033[0m  %s\n", pad - 1, last_line, line);
        free(line);
        for (size_t i = 0; i < sub_strs.len; i++) {
            line = styled_string_build(&sub_strs.data[i]);
            fprintf(stderr, "%*s\033[1;94m|\033[0m  %s\n", pad, "", line);
            free(line);
        }
    }

    styled_string_drop(line_str);
    vec_drop(sub_strs);
    vec_drop(spans);

    if (help != NULL) {
        fprintf(stderr, "\033[1;96mhelp\033[0m: %s\n", help);
    }
}
