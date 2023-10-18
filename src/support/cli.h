#pragma once

#include "fir/node.h"

#include <stdbool.h>
#include <stddef.h>

struct cli_option {
    const char* short_name;
    const char* long_name;
    bool has_value;
    void* data;
    bool (*parse)(void*, char*);
};

struct cli_option cli_flag(const char*, const char*, bool*);
struct cli_option cli_verbosity(const char*, const char*, enum fir_verbosity*);

bool cli_parse_options(int argc, char** argv, const struct cli_option*, size_t option_count);
