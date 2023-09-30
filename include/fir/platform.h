#ifndef FIR_PLATFORM_H
#define FIR_PLATFORM_H

#include <stdint.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define FIR_IMPORT __declspec(dllimport)
#define FIR_EXPORT __declspec(dllexport)
#elif defined(__clang__) || defined(__GNUC__)
#define FIR_IMPORT
#define FIR_EXPORT __attribute__((visibility("default")))
#else
#define FIR_IMPORT
#define FIR_EXPORT
#endif

#ifdef FIR_EXPORT_SYMBOLS
#define FIR_SYMBOL FIR_EXPORT
#else
#define FIR_SYMBOL FIR_IMPORT
#endif

/// Gets the major version number for this release of the library.
FIR_SYMBOL uint32_t fir_version_major(void);

/// Gets the minor version number for this release of the library.
FIR_SYMBOL uint32_t fir_version_minor(void);

/// Gets the patch version number for this release of the library.
FIR_SYMBOL uint32_t fir_version_patch(void);

/// Gets the timestamp for this release of the library.
/// The timestamp is an integer that reads YYYYMMDD.
FIR_SYMBOL uint32_t fir_version_timestamp(void);

#endif
