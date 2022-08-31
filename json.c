#define JSON_C_
#include "json.h"

#include "util.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Code for the last json parsing error
static JSONError jerrno = NoError;
// Location of the last json parsing error
static size_t jerr_index = 0;

// Get a string explaining the last json parsing error
const char *json_strerr() { return JSONErrorMessage[jerrno]; }
// Get the code of the last json parsing error
JSONError json_errno() { return jerrno; }
// Get the location of the last json parsing error
size_t json_errloc() { return jerr_index; }

// Shorthand to set jerno and return -1;
// i.e
// ```c
// if(error) return set_jerrno(JSONError);
// ```
static inline int set_jerrno(JSONError err) {
    jerrno = err;
    return -1;
}

// Return true if c is a whitespace character
static inline bool is_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

static inline void skip_whitespaces(const char **buf, const char *buf_end) {
    while (*buf < buf_end && is_whitespace(**buf)) {
        (*buf)++;
    }
}

static int json_parse_value(const char **buf, const char *buf_end, uint8_t **restrict dst,
                            const uint8_t *dst_end); // Declaration for recursion

// *dst must be 8 aligned
static inline int json_parse_string(const char **buf, const char *buf_end, uint8_t **restrict dst,
                                    const uint8_t *dst_end) {
    // Ensure enough space for the header
    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    // Build header
    JSONHeader *header = (JSONHeader *)(*dst);
    header->type       = (uint32_t)String;
    header->len        = 0;
    // Increment dst pointer
    *dst += sizeof(JSONHeader);

    // Skip first quote
    (*buf)++;
    // Ensure there is more in the buffer (there should be at least a closing ")
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

    // Loop until return or we met the end of the buffer
    for (; *buf < buf_end; (*buf)++) {
        char c = **buf;

        if (esc_unicode >= 0) { // We're currently in a \uXXXX escape
            // Parse hex digit
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

            // If we got all 4 hex digit, we UTF-8 encode the resulting codepoint
            if (esc_unicode == 4) {
                // From https://en.wikipedia.org/wiki/UTF-8#Encoding
                if (un_codepoint <= 0x7f) {    // 1 byte codepoint => ascii
                    if (*dst + 1 >= dst_end) { // Ensure enough space in the dst buffer
                        return set_jerrno(DstOverflow);
                    }
                    // *(*dst)++ => set **dst to RHS and increment *dst
                    *(*dst)++ = un_codepoint;
                    header->len++;
                } else if (un_codepoint <= 0x7ff) { // 2 byte codepoint
                    if (*dst + 2 >= dst_end) {
                        return set_jerrno(DstOverflow);
                    }

                    *(*dst)++ = 0b11000000 | (un_codepoint >> 6 & 0b011111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 0 & 0b111111);
                    header->len += 2;
                } else if (un_codepoint <= 0xffff) { // 3 byte codepoint
                    if (*dst + 3 >= dst_end) {
                        return set_jerrno(DstOverflow);
                    }

                    *(*dst)++ = 0b11100000 | (un_codepoint >> 12 & 0b1111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 6 & 0b111111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 0 & 0b111111);
                    header->len += 3;
                } else if (un_codepoint <= 0x10ffff) { // 4 byte codepoint
                    if (*dst + 4 >= dst_end) {
                        return set_jerrno(DstOverflow);
                    }

                    *(*dst)++ = 0b11110000 | (un_codepoint >> 18 & 0b111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 12 & 0b111111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 6 & 0b111111);
                    *(*dst)++ = 0b10000000 | (un_codepoint >> 0 & 0b111111);
                    header->len += 4;
                } else { // Illegal codepoint
                    return set_jerrno(StringBadUnicode);
                }
                // We finished parsing the \uXXXX escape
                esc_unicode = -1;
            }
        } else if (esc) {
            char r;
            switch (c) {
            case '"':
            case '\\':
            case '/': // For some reason you can escape a slash in JSON
                // Those stay the same
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

            if (c != 'u') { // \u is the only escape that doesn't immediatly produce a character
                if (*dst + 1 >= dst_end) {
                    return set_jerrno(DstOverflow);
                }
                // *(*dst)++ => set **dst to RHS and increment *dst
                *(*dst)++ = r;
                header->len++;
            }

            esc = false;
        } else {
            if (c == '\\') {
                esc = true;
                continue;
            } else if (c == '"') { // Closing quote
                int padded_len = align_8(header->len);
                // Ensure enough space in dst for padding
                if (*dst + (padded_len - header->len) >= dst_end) {
                    return set_jerrno(DstOverflow);
                }
                // Pad to 8 align
                for (; padded_len > header->len; padded_len--) {
                    *(*dst)++ = '\0';
                }
                // Skip "
                (*buf)++;

                return 0;
            } else if ((c < ' ' && c != '\t') ||
                       c == 0x7f) { // Illegal characters, technically tab isn't allowed either
                                    // but it felt weird so I added it
                jerrno = StringBadChar;
                return -1;
            }

            if (*dst + 1 >= dst_end) {
                return set_jerrno(DstOverflow);
            }

            // *(*dst)++ => set **dst to RHS and increment *dst
            *(*dst)++ = c;
            header->len++;
        }
    }
    // The only way to get out of the loop is if *buf >= buf_end: buffer overflow
    return set_jerrno(SrcOverflow);
}

// *dst must be 8 aligned
static int json_parse_number(const char **buf, const char *buf_end, uint8_t **restrict dst,
                             const uint8_t *dst_end) {
    // Ensure enough space for header and value
    if (*dst + sizeof(JSONHeader) + sizeof(double) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    double     *value  = (double *)((*dst) + sizeof(JSONHeader));
    *dst += sizeof(JSONHeader) + sizeof(double);

    header->type = (uint32_t)Number;
    header->len  = sizeof(double);

    *value = 0.0;

    double sign = 1.0;
    if (**buf == '-') {
        (*buf)++; // Skip -
        sign = -1.0;
    }

    // There has to be at least one digit
    if (*buf >= buf_end) {
        return set_jerrno(SrcOverflow);
    }

    if (**buf != '0') {
        // If the first character is not a zero we have a pententially mutli digit number
        for (; *buf < buf_end; (*buf)++) {
            char c = **buf;
            if (c < '0' || c > '9') { // if c isn't a number
                break;
            }

            *value *= 10.0;
            *value += (double)(c - '0');
        }
    } else {
        // If c is zero we can't have anything else (for the integer part)
        (*buf)++;
    }

    // If there another character and its a . we have a fractional part
    if (*buf < buf_end && **buf == '.') {
        // Decimal place
        double place = 0.1;

        (*buf)++; // Skip .

        // There must be at least one digit after the dot
        if (*buf >= buf_end) {
            return set_jerrno(SrcOverflow);
        }
        if (**buf < '0' || **buf > '9') {
            return set_jerrno(NumberBadChar);
        }

        for (; *buf < buf_end; (*buf)++) {
            char c = **buf;

            if (c < '0' || c > '9') {
                break;
            }

            double digit = (double)(c - '0');
            *value += digit * place;

            place *= 0.1;
        }
    }

    // if theres at least one more character and its an e or an E we got an exponent
    if (*buf < buf_end && (**buf == 'e' || **buf == 'E')) {
        double exp      = 0.0;
        double exp_sign = 1.0;

        (*buf)++; // Skip e/E

        // There must be at least one more character (a digit or a sign followed by digit(s))
        if (*buf >= buf_end) {
            return set_jerrno(SrcOverflow);
        }

        // Handle sign of exponent
        if (**buf == '+' || **buf == '-') {
            exp_sign = **buf == '-' ? -1.0 : 1.0;

            (*buf)++; // Skip sign

            // If there's a sign there must be at least one digit following it
            if (*buf >= buf_end) {
                return set_jerrno(SrcOverflow);
            }
        }

        // Parse exponent
        for (; *buf < buf_end; (*buf)++) {
            char c = **buf;

            if (c < '0' || c > '9') {
                break;
            }

            exp *= 10;
            exp += (double)(c - '0');
        }

        // Apply exponent
        exp *= exp_sign;
        *value *= pow(10.0, exp);
    }

    // Apply sign
    *value *= sign;

    return 0;
}

// *dst must be 8 aligned
static int json_parse_boolean(const char **buf, const char *buf_end, uint8_t **restrict dst,
                              const uint8_t *dst_end) {
    // Ensure enough space for header and value
    if (*dst + sizeof(JSONHeader) + 8 >= dst_end) { // 8: sizeof(uint64_t)
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    uint64_t   *value  = (uint64_t *)((*dst) + sizeof(JSONHeader));
    *dst += sizeof(JSONHeader) + 8;

    header->type = (uint32_t)Boolean;
    header->len  = 8;

    if (**buf == 't') { // The value can only be true, so we check it against that
        if (*buf + 4 > buf_end)
            return set_jerrno(SrcOverflow);
        if (strncmp(*buf, "true", 4) != 0) {
            return set_jerrno(BadKeyword);
        }

        *buf += 4;
        *value = 1;
    } else if (**buf == 'f') { // The value can only be false
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
    // Ensure enough size for the header (no value)
    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    JSONHeader *header = (JSONHeader *)(*dst);
    *dst += sizeof(JSONHeader);

    header->type = (uint32_t)Null;
    header->len  = 0;

    // Check that the word is indeed null
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
    // Ensure enough space for the header
    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    // Setup header
    JSONHeader *header = (JSONHeader *)(*dst);
    *dst += sizeof(JSONHeader);
    // Keep track of pointer to the start of "value" of the array, used to compute the final length
    uint8_t *dst_arr_start = *dst;

    header->type = (uint32_t)Array;

    (*buf)++; // Skip [

    // skip initial whitespace
    skip_whitespaces(buf, buf_end);

    // There should be at least one more character (a value or ])
    if (*buf == buf_end) {
        return set_jerrno(SrcOverflow);
    }

    if (**buf == ']') { // Array is empty
        header->len = 0;
        return 0;
    }

    while (1) {
        // Try to parse a value
        if (json_parse_value(buf, buf_end, dst, dst_end) != 0) {
            return -1;
        }
        // Skip whitespaces after value
        skip_whitespaces(buf, buf_end);

        // There should be at least one more char (, or ])
        if (*buf == buf_end) {
            return set_jerrno(SrcOverflow);
        }

        if (**buf == ',') {
            // Skip , and go for another iteration
            (*buf)++;
        } else if (**buf == ']') {
            // Skip ] and finish
            (*buf)++;
            break;
        } else {
            return set_jerrno(BadChar);
        }
    }
    // Compute len
    header->len = *dst - dst_arr_start;

    return 0;
}

// *dst must be 8 aligned
static int json_parse_object(const char **buf, const char *buf_end, uint8_t **restrict dst,
                             const uint8_t *dst_end) {
    // Esnure enough space for the header
    if (*dst + sizeof(JSONHeader) >= dst_end) {
        return set_jerrno(DstOverflow);
    }

    // Setup header
    JSONHeader *header = (JSONHeader *)(*dst);
    *dst += sizeof(JSONHeader);
    // Keep track of pointer to start of value to compute length later
    uint8_t *dst_obj_start = *dst;

    header->type = (uint32_t)Object;

    (*buf)++; // Skip {

    // Skip initial whitespace (after '{')
    skip_whitespaces(buf, buf_end);
    // There should be at least one more char (a value or })
    if (*buf == buf_end) {
        return set_jerrno(SrcOverflow);
    }
    if (**buf == '}') {
        // The object is empty
        header->len = 0;
        return 0;
    }

    while (1) {
        // Skip whitespace before key
        skip_whitespaces(buf, buf_end);
        // Try to parse key
        if (json_parse_string(buf, buf_end, dst, dst_end) != 0) {
            return -1;
        }
        // Skip whitespace after key
        skip_whitespaces(buf, buf_end);

        // There should be a colon
        if (*buf == buf_end) {
            return set_jerrno(SrcOverflow);
        }
        if (**buf != ':') {
            return set_jerrno(ObjectBadChar);
        }
        (*buf)++;

        // Try to parse value (takes care of whitespaces)
        if (json_parse_value(buf, buf_end, dst, dst_end) != 0) {
            return -1;
        }
        // Skip whitespace after value
        skip_whitespaces(buf, buf_end);

        // There should be at least one char (} or ,)
        if (*buf == buf_end) {
            return set_jerrno(SrcOverflow);
        }

        if (**buf == ',') {
            // Skip , and go for another iteration
            (*buf)++;
        } else if (**buf == '}') {
            // Skip } and finish
            (*buf)++;
            break;
        } else {
            return set_jerrno(BadChar);
        }
    }
    //
    // Compute length
    header->len = *dst - dst_obj_start;

    return 0;
}

// *dst must be 8 aligned
static int json_parse_value(const char **buf, const char *buf_end, uint8_t **restrict dst,
                            const uint8_t *dst_end) {
    for (; *buf < buf_end; (*buf)++) {
        // Ignore initial whitespaces
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

    if (*buf == buf_end) {
        return set_jerrno(SrcOverflow);
    }

    return 0;
}

int json_parse(const char *src, size_t src_len, uint8_t *dst, size_t dst_len) {
    const char *buf     = src;
    const char *buf_end = src + src_len;
    uint8_t    *dst_end = dst + dst_len;

    int rc = json_parse_value(&buf, buf_end, &dst, dst_end);
    // Set location to the difference between were we got to and where we started
    jerr_index = buf - src;
    return rc;
}

void json_print_value_priv(uint8_t **buf) {
    JSONHeader *header = (JSONHeader *)*buf;
    *buf += sizeof(header);

    switch (header->type) {
    case String:
        // TODO: escapes
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
            json_print_value_priv(buf);
            if (*buf < end) {
                printf(",");
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
            json_print_value_priv(buf);
            printf(":");
            json_print_value_priv(buf);
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

// /!\ doesn't handle strings well
void json_print_value(uint8_t *buf) { json_print_value_priv(&buf); }

// Loop over adapters and set accordingly
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

// Run adapters on a value
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

// Run adapters on a json value
void json_adapt(uint8_t *buf, JSONAdapter *adapters, size_t adapter_count, void *ptr) {
    char path[512] = ".";
    json_adapt_priv(&buf, adapters, adapter_count, ptr, path, path);
}
