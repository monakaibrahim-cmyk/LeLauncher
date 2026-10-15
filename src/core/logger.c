#include <core/logger.h>
#include <core/Version.h>
#include <Globals.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <stdatomic.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
    #include <strings.h>
    #include <sys/time.h>
    #include <sys/stat.h>
    #include <pthread.h>
#endif

typedef struct {
    LogLevel level;
    char     timestamp[32];
    char     system[32];
    char     file_line[64];
    char     function[64];
    char     message[1024];
} LogEntry;

/* ==============================================================================
 * Platform Abstractions (Pure C23 / POSIX / Win32)
 * ============================================================================== */
#if defined(_WIN32)
    #define MUTEX_TYPE            CRITICAL_SECTION
    #define MUTEX_INIT(m)         InitializeCriticalSection(m)
    #define MUTEX_LOCK(m)         EnterCriticalSection(m)
    #define MUTEX_UNLOCK(m)       LeaveCriticalSection(m)
    #define MUTEX_CLEAR(m)        DeleteCriticalSection(m)

    #define COND_TYPE             CONDITION_VARIABLE
    #define COND_INIT(c)          InitializeConditionVariable(c)
    #define COND_WAIT(c, m)       SleepConditionVariableCS(c, m, INFINITE)
    #define COND_SIGNAL(c)        WakeConditionVariable(c)
    #define COND_CLEAR(c)         ((void)0)

    #define ATOMIC_INT_TYPE       atomic_int
    #define ATOMIC_GET(v)         atomic_load(&(v))
    #define ATOMIC_SET(v, val)    atomic_store(&(v), (val))

    #define STRICMP(a, b)         _stricmp((a), (b))
    #define THREAD_USLEEP(us)     Sleep(((us) + 999) / 1000)
#else
    #define MUTEX_TYPE            pthread_mutex_t
    #define MUTEX_INIT(m)         pthread_mutex_init(m, NULL)
    #define MUTEX_LOCK(m)         pthread_mutex_lock(m)
    #define MUTEX_UNLOCK(m)       pthread_mutex_unlock(m)
    #define MUTEX_CLEAR(m)        pthread_mutex_destroy(m)

    #define COND_TYPE             pthread_cond_t
    #define COND_INIT(c)          pthread_cond_init(c, NULL)
    #define COND_WAIT(c, m)       pthread_cond_wait(c, m)
    #define COND_SIGNAL(c)        pthread_cond_signal(c)
    #define COND_CLEAR(c)         pthread_cond_destroy(c)

    #define ATOMIC_INT_TYPE       atomic_int
    #define ATOMIC_GET(v)         atomic_load(&(v))
    #define ATOMIC_SET(v, val)    atomic_store(&(v), (val))

    #define STRICMP(a, b)         strcasecmp((a), (b))
    #define THREAD_USLEEP(us)     usleep(us)
#endif

typedef struct LogNode {
    LogEntry       *entry;
    struct LogNode *next;
} LogNode;

typedef struct {
    LogNode *head;
    LogNode *tail;
    size_t   length;
} LogQueue;

static LogQueue *__queue_new__(void) {
    return (LogQueue *)calloc(1, sizeof(LogQueue));
}

static void __queue_push_tail__(LogQueue *q, LogEntry *entry) {
    if (!q || !entry) return;
    LogNode *node = (LogNode *)malloc(sizeof(LogNode));
    node->entry = entry;
    node->next = NULL;
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }
    q->length++;
}

static LogEntry *__queue_pop_head__(LogQueue *q) {
    if (!q || !q->head) return NULL;
    LogNode *node = q->head;
    LogEntry *entry = node->entry;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    free(node);
    q->length--;
    return entry;
}

static bool __queue_is_empty__(LogQueue *q) {
    return (!q || q->head == NULL);
}

static void __queue_free__(LogQueue *q) {
    if (!q) return;
    LogEntry *e;
    while ((e = __queue_pop_head__(q)) != NULL) {
        free(e);
    }
    free(q);
}

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
static ATOMIC_INT_TYPE g_current_level = (ATOMIC_INT_TYPE)LOG_LEVEL_TRACE;
#else
static ATOMIC_INT_TYPE g_current_level = (ATOMIC_INT_TYPE)LOG_LEVEL_INFO;
#endif

