#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

char* read_file(const char* file_name, size_t* size);
bool is_terminal(FILE*);
