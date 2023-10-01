#ifndef FIR_VERSION_H
#define FIR_VERSION_H

#include "fir/platform.h"

#include <stdint.h>

/// @file
///
/// Version information for the FIR library.

/// Gets the major version number for this release of the library.
FIR_SYMBOL uint32_t fir_version_major(void);

/// Gets the minor version number for this release of the library.
FIR_SYMBOL uint32_t fir_version_minor(void);

/// Gets the patch version number for this release of the library.
FIR_SYMBOL uint32_t fir_version_patch(void);

/// Gets the timestamp for this release of the library.
/// The timestamp is an integer whose decimal representation is YYYYMMDD, where YYYY is the year, MM
/// is the month, and DD is the day.
FIR_SYMBOL uint32_t fir_version_timestamp(void);

#endif