static char            g_log_file_path[512] = "Logs/LeLauncher.log";
static FILE           *g_log_file = NULL;
static MUTEX_TYPE      g_file_mutex;
static LogQueue       *g_log_queue = NULL;

#if defined(_WIN32)
static HANDLE          g_worker_thread = NULL;
#else
static pthread_t       g_worker_thread = 0;
#endif

static MUTEX_TYPE      g_log_mutex;
static COND_TYPE       g_log_cond;
static bool            g_running = false;
static bool            g_initialized = false;
static bool            g_shut_down = false;

static void __ensure_parent_dir__(const char *filepath) {
    if (!filepath || filepath[0] == '\0') return;
    char dir[512];
    strncpy(dir, filepath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *p = strrchr(dir, '/');
#if defined(_WIN32)
    char *p2 = strrchr(dir, '\\');
    if (p2 && (!p || p2 > p)) p = p2;
#endif
    if (!p) return;
    *p = '\0';
    if (dir[0] == '\0' || strcmp(dir, ".") == 0) return;
#if defined(_WIN32)
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0755);
#endif
}

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
static ATOMIC_INT_TYPE g_log_include_file_line = 1;
#else
static ATOMIC_INT_TYPE g_log_include_file_line = 0;
#endif

static LogUiCallback   g_ui_callback = NULL;
static void           *g_ui_user_data = NULL;
static MUTEX_TYPE      g_ui_mutex;

static const char *__file_basename__(const char *path) {
    if (!path || path[0] == '\0') return NULL;
    const char *p = strrchr(path, '/');
#if defined(_WIN32)
    const char *p2 = strrchr(path, '\\');
    if (p2 && (!p || p2 > p)) p = p2;
#endif
    return p ? p + 1 : path;
}

static const char *__infer_system__(const char *system, const char *file) {
    if (system && system[0] != '\0') {
        return system;
    }
    if (system && system[0] == '\0') {
        return NULL;
    }
    if (!file) return NULL;

    if (strstr(file, "src/core/") || strstr(file, "/core/") || strstr(file, "include/core/")) {
        return "core";
    }
    if (strstr(file, "src/network/") || strstr(file, "/network/") || strstr(file, "include/network/")) {
        return "network";
    }
    if (strstr(file, "src/ui/") || strstr(file, "/ui/") || strstr(file, "include/ui/")) {
        return "ui";
    }
    return NULL;
}

static const char *__level_to_string__(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_TRACE: return "TRACE";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default:              return "LOG";
    }
}

static const char *__level_to_color__(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_TRACE: return "\033[90m";
        case LOG_LEVEL_DEBUG: return "\033[36m";
        case LOG_LEVEL_INFO:  return "\033[32m";
        case LOG_LEVEL_WARN:  return "\033[33m";
        case LOG_LEVEL_ERROR: return "\033[31m";
        case LOG_LEVEL_FATAL: return "\033[41;1;37m";
        default:              return "\033[0m";
    }
}

static void __get_current_timestamp__(char *buf, size_t max_len) {
#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, max_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, (int)st.wMilliseconds);
#else
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int millis = (int)(tv.tv_usec / 1000);
    snprintf(buf, max_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, millis);
#endif
}

static void __format_entry__(const LogEntry *e, char *out, size_t max_len) {
    const char *lvl_str = __level_to_string__(e->level);
    bool has_system = (e->system[0] != '\0');
    bool show_file_line = (ATOMIC_GET(g_log_include_file_line) != 0 && e->file_line[0] != '\0');

    if (has_system && show_file_line) {
        snprintf(out, max_len, "%s [%s] [%s] [%s] [%s] : %s",
                 e->timestamp, lvl_str, e->system, e->file_line, e->function, e->message);
    } else if (has_system && !show_file_line) {
        snprintf(out, max_len, "%s [%s] [%s] [%s] : %s",
                 e->timestamp, lvl_str, e->system, e->function, e->message);
    } else if (!has_system && show_file_line) {
        snprintf(out, max_len, "%s [%s] [%s] [%s] : %s",
                 e->timestamp, lvl_str, e->file_line, e->function, e->message);
    } else {
        snprintf(out, max_len, "%s [%s] [%s] : %s",
                 e->timestamp, lvl_str, e->function, e->message);
    }
}

