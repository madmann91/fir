#include "format.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void format(struct format_out* format_out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (format_out->tag == FORMAT_OUT_FILE) {
        vfprintf(format_out->file, fmt, args);
    } else {
        struct format_buf* buf = format_out->format_buf;
        int count = vsnprintf(buf->data + buf->size, buf->capacity - buf->size, fmt, args);
        if (count > 0)
            buf->size += count;
    }
    va_end(args);
}
