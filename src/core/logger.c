#include <core/logger.h>
#include <core/Version.h>
#include <Globals.h>
#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#if !defined(_WIN32)
#include <sys/time.h>
#endif

typedef struct {
    LogLevel level;
    char     timestamp[32];
    char     system[32];
    char     file_line[64];
    char     function[64];
    char     message[1024];
} LogEntry;

typedef struct {
    LogUiCallback callback;
    LogLevel      level;
    char          system[32];
    char          formatted[1280];
    void          *user_data;
} UiLogDispatch;

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
static gint            g_current_level = (gint)LOG_LEVEL_TRACE;
#else
static gint            g_current_level = (gint)LOG_LEVEL_INFO;
#endif
static char            g_log_file_path[512] = "Logs/LeLauncher.log";
static FILE           *g_log_file = NULL;
static GMutex          g_file_mutex;
static GQueue         *g_log_queue = NULL;
static GMutex          g_log_mutex;
static GCond           g_log_cond;
static GThread        *g_worker_thread = NULL;
static bool            g_running = false;
static bool            g_initialized = false;
static bool            g_shut_down = false;

static void __ensure_parent_dir__(const char *filepath) {
    if (!filepath || filepath[0] == '\0') return;
    char *dir = g_path_get_dirname(filepath);
    if (dir) {
        if (strcmp(dir, ".") != 0 && strcmp(dir, "/") != 0) {
            g_mkdir_with_parents(dir, 0755);
        }
        g_free(dir);
    }
}

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
static gint            g_log_include_file_line = 1;
#else
static gint            g_log_include_file_line = 0;
#endif

static LogUiCallback   g_ui_callback = NULL;
static void           *g_ui_user_data = NULL;
static GMutex          g_ui_mutex;

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
    time_t now = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif

    int millis = 0;
#if defined(_WIN32)
    millis = (int)(GetTickCount() % 1000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    millis = (int)(tv.tv_usec / 1000);
#endif

    snprintf(buf, max_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, millis);
}