#if defined(_WIN32)
static DWORD WINAPI __logger_worker__(LPVOID data)
#else
static void *__logger_worker__(void *data)
#endif
{
    (void)data;

    while (true) {
        MUTEX_LOCK(&g_log_mutex);
        while (g_running && __queue_is_empty__(g_log_queue)) {
            COND_WAIT(&g_log_cond, &g_log_mutex);
        }
        if (!g_running && __queue_is_empty__(g_log_queue)) {
            MUTEX_UNLOCK(&g_log_mutex);
            break;
        }
        LogQueue local_queue = {0};
        while (!__queue_is_empty__(g_log_queue)) {
            __queue_push_tail__(&local_queue, __queue_pop_head__(g_log_queue));
        }
        MUTEX_UNLOCK(&g_log_mutex);

        LogEntry *entry = NULL;
        bool has_warn_or_error = false;

        while ((entry = __queue_pop_head__(&local_queue)) != NULL) {
            const char *lvl_str = __level_to_string__(entry->level);
            const char *color   = __level_to_color__(entry->level);
            const char *reset   = "\033[0m";
            bool has_system     = (entry->system[0] != '\0');
            bool show_file_line = (ATOMIC_GET(g_log_include_file_line) != 0 && entry->file_line[0] != '\0');

            /* Terminal output */
            FILE *stream = (entry->level >= LOG_LEVEL_WARN) ? stderr : stdout;
            if (has_system && show_file_line) {
                fprintf(stream, "%s [%s%s%s] [%s] [%s] [%s] : %s\n",
                        entry->timestamp, color, lvl_str, reset,
                        entry->system, entry->file_line, entry->function, entry->message);
            } else if (has_system && !show_file_line) {
                fprintf(stream, "%s [%s%s%s] [%s] [%s] : %s\n",
                        entry->timestamp, color, lvl_str, reset,
                        entry->system, entry->function, entry->message);
            } else if (!has_system && show_file_line) {
                fprintf(stream, "%s [%s%s%s] [%s] [%s] : %s\n",
                        entry->timestamp, color, lvl_str, reset,
                        entry->file_line, entry->function, entry->message);
            } else {
                fprintf(stream, "%s [%s%s%s] [%s] : %s\n",
                        entry->timestamp, color, lvl_str, reset,
                        entry->function, entry->message);
            }

            /* File output */
            MUTEX_LOCK(&g_file_mutex);
            if (g_log_file) {
                if (has_system && show_file_line) {
                    fprintf(g_log_file, "%s [%s] [%s] [%s] [%s] : %s\n",
                            entry->timestamp, lvl_str, entry->system,
                            entry->file_line, entry->function, entry->message);
                } else if (has_system && !show_file_line) {
                    fprintf(g_log_file, "%s [%s] [%s] [%s] : %s\n",
                            entry->timestamp, lvl_str, entry->system,
                            entry->function, entry->message);
                } else if (!has_system && show_file_line) {
                    fprintf(g_log_file, "%s [%s] [%s] [%s] : %s\n",
                            entry->timestamp, lvl_str,
                            entry->file_line, entry->function, entry->message);
                } else {
                    fprintf(g_log_file, "%s [%s] [%s] : %s\n",
                            entry->timestamp, lvl_str,
                            entry->function, entry->message);
                }
            }

            if (entry->level >= LOG_LEVEL_WARN) {
                fflush(stream);
                if (g_log_file) fflush(g_log_file);
                has_warn_or_error = true;
            }
            MUTEX_UNLOCK(&g_file_mutex);

            /* UI dispatch directly */
            MUTEX_LOCK(&g_ui_mutex);
            if (g_ui_callback) {
                char formatted[1280];
                __format_entry__(entry, formatted, sizeof(formatted));
                g_ui_callback(entry->level, entry->system, formatted, g_ui_user_data);
            }
            MUTEX_UNLOCK(&g_ui_mutex);

            free(entry);
        }

        /* Flush output streams once per batch if not already flushed by a warning */
        if (!has_warn_or_error) {
            fflush(stdout);
            MUTEX_LOCK(&g_file_mutex);
            if (g_log_file) {
                fflush(g_log_file);
            }
            MUTEX_UNLOCK(&g_file_mutex);
        }
    }

#if !defined(_WIN32)
    return NULL;
#else
    return 0;
#endif
}

