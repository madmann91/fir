#include "log.h"
#include "alloc.h"
#include "term.h"

#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>

#define ERROR_STYLE TERM2(TERM_FG_RED, TERM_BOLD)
#define WARN_STYLE  TERM2(TERM_FG_YELLOW, TERM_BOLD)
#define NOTE_STYLE TERM2(TERM_FG_CYAN, TERM_BOLD)
#define RANGE_STYLE TERM2(TERM_FG_WHITE, TERM_BOLD)
#define WITH_STYLE(x, y) (log->disable_colors ? (y) : x y TERM1(TERM_RESET))

enum msg_tag {
    MSG_ERR,
    MSG_WARN,
    MSG_NOTE
};

static inline void log_msg(
    enum msg_tag tag,
    struct log* log,
    const struct fir_source_range* source_range,
    const char* fmt,
    va_list args)
{
    if (tag == MSG_ERR) {
        log->error_count++;
        if (log->error_count >= log->max_errors)
            return;
    }

    if (!log->file)
        return;

    switch (tag) {
        case MSG_ERR:  fprintf(log->file, WITH_STYLE(ERROR_STYLE, "error:"  )); break;
        case MSG_WARN: fprintf(log->file, WITH_STYLE(WARN_STYLE,  "warning:")); break;
        case MSG_NOTE: fprintf(log->file, WITH_STYLE(NOTE_STYLE,  "note:"   )); break;
    }

    fprintf(log->file, " ");
    vfprintf(log->file, fmt, args);
    fprintf(log->file, "\n");

    if (source_range && log->source_name) {
        fprintf(log->file, "  in ");
        fprintf(log->file, WITH_STYLE(RANGE_STYLE, "%s(%"PRIu32":%"PRIu32" - %"PRIu32":%"PRIu32")\n"),
            log->source_name,
            source_range->begin.row,
            source_range->begin.col,
            source_range->end.row,
            source_range->end.col);
    }
}

void log_error(struct log* log, const struct fir_source_range* source_range, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_msg(MSG_ERR, log, source_range, fmt, args);    
    va_end(args);
}

void log_warn(struct log* log, const struct fir_source_range* source_range, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_msg(MSG_WARN, log, source_range, fmt, args);    
    va_end(args);
}

void log_note(struct log* log, const struct fir_source_range* source_range, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_msg(MSG_NOTE, log, source_range, fmt, args);    
    va_end(args);
}
