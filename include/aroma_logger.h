/*
 Copyright (c) 2025 Yassine Ahmed Ali

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file aroma_logger.h
 * @brief Logging and debugging utilities.
 *
 * Provides a standardized logging interface with multiple severity levels (INFO, WARNING, ERROR, CRITICAL),
 * helper macros for calling them with file/line context, and utilities for debugging like memory dumping.
 */

#ifndef AROMA_LOGGER_H
#define AROMA_LOGGER_H

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ANSI color codes for colored terminal output
#define KNRM "\x1B[0m"  /**< Reset color */
#define KRED "\x1B[31m" /**< Red color for errors */
#define KGRN "\x1B[32m" /**< Green color for success */
#define KYEL "\x1B[33m" /**< Yellow color for warnings */
#define KBLU "\x1B[34m" /**< Blue color for informational messages */
#define KMAG "\x1B[35m" /**< Magenta color for special messages */
#define KCYN "\x1B[36m" /**< Cyan color for debug messages */
#define KWHT "\x1B[37m" /**< White color for general text */

/** @brief Logging severity levels. */
typedef enum
{
    DEBUG_LEVEL_INFO,    /**< Informational messages. */
    DEBUG_LEVEL_WARNING, /**< Warnings (potential issues). */
    DEBUG_LEVEL_ERROR,   /**< Errors (recoverable problems). */
    DEBUG_LEVEL_CRITICAL /**< Critical failures (likely non-recoverable). */
} DebugLevel;


#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "AromaUI"
#define LOG_INFO(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOG_WARNING(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOG_ERROR(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))
#define LOG_CRITICAL(...) ((void)__android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__))
#else
/**
 * @brief Internal macro to route log calls with context.
 * 
 * Do not call this directly; use LOG_INFO, LOG_ERROR, etc.
 */
#define LOG_MESSAGE(level, fmt, ...) \
    log_message(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/** @brief Log an informational message. */
#define LOG_INFO(fmt, ...)     LOG_MESSAGE(DEBUG_LEVEL_INFO, fmt, ##__VA_ARGS__)
/** @brief Log a warning message. */
#define LOG_WARNING(fmt, ...)  LOG_MESSAGE(DEBUG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
/** @brief Log an error message. */
#define LOG_ERROR(fmt, ...)    LOG_MESSAGE(DEBUG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
/** @brief Log a critical error message. */
#define LOG_CRITICAL(fmt, ...) LOG_MESSAGE(DEBUG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)
#endif
/**
 * @brief Core logging function.
 * 
 * @param level Severity level.
 * @param file Source file name.
 * @param line Source line number.
 * @param func Calling function name.
 * @param fmt Printf-style format string.
 * @param ... Format arguments.
 */
void log_message(DebugLevel level, const char *file, int line, const char *func, const char *fmt, ...);

/**
 * @brief Log a performance milestone timestamp.
 * @param message Description of the checkpoint.
 */
void log_performance(char *message);

/** @brief Macro for performance logging. */
#define LOG_PERFORMANCE(message) log_performance(message)

/**
 * @brief Enable or disable all logging output.
 * @param enabled True to enable, false to disable.
 */
void set_logging_enabled(bool enabled);

/**
 * @brief Set the minimum severity level to log.
 * @param level Minimum level (messages below this are ignored).
 */
void set_minimum_log_level(DebugLevel level);

/**
 * @brief Print the current call stack trace to the log.
 */
void print_stack_trace(void);

/**
 * @brief Dump a memory region to the log in hex format.
 * @param label A label for this dump.
 * @param buffer Pointer to the memory buffer.
 * @param size Number of bytes to dump.
 */
void dump_memory(const char *label, const void *buffer, size_t size);

/**
 * @brief Save the current log buffer/session to a file.
 * @param path File path to save the log to.
 */
void save_log_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif // AROMA_LOGGER_H