void __logger_init__(void) {
    if (g_initialized) return;

    MUTEX_INIT(&g_log_mutex);
    COND_INIT(&g_log_cond);
    MUTEX_INIT(&g_ui_mutex);
    MUTEX_INIT(&g_file_mutex);

    g_log_queue = __queue_new__();
    g_running = true;

    const char *env_path = getenv("LELAUNCHER_LOG_FILE");
    if (env_path && strlen(env_path) > 0) {
        strncpy(g_log_file_path, env_path, sizeof(g_log_file_path) - 1);
    }

    const char *env_level = getenv("LELAUNCHER_LOG_LEVEL");
    if (env_level) {
        if (STRICMP(env_level, "TRACE") == 0) {
            ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)LOG_LEVEL_TRACE);
        } else if (STRICMP(env_level, "DEBUG") == 0) {
            ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)LOG_LEVEL_DEBUG);
        } else if (STRICMP(env_level, "INFO") == 0) {
            ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)LOG_LEVEL_INFO);
        } else if (STRICMP(env_level, "WARN") == 0 || STRICMP(env_level, "WARNING") == 0) {
            ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)LOG_LEVEL_WARN);
        } else if (STRICMP(env_level, "ERROR") == 0) {
            ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)LOG_LEVEL_ERROR);
        } else if (STRICMP(env_level, "FATAL") == 0) {
            ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)LOG_LEVEL_FATAL);
        }
    }

    const char *env_file_line = getenv("LELAUNCHER_LOG_FILE_LINE");
    if (env_file_line) {
        if (strcmp(env_file_line, "1") == 0 || STRICMP(env_file_line, "true") == 0) {
            ATOMIC_SET(g_log_include_file_line, 1);
        } else if (strcmp(env_file_line, "0") == 0 || STRICMP(env_file_line, "false") == 0) {
            ATOMIC_SET(g_log_include_file_line, 0);
        }
    }

    __ensure_parent_dir__(g_log_file_path);
    g_log_file = fopen(g_log_file_path, "a");

#if defined(_WIN32)
    g_worker_thread = CreateThread(NULL, 0, __logger_worker__, NULL, 0, NULL);
#else
    pthread_create(&g_worker_thread, NULL, __logger_worker__, NULL);
#endif
    g_initialized = true;

    LOG_INFO("core", "%s v%s (%s) | Built: %s %s | Compiler: %s",
             APP_NAME, APP_VERSION_STRING, BUILD_CONFIG, BUILD_DATE, BUILD_TIME, COMPILER_INFO);
    LOG_INFO("core", "Threaded logger initialized (%s)", g_log_file_path);
}

void __logger_shutdown__(void) {
    if (!g_initialized) return;

    LOG_INFO("core", "Shutting down logger worker...");

    MUTEX_LOCK(&g_log_mutex);
    g_running = false;
    COND_SIGNAL(&g_log_cond);
    MUTEX_UNLOCK(&g_log_mutex);

#if defined(_WIN32)
    if (g_worker_thread) {
        WaitForSingleObject(g_worker_thread, INFINITE);
        CloseHandle(g_worker_thread);
        g_worker_thread = NULL;
    }
#else
    if (g_worker_thread) {
        pthread_join(g_worker_thread, NULL);
        g_worker_thread = 0;
    }
#endif

    MUTEX_LOCK(&g_file_mutex);
    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    MUTEX_UNLOCK(&g_file_mutex);

    if (g_log_queue) {
        __queue_free__(g_log_queue);
        g_log_queue = NULL;
    }

    MUTEX_CLEAR(&g_log_mutex);
    COND_CLEAR(&g_log_cond);
    MUTEX_CLEAR(&g_ui_mutex);
    MUTEX_CLEAR(&g_file_mutex);

    g_initialized = false;
    g_shut_down = true;
}

void __logger_set_level__(LogLevel level) {
    ATOMIC_SET(g_current_level, (ATOMIC_INT_TYPE)level);
}

LogLevel __logger_get_level__(void) {
    return (LogLevel)ATOMIC_GET(g_current_level);
}

void __logger_set_file_path__(const char *path) {
    if (!path || strlen(path) == 0) return;

    MUTEX_LOCK(&g_file_mutex);
    strncpy(g_log_file_path, path, sizeof(g_log_file_path) - 1);
    g_log_file_path[sizeof(g_log_file_path) - 1] = '\0';

    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
    }
    __ensure_parent_dir__(g_log_file_path);
    g_log_file = fopen(g_log_file_path, "a");
    MUTEX_UNLOCK(&g_file_mutex);
}