static void __format_entry__(const LogEntry *e, char *out, size_t max_len) {
    const char *lvl_str = __level_to_string__(e->level);
    bool has_system = (e->system[0] != '\0');
    bool show_file_line = (g_atomic_int_get(&g_log_include_file_line) != 0 && e->file_line[0] != '\0');

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

static gboolean __dispatch_ui_idle__(gpointer data) {
    UiLogDispatch *d = (UiLogDispatch *)data;
    if (d && d->callback) {
        d->callback(d->level, d->system, d->formatted, d->user_data);
    }
    g_free(d);
    return G_SOURCE_REMOVE;
}

static gpointer __logger_worker__(gpointer data) {
    (void)data;

    while (true) {
        g_mutex_lock(&g_log_mutex);
        while (g_running && g_queue_is_empty(g_log_queue)) {
            g_cond_wait(&g_log_cond, &g_log_mutex);
        }

        if (!g_running && g_queue_is_empty(g_log_queue)) {
            g_mutex_unlock(&g_log_mutex);
            break;
        }

        GQueue local_queue = G_QUEUE_INIT;
        while (!g_queue_is_empty(g_log_queue)) {
            g_queue_push_tail(&local_queue, g_queue_pop_head(g_log_queue));
        }
        g_mutex_unlock(&g_log_mutex);

        LogEntry *entry = NULL;
        bool has_warn_or_error = false;

        while ((entry = (LogEntry *)g_queue_pop_head(&local_queue)) != NULL) {
            const char *lvl_str = __level_to_string__(entry->level);
            const char *color   = __level_to_color__(entry->level);
            const char *reset   = "\033[0m";
            bool has_system     = (entry->system[0] != '\0');
            bool show_file_line = (g_atomic_int_get(&g_log_include_file_line) != 0 && entry->file_line[0] != '\0');

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
            g_mutex_lock(&g_file_mutex);
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
            g_mutex_unlock(&g_file_mutex);

            /* UI main loop dispatch */
            g_mutex_lock(&g_ui_mutex);
            if (g_ui_callback) {
                UiLogDispatch *disp = g_new0(UiLogDispatch, 1);
                disp->callback = g_ui_callback;
                disp->level = entry->level;
                strncpy(disp->system, entry->system, sizeof(disp->system) - 1);
                __format_entry__(entry, disp->formatted, sizeof(disp->formatted));
                disp->user_data = g_ui_user_data;

                g_idle_add(__dispatch_ui_idle__, disp);
            }
            g_mutex_unlock(&g_ui_mutex);

            g_free(entry);
        }

        /* Flush output streams once per batch if not already flushed by a warning */
        if (!has_warn_or_error) {
            fflush(stdout);
            g_mutex_lock(&g_file_mutex);
            if (g_log_file) {
                fflush(g_log_file);
            }
            g_mutex_unlock(&g_file_mutex);
        }
    }

    return NULL;
}

void __logger_init__(void) {
    if (g_initialized) return;

    g_mutex_init(&g_log_mutex);
    g_cond_init(&g_log_cond);
    g_mutex_init(&g_ui_mutex);
    g_mutex_init(&g_file_mutex);

    g_log_queue = g_queue_new();
    g_running = true;

    const char *env_path = getenv("LELAUNCHER_LOG_FILE");
    if (env_path && strlen(env_path) > 0) {
        strncpy(g_log_file_path, env_path, sizeof(g_log_file_path) - 1);
    }

    const char *env_level = getenv("LELAUNCHER_LOG_LEVEL");
    if (env_level) {
        if (g_ascii_strcasecmp(env_level, "TRACE") == 0) {
            g_atomic_int_set(&g_current_level, (gint)LOG_LEVEL_TRACE);
        } else if (g_ascii_strcasecmp(env_level, "DEBUG") == 0) {
            g_atomic_int_set(&g_current_level, (gint)LOG_LEVEL_DEBUG);
        } else if (g_ascii_strcasecmp(env_level, "INFO") == 0) {
            g_atomic_int_set(&g_current_level, (gint)LOG_LEVEL_INFO);
        } else if (g_ascii_strcasecmp(env_level, "WARN") == 0 || g_ascii_strcasecmp(env_level, "WARNING") == 0) {
            g_atomic_int_set(&g_current_level, (gint)LOG_LEVEL_WARN);
        } else if (g_ascii_strcasecmp(env_level, "ERROR") == 0) {
            g_atomic_int_set(&g_current_level, (gint)LOG_LEVEL_ERROR);
        } else if (g_ascii_strcasecmp(env_level, "FATAL") == 0) {
            g_atomic_int_set(&g_current_level, (gint)LOG_LEVEL_FATAL);
        }
    }

    const char *env_file_line = getenv("LELAUNCHER_LOG_FILE_LINE");
    if (env_file_line) {
        if (strcmp(env_file_line, "1") == 0 || g_ascii_strcasecmp(env_file_line, "true") == 0) {
            g_atomic_int_set(&g_log_include_file_line, 1);
        } else if (strcmp(env_file_line, "0") == 0 || g_ascii_strcasecmp(env_file_line, "false") == 0) {
            g_atomic_int_set(&g_log_include_file_line, 0);
        }
    }

    __ensure_parent_dir__(g_log_file_path);
    g_log_file = fopen(g_log_file_path, "a");

    g_worker_thread = g_thread_new("logger_worker", __logger_worker__, NULL);
    g_initialized = true;

    LOG_INFO("core", "%s v%s (%s) | Built: %s %s | Compiler: %s",
             APP_NAME, APP_VERSION_STRING, BUILD_CONFIG, BUILD_DATE, BUILD_TIME, COMPILER_INFO);
    LOG_INFO("core", "Threaded logger initialized (%s)", g_log_file_path);
}

void __logger_shutdown__(void) {
    if (!g_initialized) return;

    LOG_INFO("core", "Shutting down logger worker...");

    g_mutex_lock(&g_log_mutex);
    g_running = false;
    g_cond_signal(&g_log_cond);
    g_mutex_unlock(&g_log_mutex);

    if (g_worker_thread) {
        g_thread_join(g_worker_thread);
        g_worker_thread = NULL;
    }

    g_mutex_lock(&g_file_mutex);
    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_mutex_unlock(&g_file_mutex);

    if (g_log_queue) {
        LogEntry *entry = NULL;
        while ((entry = (LogEntry *)g_queue_pop_head(g_log_queue)) != NULL) {
            g_free(entry);
        }
        g_queue_free(g_log_queue);
        g_log_queue = NULL;
    }

    g_mutex_clear(&g_log_mutex);
    g_cond_clear(&g_log_cond);
    g_mutex_clear(&g_ui_mutex);
    g_mutex_clear(&g_file_mutex);

    g_initialized = false;
    g_shut_down = true;
}

void __logger_set_level__(LogLevel level) {
    g_atomic_int_set(&g_current_level, (gint)level);
}

LogLevel __logger_get_level__(void) {
    return (LogLevel)g_atomic_int_get(&g_current_level);
}

void __logger_set_file_path__(const char *path) {
    if (!path || strlen(path) == 0) return;

    g_mutex_lock(&g_file_mutex);
    strncpy(g_log_file_path, path, sizeof(g_log_file_path) - 1);
    g_log_file_path[sizeof(g_log_file_path) - 1] = '\0';

    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
    }
    __ensure_parent_dir__(g_log_file_path);
    g_log_file = fopen(g_log_file_path, "a");
    g_mutex_unlock(&g_file_mutex);
}

