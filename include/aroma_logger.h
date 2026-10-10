




























#ifndef AROMA_LOGGER_H
#define AROMA_LOGGER_H

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif


#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"


typedef enum
{
    DEBUG_LEVEL_INFO,
    DEBUG_LEVEL_WARNING,
    DEBUG_LEVEL_ERROR,
    DEBUG_LEVEL_CRITICAL
} DebugLevel;


#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "AromaUI"
#define LOG_INFO(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOG_WARNING(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOG_ERROR(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))
#define LOG_CRITICAL(...) ((void)__android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__))
#else





#define LOG_MESSAGE(level, fmt, ...) \
    log_message(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)


#define LOG_INFO(fmt, ...)     LOG_MESSAGE(DEBUG_LEVEL_INFO, fmt, ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...)  LOG_MESSAGE(DEBUG_LEVEL_WARNING, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...)    LOG_MESSAGE(DEBUG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#define LOG_CRITICAL(fmt, ...) LOG_MESSAGE(DEBUG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)
#endif










void log_message(DebugLevel level, const char *file, int line, const char *func, const char *fmt, ...);





void log_performance(char *message);


#define LOG_PERFORMANCE(message) log_performance(message)





void set_logging_enabled(bool enabled);





void set_minimum_log_level(DebugLevel level);




void print_stack_trace(void);







void dump_memory(const char *label, const void *buffer, size_t size);





void save_log_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif

