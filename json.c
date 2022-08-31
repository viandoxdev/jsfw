#define JSON_C_
#include "json.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JSONError jerrno     = NoError;
static size_t    jerr_index = 0;

const char *json_strerr() { return JSONErrorMessage[jerrno]; }

size_t json_err_loc() { return jerr_index; }

// Shorthand to set jerno and return -1;
static inline int set_jerrno(JSONError err) {
    jerrno = err;
    return -1;
}
static inline size_t align_8(size_t n) { return (((n - 1) >> 3) + 1) << 3; }
static inline bool   is_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

static int json_parse_value(const char **buf, const char *buf_end, uint8_t **restrict dst,
                            const uint8_t *dst_end);

// *dst must be 8 aligned
static inline int json_parse_string(const char **buf, const char *buf_end, uint8_t **restrict dst,
                                    const uint8_t *dst_end) {
    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    header->type       = (uint32_t)String;
    header->len        = 0;
    *dst += sizeof(JSONHeader);

    // Skip first quote
    (*buf)++;
    if (*buf == buf_end) {
        return set_jerrno(SrcOverflow);
    }

    // If the last char was an esc
    bool esc = false;
    // If we're currently parsing a unicode escape,
    // -1: no, 0-4: we're n char in
    int esc_unicode = -1;
    // The unicode codepoint we're parsing
    int un_codepoint = 0;

    for (; *buf < buf_end; (*buf)++) {
        char c = **buf;

        if (esc_unicode >= 0) {
            int digit = 0;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else {
                return set_jerrno(StringBadUnicode);
            }
            un_codepoint <<= 4;
            un_codepoint += digit;
            esc_unicode++;

            if (esc_unicode == 4) { // UTF-8 Encoding
                if (un_codepoint <= 0x7f) {
                    if (*dst + 1 >= dst_end)
                        return set_jerrno(DstOverflow);

                    *(*dst)++ = un_codepoint;
                    header->len++;
                } else if (un_codepoint <= 0x7ff) {
                    if (*dst + 2 >= dst_end)
                        return set_jerrno(DstOverflow);

                    *(*dst)++ = 0b11000000 | (un_codepoint >> 6 & 0b011111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 0 & 0b111111);
                    header->len += 2;
                } else if (un_codepoint <= 0xffff) {
                    if (*dst + 3 >= dst_end)
                        return set_jerrno(DstOverflow);

                    *(*dst)++ = 0b11100000 | (un_codepoint >> 12 & 0b1111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 6 & 0b111111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 0 & 0b111111);
                    header->len += 3;
                } else if (un_codepoint <= 0x10ffff) {
                    if (*dst + 4 >= dst_end)
                        return set_jerrno(DstOverflow);

                    *(*dst)++ = 0b11110000 | (un_codepoint >> 18 & 0b111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 12 & 0b111111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 6 & 0b111111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 0 & 0b111111);
                    header->len += 4;
                } else {
                    return set_jerrno(StringBadUnicode);
                }
                esc_unicode = -1;
            }
        } else if (esc) {
            char r;
            switch (c) {
            case '"':
            case '\\':
            case '/': // For some reason you can escape a slash in JSON
                r = c;
                break;
            case 'b':
                r = '\b';
                break;
            case 'f':
                r = '\f';
                break;
            case 'n':
                r = '\n';
                break;
            case 'r':
                r = '\r';
                break;
            case 't':
                r = '\t';
                break;
            case 'u':
                esc_unicode = 0;
                break;
            default:
                return set_jerrno(StringBadEscape);
            }

            if (c != 'u') {
                if (*dst + 1 >= dst_end) {
                    return set_jerrno(DstOverflow);
                }
                *(*dst)++ = r;
                header->len++;
            }

            esc = false;
        } else {
            if (c == '\\') {
                esc = true;
                continue;
            } else if (c == '"') {
                int padded_len = align_8(header->len);
                if (*dst + (padded_len - header->len) >= dst_end)
                    return set_jerrno(DstOverflow);
                for (; padded_len > header->len; padded_len--)
                    *(*dst)++ = '\0';
                (*buf)++;
                return 0;
            } else if ((c < ' ' && c != '\t') || c == 0x7f) {
                jerrno = StringBadChar;
                return -1;
            }
            if (*dst + 1 >= dst_end)
                return set_jerrno(DstOverflow);
            *(*dst)++ = c;
            header->len++;
        }
    }
    // The only way to get out of the loop is if *buf >= buf_end
    return set_jerrno(SrcOverflow);
}

