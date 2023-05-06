#ifndef SOURCE_H
#define SOURCE_H
#include "utils.h"
#include "vector_impl.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t line;
    uint32_t column;
    uint32_t offset;
} Location;

typedef struct {
    Location loc;
    uint32_t len;
} Span;

typedef struct {
    StringSlice slice;
    Span span;
} SpannedStringSlice;

int span_compare(const void *sa, const void *sb);

bool sss_equal(const void *a, const void *b);
uint32_t sss_hash(Hasher state, const void *v);

VECTOR_IMPL(Span, SpanVec, span);
VECTOR_IMPL(SpannedStringSlice, SpannedStringSliceVec, spanned_string_slice);

typedef struct {
    // The string content
    const char *str;
    // Path of the source file if available
    const char *path;
    uint32_t len;
    IF_DEBUG(uint32_t ref_count;)
} Source;

typedef enum : uint32_t {
    SourceErrorNoError = 0,
    SourceErrorReadFailed = 1,
    SourceErrorOpenFailed = 2,
} SourceError;

typedef enum {
    ReportSeverityError,
    ReportSeverityWarning,
    ReportSeverityNote,
} ReportSeverity;

typedef struct {
    Span span;
    ReportSeverity sev;
    const char *message;
} ReportSpan;

VECTOR_IMPL(ReportSpan, ReportSpanVec, report_span);

static inline __attribute__((always_inline)) Location location(uint32_t line, uint32_t column, uint32_t offset) {
    return (Location){.line = line, .column = column, .offset = offset};
}

// Initialize source from a string and its length (without null terminator), the string will be copied.
Source source_init(const char *str, uint32_t len);
// Try to initialize source from a FILE*
SourceError source_from_file(FILE *f, Source *src);
// Try to initialize source
SourceError source_open(const char *path, Source *src);
// Destroy source
void source_drop(Source src);
void source_report(
    const Source *src,
    Location loc,
    ReportSeverity sev,
    const ReportSpan *pspans,
    uint32_t span_count,
    const char *help,
    const char *fmt,
    ...
);

static inline Span span_from_to(Location from, Location to) {
    return (Span){
        .loc = from,
        .len = to.offset - from.offset,
    };
}
#endif