const char *__logger_get_file_path__(void) {
    return g_log_file_path;
}

void __logger_set_ui_callback__(LogUiCallback callback, void *user_data) {
    MUTEX_LOCK(&g_ui_mutex);
    g_ui_callback = callback;
    g_ui_user_data = user_data;
    MUTEX_UNLOCK(&g_ui_mutex);
}

void __logger_set_include_file_line__(bool enable) {
    ATOMIC_SET(g_log_include_file_line, enable ? 1 : 0);
}

bool __logger_get_include_file_line__(void) {
    return ATOMIC_GET(g_log_include_file_line) != 0;
}

void __logger_log__(LogLevel level, const char *system, const char *file, int line, const char *func, const char *fmt, ...) {
    if (!g_initialized && !g_shut_down) {
        __logger_init__();
    }

    if ((int)level < (int)ATOMIC_GET(g_current_level)) return;

    const char *resolved_system = __infer_system__(system, file);
    const char *base_file = __file_basename__(file);
    const char *fn_name = (func && strlen(func) > 0) ? func : "unknown";

    if (g_shut_down) {
        char msg[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        char ts[32];
        __get_current_timestamp__(ts, sizeof(ts));
        const char *lvl_str = __level_to_string__(level);
        const char *color   = __level_to_color__(level);
        const char *reset   = "\033[0m";
        FILE *stream = (level >= LOG_LEVEL_WARN) ? stderr : stdout;

        bool has_system     = (resolved_system && resolved_system[0] != '\0');
        bool show_file_line = (ATOMIC_GET(g_log_include_file_line) != 0 && base_file && line > 0);

        if (has_system && show_file_line) {
            fprintf(stream, "%s [%s%s%s] [%s] [%s:%d] [%s] : %s\n",
                    ts, color, lvl_str, reset, resolved_system, base_file, line, fn_name, msg);
        } else if (has_system && !show_file_line) {
            fprintf(stream, "%s [%s%s%s] [%s] [%s] : %s\n",
                    ts, color, lvl_str, reset, resolved_system, fn_name, msg);
        } else if (!has_system && show_file_line) {
            fprintf(stream, "%s [%s%s%s] [%s:%d] [%s] : %s\n",
                    ts, color, lvl_str, reset, base_file, line, fn_name, msg);
        } else {
            fprintf(stream, "%s [%s%s%s] [%s] : %s\n",
                    ts, color, lvl_str, reset, fn_name, msg);
        }
        fflush(stream);
        return;
    }

    LogEntry *entry = (LogEntry *)calloc(1, sizeof(LogEntry));
    if (!entry) return;

    entry->level = level;
    __get_current_timestamp__(entry->timestamp, sizeof(entry->timestamp));

    if (resolved_system && resolved_system[0] != '\0') {
        strncpy(entry->system, resolved_system, sizeof(entry->system) - 1);
    } else {
        entry->system[0] = '\0';
    }

    if (base_file && line > 0) {
        snprintf(entry->file_line, sizeof(entry->file_line), "%s:%d", base_file, line);
    } else {
        entry->file_line[0] = '\0';
    }
    strncpy(entry->function, fn_name, sizeof(entry->function) - 1);

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
    va_end(args);

    MUTEX_LOCK(&g_log_mutex);
    if (g_log_queue->length > 2000) {
        LogEntry *dropped = __queue_pop_head__(g_log_queue);
        free(dropped);
    }
    __queue_push_tail__(g_log_queue, entry);
    COND_SIGNAL(&g_log_cond);
    MUTEX_UNLOCK(&g_log_mutex);
}

void __logger_flush__(void) {
    if (!g_initialized) return;

    while (true) {
        MUTEX_LOCK(&g_log_mutex);
        bool empty = __queue_is_empty__(g_log_queue);
        MUTEX_UNLOCK(&g_log_mutex);

        if (empty) break;
        THREAD_USLEEP(1000);
    }

    MUTEX_LOCK(&g_file_mutex);
    if (g_log_file) {
        fflush(g_log_file);
    }
    MUTEX_UNLOCK(&g_file_mutex);
}

INITIALIZER(__logger_auto_init__) {
    __logger_init__();
}

DESTRUCTOR(__logger_auto_cleanup__) {
    __logger_shutdown__();
}
