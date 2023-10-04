#include "lexer.h"

#include "support/mem_pool.h"
#include "support/io.h"
#include "support/log.h"

struct parser {
    struct lexer lexer;
    struct mem_pool mem_pool;
    const char* file_name;
};

struct ast* parse_file(
    const char* file_name,
    struct mem_pool* mem_pool
    struct log* error_log)
{
    size_t file_size = 0;
    char* file_data = read_file(file_name, &file_size);
    if (!file_data) {
        log_print(error_log, 
        fprintf(std
    }
}
