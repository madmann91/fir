#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

[[nodiscard]] char* read_file(const char* file_name, size_t* size);
[[nodiscard]] bool is_terminal(FILE*);
