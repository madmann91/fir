#include "cli.h"

#include <string.h>
#include <stdio.h>

enum cli_state {
    STATE_ACCEPTED,
    STATE_REJECTED,
    STATE_ERROR
};

static inline bool set_flag(void* data, char*) {
    return *(bool*)data = true;
}

struct cli_option cli_flag(const char* short_name, const char* long_name, bool* data) {
    return (struct cli_option) {
        .short_name = short_name,
        .long_name = long_name,
        .data = data,
        .parse = set_flag
    };
}

static inline bool set_verbosity(void* data, char* arg) {
    if (!strcmp(arg, "compact")) return *(enum fir_verbosity*)data = FIR_VERBOSITY_COMPACT, true;
    if (!strcmp(arg, "medium"))  return *(enum fir_verbosity*)data = FIR_VERBOSITY_MEDIUM, true;
    if (!strcmp(arg, "high"))    return *(enum fir_verbosity*)data = FIR_VERBOSITY_HIGH, true;
    fprintf(stderr, "invalid verbosity level '%s', must be 'compact', 'medium', or 'high'\n", arg);
    return false;
}

struct cli_option cli_verbosity(const char* short_name, const char* long_name, enum fir_verbosity* verbosity) {
    return (struct cli_option) {
        .short_name = short_name,
        .long_name = long_name,
        .data = verbosity,
        .parse = set_verbosity,
        .has_value = true
    };
}

static inline char* take_arg(char** argv, int i) {
    char* arg = argv[i];
    argv[i] = NULL;
    return arg;
}

static inline enum cli_state to_state(bool ret_val) {
    return ret_val ? STATE_ACCEPTED : STATE_ERROR;
}

static inline enum cli_state accept_option(int argc, char** argv, int* i, const struct cli_option* option) {
    if (option->short_name && !strcmp(argv[*i], option->short_name)) {
        if (!option->has_value) {
            take_arg(argv, *i);
            return to_state(option->parse(option->data, NULL));
        }
        if (*i + 1 >= argc)
            goto missing_argument;
        take_arg(argv, *i);
        return to_state(option->parse(option->data, take_arg(argv, ++(*i))));
    } else if (option->long_name) {
        size_t name_len = strlen(option->long_name);
        if (strncmp(argv[*i], option->long_name, name_len))
            return STATE_REJECTED;
        if (!option->has_value) {
            if (argv[*i][name_len] != 0)
                return STATE_REJECTED;
            take_arg(argv, *i);
            return to_state(option->parse(option->data, NULL));
        }
        if (argv[*i][name_len] == '=') {
            return to_state(option->parse(option->data, take_arg(argv, *i) + name_len + 1));
        } else if (argv[*i][name_len] == 0) {
            if (*i + 1 >= argc)
                goto missing_argument;
            take_arg(argv, *i);
            return to_state(option->parse(option->data, take_arg(argv, ++(*i))));
        }
    }
    return STATE_REJECTED;

missing_argument:
    fprintf(stderr, "missing argument for '%s'\n", argv[*i]);
    return STATE_ERROR;
}

bool cli_parse_options(int argc, char** argv, const struct cli_option* options, size_t option_count) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-')
            continue;
        for (size_t j = 0; j < option_count; ++j) {
            enum cli_state state = accept_option(argc, argv, &i, &options[j]);
            if (state == STATE_ACCEPTED)
                goto next_arg;
            if (state == STATE_ERROR)
                return false;
        }
        fprintf(stderr, "invalid option '%s'\n", argv[i]);
        return false;
next_arg:
    }
    return true;
}