// *dst must be 8 aligned
static int json_parse_number(const char **buf, const char *buf_end, uint8_t **restrict dst,
                             const uint8_t *dst_end) {

    if (*dst + sizeof(JSONHeader) + sizeof(double) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    double     *value  = (double *)((*dst) + sizeof(JSONHeader));
    *dst += sizeof(JSONHeader) + sizeof(double);

    header->type = (uint32_t)Number;
    header->len  = sizeof(double);

    double sign = 1.0;
    if (**buf == '-') {
        (*buf)++;
        sign = -1.0;
    }

    if (*buf >= buf_end)
        return set_jerrno(SrcOverflow);
    if (**buf != '0') {
        for (; *buf < buf_end; (*buf)++) {
            char c = **buf;
            if (c < '0' || c > '9') {
                break;
            }

            *value *= 10.0;
            *value += (double)(c - '0');
        }
    } else {
        (*buf)++;
    }

    if (*buf < buf_end && **buf == '.') {
        double place = 0.1;
        (*buf)++; // Skip dot
        if (*buf >= buf_end)
            return set_jerrno(SrcOverflow);
        if (**buf < '0' || **buf > '9')
            return set_jerrno(NumberBadChar);

        for (; *buf < buf_end; (*buf)++) {
            char c = **buf;
            if (c < '0' || c > '9')
                break;
            double digit = (double)(c - '0');
            *value += digit * place;
            place *= 0.1;
        }
    }

    if (*buf < buf_end && (**buf == 'e' || **buf == 'E')) {
        double exp      = 0.0;
        double exp_sign = 1.0;

        (*buf)++; // Skip e/E
        if (*buf >= buf_end)
            return set_jerrno(SrcOverflow);

        if (**buf == '+' || **buf == '-') {
            exp_sign = **buf == '-' ? -1.0 : 1.0;
            (*buf)++;
            if (*buf >= buf_end)
                return set_jerrno(SrcOverflow);
        }

        for (; *buf < buf_end; (*buf)++) {
            char c = **buf;
            if (c < '0' || c > '9')
                break;
            exp *= 10;
            exp += (double)(c - '0');
        }

        exp *= exp_sign;
        *value *= pow(10.0, exp);
    }

    *value *= sign;

    return 0;
}

// *dst must be 8 aligned
static int json_parse_boolean(const char **buf, const char *buf_end, uint8_t **restrict dst,
                              const uint8_t *dst_end) {

    if (*dst + sizeof(JSONHeader) + 8 >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    uint64_t   *value  = (uint64_t *)((*dst) + sizeof(JSONHeader));
    *dst += sizeof(JSONHeader) + 8;

    header->type = (uint32_t)Boolean;
    header->len  = 8;

    if (**buf == 't') {
        if (*buf + 4 > buf_end)
            return set_jerrno(SrcOverflow);
        if (strncmp(*buf, "true", 4) != 0) {
            return set_jerrno(BadKeyword);
        }
        *buf += 4;
        *value = 1;
    } else if (**buf == 'f') {
        if (*buf + 5 > buf_end)
            return set_jerrno(SrcOverflow);
        if (strncmp(*buf, "false", 5) != 0) {
            return set_jerrno(BadKeyword);
        }
        *buf += 5;
        *value = 0;
    } else {
        return set_jerrno(BadKeyword);
    }
    return 0;
}

// *dst must be 8 aligned
static int json_parse_null(const char **buf, const char *buf_end, uint8_t **restrict dst,
                           const uint8_t *dst_end) {

    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    *dst += sizeof(JSONHeader);

    header->type = (uint32_t)Null;
    header->len  = 0;

    if (*buf + 4 > buf_end)
        return set_jerrno(SrcOverflow);
    if (strncmp(*buf, "null", 4) != 0) {
        return set_jerrno(BadKeyword);
    }
    *buf += 4;
    return 0;
}

// *dst must be 8 aligned
static int json_parse_array(const char **buf, const char *buf_end, uint8_t **restrict dst,
                            const uint8_t *dst_end) {

    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    *dst += sizeof(JSONHeader);
    uint8_t *dst_arr_start = *dst;

    header->type = (uint32_t)Array;

    (*buf)++; // Skip [
    // skip initial whitespace
    for (; *buf < buf_end && is_whitespace(**buf); (*buf)++)
        ;
    if (*buf == buf_end)
        return set_jerrno(SrcOverflow);
    if (**buf == ']') {
        header->len = 0;
        return 0;
    }
    while (1) {
        if (json_parse_value(buf, buf_end, dst, dst_end) != 0)
            return -1;
        for (; *buf < buf_end && is_whitespace(**buf); (*buf)++)
            ;
        if (*buf == buf_end)
            return set_jerrno(SrcOverflow);
        if (**buf == ',') {
            (*buf)++;
        } else if (**buf == ']') {
            (*buf)++;
            break;
        } else {
            return set_jerrno(BadChar);
        }
    }
    header->len = *dst - dst_arr_start;
    return 0;
}

// *dst must be 8 aligned
static int json_parse_object(const char **buf, const char *buf_end, uint8_t **restrict dst,
                             const uint8_t *dst_end) {

    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    *dst += sizeof(JSONHeader);
    uint8_t *dst_obj_start = *dst;

    header->type = (uint32_t)Object;

    (*buf)++; // Skip {

    for (; *buf < buf_end && is_whitespace(**buf); (*buf)++)
        ;
    if (*buf == buf_end)
        return set_jerrno(SrcOverflow);
    if (**buf == '}') {
        header->len = 0;
        return 0;
    }

    while (1) {
        // Skip whitespace before key
        for (; *buf < buf_end && is_whitespace(**buf); (*buf)++)
            ;
        // Parse key
        if (json_parse_string(buf, buf_end, dst, dst_end) != 0)
            return -1;
        // Skip whitespace after key
        for (; *buf < buf_end && is_whitespace(**buf); (*buf)++)
            ;
        // There should be at least one char
        if (*buf == buf_end)
            return set_jerrno(SrcOverflow);
        // There should be a colon
        if (**buf != ':')
            return set_jerrno(ObjectBadChar);
        // Skip colon
        (*buf)++;
        // Parse value (takes char of whitespace)
        if (json_parse_value(buf, buf_end, dst, dst_end) != 0)
            return -1;
        // Skip whitespace after value
        for (; *buf < buf_end && is_whitespace(**buf); (*buf)++)
            ;
        // There should be at least one char (} or ,)
        if (*buf == buf_end)
            return set_jerrno(SrcOverflow);
        if (**buf == ',') {
            (*buf)++;
        } else if (**buf == '}') {
            (*buf)++;
            break;
        } else {
            return set_jerrno(BadChar);
        }
    }
    header->len = *dst - dst_obj_start;
    return 0;
}

// *dst must be 8 aligned
static int json_parse_value(const char **buf, const char *buf_end, uint8_t **restrict dst,
                            const uint8_t *dst_end) {
    for (; *buf < buf_end; (*buf)++) {
        if (is_whitespace(**buf))
            continue;

        switch (**buf) {
        case '"':
            return json_parse_string(buf, buf_end, dst, dst_end);
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return json_parse_number(buf, buf_end, dst, dst_end);
        case '{':
            return json_parse_object(buf, buf_end, dst, dst_end);
        case '[':
            return json_parse_array(buf, buf_end, dst, dst_end);
        case 't':
        case 'f':
            return json_parse_boolean(buf, buf_end, dst, dst_end);
        case 'n':
            return json_parse_null(buf, buf_end, dst, dst_end);
        default:
            return set_jerrno(BadChar);
        }
    }
    if (*buf == buf_end)
        return set_jerrno(SrcOverflow);
    return 0;
}

int json_parse(const char *src, size_t src_len, uint8_t *dst, size_t dst_len) {
    memset(dst, 0, dst_len);
    const char *buf     = src;
    const char *buf_end = src + src_len;
    uint8_t    *dst_end = dst + dst_len;
    int         rc      = json_parse_value(&buf, buf_end, &dst, dst_end);
    jerr_index          = buf - src;
    return rc;
}

void json_print_value(uint8_t **buf) {
    JSONHeader *header = (JSONHeader *)*buf;
    *buf += sizeof(header);
    switch (header->type) {
    case String:
        printf("\"%.*s\"", header->len, *buf);
        *buf += align_8(header->len);
        break;
    case Number:
        printf("%lf", *(double *)*buf);
        *buf += sizeof(double);
        break;
    case Boolean: {
        uint64_t value = *(uint64_t *)*buf;
        if (value == 1) {
            printf("true");
        } else if (value == 0) {
            printf("false");
        } else {
            printf("(boolean) garbage");
        }
        *buf += 8;
    } break;
    case Null:
        printf("null");
        break;
    case Array: {
        uint8_t *end = *buf + header->len;
        printf("[");
        while (1) {
            json_print_value(buf);
            if (*buf < end) {
                printf(", ");
            } else {
                printf("]");
                break;
            }
        }
    } break;
    case Object: {
        uint8_t *end = *buf + header->len;
        printf("{");
        while (1) {
            json_print_value(buf);
            printf(":");
            json_print_value(buf);
            if (*buf < end) {
                printf(",");
            } else {
                printf("}");
                break;
            }
        }
    } break;
    }
}

struct Test {
    double a;
    char  *b;
};

const JSONAdapter TestAdapter[] = {
    {".a", Number, offsetof(struct Test, a)},
    {".b", String, offsetof(struct Test, b)},
};

static void json_adapt_set(uint8_t *buf, JSONAdapter *adapters, size_t adapter_count, void *ptr, char *path) {
    JSONHeader *header = (JSONHeader *)buf;
    for (int i = 0; i < adapter_count; i++) {
        if (strcmp(path, adapters[i].path) == 0 && header->type == adapters[i].type) {
            void *p = ptr + adapters[i].offset;
            switch (header->type) {
            case String: {
                char *v = malloc(header->len + 1);
                strncpy(v, (char *)(buf + sizeof(JSONHeader)), header->len);
                v[header->len] = '\0';
                *(char **)p    = v;
            } break;
            case Number:
                *(double *)p = *(double *)(buf + sizeof(JSONHeader));
                break;
            case Boolean:
                *(bool *)p = *(uint64_t *)(buf + sizeof(JSONHeader)) == 1;
                break;
            }
        }
    }
}

static void json_adapt_priv(uint8_t **buf, JSONAdapter *adapters, size_t adapter_count, void *ptr,
                            char *full_path, char *path) {
    JSONHeader *header = (JSONHeader *)*buf;

    switch (header->type) {
    case String:
        json_adapt_set(*buf, adapters, adapter_count, ptr, full_path);
        *buf += sizeof(JSONHeader) + align_8(header->len);
        break;
    case Number:
        json_adapt_set(*buf, adapters, adapter_count, ptr, full_path);
        *buf += sizeof(JSONHeader) + sizeof(double);
        break;
    case Boolean:
        json_adapt_set(*buf, adapters, adapter_count, ptr, full_path);
        *buf += sizeof(JSONHeader) + 8;
        break;
    case Null:
        *buf += sizeof(JSONHeader);
        break;
    case Array: {
        *buf += sizeof(JSONHeader);
        uint8_t *end = *buf + header->len;
        for (size_t index = 0; *buf < end; index++) {
            int len = sprintf(path, ".%lu", index);
            json_adapt_priv(buf, adapters, adapter_count, ptr, full_path, path + len);
        }
    } break;
    case Object: {
        *buf += sizeof(JSONHeader);
        uint8_t *end = *buf + header->len;
        while (*buf < end) {
            JSONHeader *key_header = (JSONHeader *)*buf;
            *buf += sizeof(JSONHeader);

            int len = sprintf(path, ".%.*s", key_header->len, *buf);
            *buf += align_8(key_header->len);

            json_adapt_priv(buf, adapters, adapter_count, ptr, full_path, path + len);
        }
    } break;
    }
}

void json_adapt(uint8_t *buf, JSONAdapter *adapters, size_t adapter_count, void *ptr) {
    char path[512] = ".";
    json_adapt_priv(&buf, adapters, adapter_count, ptr, path, path);
}
