#ifndef FIR_FP_FLAGS_H
#define FIR_FP_FLAGS_H

#include <stdint.h>

/// @file
///
/// Floating-point flags determine the set of optimizations that are allowed on floating-point
/// operations. The strictest level allows no optimization that would break the semantics of the
/// IEEE-754 standard.

/// Floating-point flags.
enum fir_fp_flags {
    FIR_FP_FINITE_ONLY    = 0x01, ///< Assumes that only finite values are used.
    FIR_FP_NO_SIGNED_ZERO = 0x02, ///< Assumes that negative zero is the same as positive zero.
    FIR_FP_ASSOCIATIVE    = 0x04, ///< Assumes that floating-point math is associative.
    FIR_FP_DISTRIBUTIVE   = 0x08, ///< Assumes that floating-point math is distributive.

    /// Fast-math mode, non IEEE-754 compliant.
    FIR_FP_FAST =
        FIR_FP_FINITE_ONLY |
        FIR_FP_NO_SIGNED_ZERO |
        FIR_FP_ASSOCIATIVE |
        FIR_FP_DISTRIBUTIVE,

    /// Strict math mode, IEEE-754 compliant.
    FIR_FP_STRICT = 0
};

#endif
