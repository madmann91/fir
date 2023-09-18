#ifndef FIR_PLATFORM_H
#define FIR_PLATFORM_H

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

#endif
