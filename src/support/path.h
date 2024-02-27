#pragma once

#include "str.h"

[[nodiscard]] static inline bool is_path_separator(char c) {
    return c == '\\' || c == '/';
}

[[nodiscard]] static inline struct str_view skip_dir(struct str_view path) { 
    for (size_t i = path.length; i-- > 0;) {
        if (is_path_separator(path.data[i]))
            return str_view_shrink(path, i + 1, 0);
    }
    return path;
}

[[nodiscard]] static inline struct str_view trim_ext(struct str_view path) {
    for (size_t i = path.length; i-- > 0;) {
        if (path.data[i] == '.')
            return str_view_shrink(path, 0, path.length - i);
    }
    return path;
}
