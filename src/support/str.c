#include "str.h"

#include <stdarg.h>
#include <stdio.h>

void str_printf(struct str* str, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t remaining_size = str->capacity - str->length;
    int req_size = vsnprintf(str->data + str->length, remaining_size, fmt, args);
    if ((size_t)req_size >= remaining_size) {
        str_grow(str, req_size + 1);
        remaining_size = str->capacity - str->length;
        req_size = vsnprintf(str->data + str->length, remaining_size, fmt, args);
        assert((size_t)req_size < remaining_size);
        str->length += req_size;
    }
    va_end(args);
}
