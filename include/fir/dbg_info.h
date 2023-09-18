#ifndef FIR_DBG_INFO_H
#define FIR_DBG_INFO_H

#include "fir/platform.h"

#include <stdint.h>

/// @file
///
/// Debug information is represented as an object name, along with a source file location. The
/// compiler does its best to propagate it as best as possible between the various passes. The
/// client of the library is responsible for setting appropriate debug information on every node
/// that needs it, and managing the lifetime of said information. To simplify memory management for
/// debug information, this module offers a debug information pool that stores strings uniquely.

/// Row and column position in a source file. Rows and column numbers start at 1.
struct fir_source_pos {
    uint32_t row, col;
};

/// A range of characters in a source file.
struct fir_source_range {
    struct fir_source_pos begin, end;
};

/// Debug information that can be attached to a node.
struct fir_dbg_info {
    const char* name;       ///< Object name.
    const char* file_name;  ///< File name, or NULL.
                                      
    /// Source file range that corresponds to the object, ignored when file_name is NULL.
    struct fir_source_range source_range;
};

/// @struct fir_dbg_info_pool
/// Debug information pool.
struct fir_dbg_info_pool;

/// Creates an empty debug information pool.
FIR_SYMBOL struct fir_dbg_info_pool* fir_dbg_info_pool_create(void);

/// Destroys a debug information pool. This destroys all contained debug information objects.
FIR_SYMBOL void fir_dbg_info_pool_destroy(struct fir_dbg_info_pool*);

/// Creates a debug information object and stores it into the given pool.
/// Name and filename may be NULL. If they are not, they are copied uniquely into the pool.
FIR_SYMBOL const struct fir_dbg_info* fir_dbg_info(
    struct fir_dbg_info_pool*,
    const char* name,
    const char* file_name,
    struct fir_source_range source_range);

#endif