const char *__logger_get_file_path__(void) {
    return g_log_file_path;
}

void __logger_set_ui_callback__(LogUiCallback callback, void *user_data) {
    g_mutex_lock(&g_ui_mutex);
    g_ui_callback = callback;
    g_ui_user_data = user_data;
    g_mutex_unlock(&g_ui_mutex);
}

void __logger_set_include_file_line__(bool enable) {
    g_atomic_int_set(&g_log_include_file_line, enable ? 1 : 0);
}

bool __logger_get_include_file_line__(void) {
    return g_atomic_int_get(&g_log_include_file_line) != 0;
}

void __logger_log__(LogLevel level, const char *system, const char *file, int line, const char *func, const char *fmt, ...) {
    if (!g_initialized && !g_shut_down) {
        __logger_init__();
    }

    if ((gint)level < g_atomic_int_get(&g_current_level)) return;

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
        bool show_file_line = (g_atomic_int_get(&g_log_include_file_line) != 0 && base_file && line > 0);

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

    LogEntry *entry = g_new0(LogEntry, 1);
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

    g_mutex_lock(&g_log_mutex);
    if (g_queue_get_length(g_log_queue) > 2000) {
        LogEntry *dropped = (LogEntry *)g_queue_pop_head(g_log_queue);
        g_free(dropped);
    }
    g_queue_push_tail(g_log_queue, entry);
    g_cond_signal(&g_log_cond);
    g_mutex_unlock(&g_log_mutex);
}

void __logger_flush__(void) {
    if (!g_initialized) return;

    while (true) {
        g_mutex_lock(&g_log_mutex);
        bool empty = g_queue_is_empty(g_log_queue);
        g_mutex_unlock(&g_log_mutex);

        if (empty) break;
        g_usleep(1000);
    }

    g_mutex_lock(&g_file_mutex);
    if (g_log_file) {
        fflush(g_log_file);
    }
    g_mutex_unlock(&g_file_mutex);
}

INITIALIZER(__logger_auto_init__) {
    __logger_init__();
}

DESTRUCTOR(__logger_auto_cleanup__) {
    __logger_shutdown__();
}
