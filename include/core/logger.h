#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <Globals.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

typedef void (*LogUiCallback)(LogLevel level, const char *system, const char *formatted_line, void *user_data);

/* Core logger routines */
void __logger_init__(void);
void __logger_shutdown__(void);
void __logger_set_level__(LogLevel level);
LogLevel __logger_get_level__(void);
void __logger_set_file_path__(const char *path);
const char *__logger_get_file_path__(void);
void __logger_set_ui_callback__(LogUiCallback callback, void *user_data);
void __logger_flush__(void);
void __logger_set_include_file_line__(bool enable);
bool __logger_get_include_file_line__(void);

/* Asynchronous log dispatch with context */
void __logger_log__(LogLevel level, const char *system, const char *file, int line, const char *func, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

/* Logging macros */
#define LOG_TRACE(system, ...)                                          __logger_log__(LOG_LEVEL_TRACE, system, __FILE__, __LINE__, __FUNCTION_NAME__, __VA_ARGS__)
#define LOG_DEBUG(system, ...)                                          __logger_log__(LOG_LEVEL_DEBUG, system, __FILE__, __LINE__, __FUNCTION_NAME__, __VA_ARGS__)
#define LOG_INFO(system, ...)                                           __logger_log__(LOG_LEVEL_INFO,  system, __FILE__, __LINE__, __FUNCTION_NAME__, __VA_ARGS__)
#define LOG_WARN(system, ...)                                           __logger_log__(LOG_LEVEL_WARN,  system, __FILE__, __LINE__, __FUNCTION_NAME__, __VA_ARGS__)
#define LOG_ERROR(system, ...)                                          __logger_log__(LOG_LEVEL_ERROR, system, __FILE__, __LINE__, __FUNCTION_NAME__, __VA_ARGS__)
#define LOG_FATAL(system, ...)                                          __logger_log__(LOG_LEVEL_FATAL, system, __FILE__, __LINE__, __FUNCTION_NAME__, __VA_ARGS__)

typedef struct {
    const char *func;
    const char *file;
    int         line;
    const char *system;
} TraceScopeContext;

static inline __attribute__((unused)) void __logger_scope_cleanup(TraceScopeContext *scope) {
    if (scope && scope->func) {
        __logger_log__(LOG_LEVEL_TRACE, scope->system, scope->file, scope->line, scope->func, "[EXIT]  %s()", scope->func);
    }
}

/* Scoped function tracing */
#define TRACE_SCOPE()                                                   \
    __logger_log__(LOG_LEVEL_TRACE, NULL, __FILE__, __LINE__, __FUNCTION_NAME__, "[ENTER] %s()", __FUNCTION_NAME__); \
    TraceScopeContext __trace_ctx __attribute__((cleanup(__logger_scope_cleanup), unused)) = { __FUNCTION_NAME__, __FILE__, __LINE__, NULL }

#define TRACE_SCOPE_NAMED(label)                                        \
    __logger_log__(LOG_LEVEL_TRACE, NULL, __FILE__, __LINE__, (label), "[ENTER] %s()", (label)); \
    TraceScopeContext __trace_ctx __attribute__((cleanup(__logger_scope_cleanup), unused)) = { (label), __FILE__, __LINE__, NULL }

#define LOG_AUTO(level, system, fmt, ...)                               \
    __logger_log__(level, system, __FILE__, __LINE__, __FUNCTION_NAME__, fmt, ##__VA_ARGS__)

#define LOG_EXPR(system, expr, fmt) ({                                  \
    __auto_type __res = (expr);                                         \
    __logger_log__(LOG_LEVEL_TRACE, system, __FILE__, __LINE__, __FUNCTION_NAME__, "%s() returned " fmt, #expr, __res); \
    __res;                                                              \
})

#endif /* __LOGGER_H__ */
