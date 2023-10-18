#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cli_option {
    const char* short_name;
    const char* long_name;
    bool has_value;
    void* data;
    bool (*parse)(void*, char*);
};

[[nodiscard]] struct cli_option cli_flag(const char*, const char*, bool*);
[[nodiscard]] struct cli_option cli_option_uint32(const char*, const char*, uint32_t*);
[[nodiscard]] struct cli_option cli_option_uint64(const char*, const char*, uint64_t*);
[[nodiscard]] struct cli_option cli_option_string(const char*, const char*, char**);

bool cli_parse_options(
    int argc, char** argv,
    const struct cli_option*,
    size_t option_count);
