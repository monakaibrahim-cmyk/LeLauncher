#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include <Globals.h>
#include <downloader.h>
#include <core/logger.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>
#define curldl_strdup _strdup
#else
#include <sys/stat.h>
#include <time.h>
#define curldl_strdup strdup
#endif

#define SPEED_TAU                                                       3.0                     /* seconds: EMA smoothing time constant */
#define SAMPLE_MIN_DT                                                   0.2                     /* seconds: min interval between speed samples */
#define DEFAULT_PROGRESS_DT                                             0.1                     /* seconds: 10 Hz UI update rate */

static double __now_sec__(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency  = {0};
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }

    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/* UTF-8 aware 64-bit file size query; returns -1 if file does not exist */
static int64_t __get_existing_file_size__(const char *path) {
#ifdef _WIN32
    wchar_t wpath[MAX_PATH * 2];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) == 0) {
        return -1;
    }

    struct _stat64 st;

    if (_wstat64(wpath, &st) == 0 && (st.st_mode & _S_IFREG)) {
        return (int64_t)st.st_size;
    }

    return -1;
#else
    struct stat st;

    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        return (int64_t)st.st_size;
    }

    return -1;
#endif
}

/* UTF-8 aware file open */
static FILE *__open_file_utf8__(const char *path, int append) {
#ifdef _WIN32
    wchar_t wpath[MAX_PATH * 2];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) == 0) {
        errno = EINVAL;
        return NULL;
    }

    return _wfopen(wpath, append ? L"ab" : L"wb");
#else
    return fopen(path, append ? "ab" : "wb");
#endif
}

/* Extracts and URL-decodes the filename component from a URL.
 * Returns a heap-allocated string that the caller must free(), or NULL on failure. */
static char *__filename_from_url__(CURL *curl, const char *url) {
    if (!url || !*url) {
        return NULL;
    }

    const char *p                   = strstr(url, "://");
    p                               = p ? p + 3 : url;

    const char *path                = strchr(p, '/');

    if (!path) {
        return NULL;
    }

    const char *end                 = path;

    while (*end && *end != '?' && *end != '#') {
        end++;
    }

    while (end > path && *(end - 1) == '/') {
        end--;
    }

    if (end <= path) {
        return NULL;
    }

    const char *last_slash          = end - 1;

    while (last_slash >= path && *last_slash != '/') {
        last_slash--;
    }

    const char *segment             = last_slash + 1;
    size_t seg_len                  = (size_t)(end - segment);

    if (seg_len == 0) {
        return NULL;
    }

    char *result                    = NULL;

    if (curl) {
        int outlen                  = 0;
        char *unescaped             = curl_easy_unescape(curl, segment, (int)seg_len, &outlen);

        if (unescaped) {
            if (*unescaped && strcmp(unescaped, ".") != 0 && strcmp(unescaped, "..") != 0) {
                result              = curldl_strdup(unescaped);
            }

            curl_free(unescaped);
        }
    }

    if (!result) {
        char buf[1024];

        if (seg_len < sizeof(buf)) {
            memcpy(buf, segment, seg_len);
            buf[seg_len]            = '\0';

            if (strcmp(buf, ".") != 0 && strcmp(buf, "..") != 0) {
                result              = curldl_strdup(buf);
            }
        }
    }

    return result;
}

void __format_bytes__(double bytes, char *output, size_t size) {
    static const char *units[] = {
        "B",
        "KiB",
        "MiB",
        "GiB",
        "TiB",
        "PiB"
    };

    int i                           = 0;

    if (bytes < 0.0) {
        bytes                       = 0.0;
    }

    while (bytes >= 1024.0 && i < 5) {
        bytes                       /= 1024.0;
        i++;
    }

    if (i == 0) {
        snprintf(output, size, "%.0f %s", bytes, units[i]);
    } else {
        snprintf(output, size, "%.2f %s", bytes, units[i]);
    }
}

void __format_duration__(double seconds, char *output, size_t size) {
    if (!isfinite(seconds) || seconds < 0.0 || seconds > 99.0 * 3600.0) {
        snprintf(output, size, "--:--");
        return;
    }

    long t                          = (long)(seconds + 0.5);
    long h                          = t / 3600;
    long m                          = (t % 3600) / 60;
    long sec                        = t % 60;

    if (h) {
        snprintf(output, size, "%ld:%02ld:%02ld", h, m, sec);
    } else {
        snprintf(output, size, "%02ld:%02ld", m, sec);
    }
}

int64_t __parse_rate__(const char *data) {
    if (!data || !*data) {
        return -1;
    }

    char *end                       = NULL;
    double v                        = strtod(data, &end);

    if (end == data || v < 0.0) {
        return -1;
    }

    switch (*end) {
    case '\0':                                                              break;
    case 'k': case 'K':                 v *= 1024.0;                        break;
    case 'm': case 'M':                 v *= 1024.0 * 1024.0;               break;
    case 'g': case 'G':                 v *= 1024.0 * 1024.0 * 1024.0;      break;
    default:                                                                return -1;
    }

    return (int64_t)v;
}

typedef struct {
    CURL                                                                    *curl;
    const CURLOptions                                                       *options;
    const char                                                              *path;
    FILE                                                                    *fp;
    int                                                                     opened;
    int                                                                     io_errno;

    curl_off_t                                                              resume_from;
    curl_off_t                                                              offset;
    curl_off_t                                                              total;
    curl_off_t                                                              session_bytes;

    int                                                                     resolved;
    long                                                                    http_code;
    int                                                                     range_ignored;
    int                                                                     restarted;
    int                                                                     user_canceled;

    double                                                                  t_begin;
    double                                                                  t_data;
    double                                                                  t_last_emit;
    double                                                                  t_sample;
    curl_off_t                                                              bytes_sample;
    double                                                                  speed;
    int                                                                     have_speed;
} DownloadContext;

static void __resolve_response__(DownloadContext *context) {
    if (context->resolved) {
        return;
    }

    long code                       = 0;
    curl_easy_getinfo(context->curl, CURLINFO_RESPONSE_CODE, &code);

    if (code < 200 || code >= 300) {
        return;
    }

    /* Server ignored Range request and sent 200 OK instead of 206 */
    if (context->resume_from > 0 && code == 200) {
        context->range_ignored      = 1;
        return;
    }

    context->resolved               = 1;
    context->http_code              = code;

    curl_off_t clen                 = -1;
    curl_easy_getinfo(context->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &clen);
    context->total                  = (clen >= 0) ? clen + context->offset : -1;

    context->t_data                 = __now_sec__();
    context->t_sample               = context->t_data;
    context->bytes_sample           = 0;

    if (context->options->on_headers) {
        char *ctype                 = NULL;
        char *ip                    = NULL;
        curl_easy_getinfo(context->curl, CURLINFO_CONTENT_TYPE, &ctype);
        curl_easy_getinfo(context->curl, CURLINFO_PRIMARY_IP, &ip);

        CURLResponseInfo info;
        memset(&info, 0, sizeof(info));
        info.http_code              = code;
        info.content_type           = ctype;
        info.server_ip              = ip;
        info.save_path              = context->path;
        info.total_bytes            = (int64_t)context->total;
        info.resumed_offset         = (int64_t)context->offset;
        info.restarted              = context->restarted;

        context->options->on_headers(&info, context->options->user_data);
    }
}

static size_t __write_cb__(char *pointer, size_t size, size_t nmemb, void *ud) {
    DownloadContext *context        = (DownloadContext *)ud;
    size_t n                        = size * nmemb;

    __resolve_response__(context);

    if (context->range_ignored) {
        return 0;
    }

    if (!context->resolved) {
        context->resolved           = 1;                    /* Non-HTTP protocols */
    }

    if (!context->fp) {
        context->fp                 = __open_file_utf8__(context->path, context->offset > 0);

        if (!context->fp) {
            context->io_errno       = errno;
            return 0;
        }

        context->opened             = 1;
    }

    size_t written                  = fwrite(pointer, 1, n, context->fp);

    if (written < n) {
        context->io_errno           = errno;
    }

    return written;
}

static void __update_speed__(DownloadContext *context, double t, curl_off_t now) {
    double dt                       = t - context->t_sample;

    if (dt < SAMPLE_MIN_DT) {
        return;
    }

    double inst                     = (double)(now - context->bytes_sample) / dt;

    if (!context->have_speed) {
        context->speed              = inst;
        context->have_speed         = 1;
    } else {
        double alpha                = 1.0 - exp(-dt / SPEED_TAU);
        context->speed              += alpha * (inst - context->speed);
    }

    context->t_sample               = t;
    context->bytes_sample           = now;
}

static int __emit_progress__(DownloadContext *context, double t, int final) {
    if (!context->options->on_progress) {
        return 0;
    }

    curl_off_t done                 = context->offset + context->session_bytes;
    int known                       = context->total >= 0;
    double speed                    = context->speed;
    double since                    = t - context->t_data;

    if (final || !context->have_speed) {
        speed                       = since > 0.05 ? (double)context->session_bytes / since : 0.0;
    }

    double fraction                 = -1.0;
    double eta                      = -1.0;

    if (known) {
        fraction                    = context->total > 0 ? (double)done / (double)context->total : 1.0;

        if (fraction > 1.0) {
            fraction                = 1.0;
        }

        if (final) {
            eta                     = 0.0;
        } else if (speed > 1.0) {
            eta                     = (double)(context->total - done) / speed;
        }
    }

    CURLProgress progress;
    progress.downloaded_bytes       = (int64_t)done;
    progress.total_bytes            = (int64_t)context->total;
    progress.session_bytes          = (int64_t)context->session_bytes;
    progress.fraction               = fraction;
    progress.speed_bytes_per_seconds = speed;
    progress.eta_seconds            = eta;
    progress.elapsed_seconds        = t - context->t_begin;

    return context->options->on_progress(&progress, context->options->user_data);
}

static int __xferinfo_cb__(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;

    DownloadContext *context        = (DownloadContext *)ud;

    if (context->options->cancel_flag && *(context->options->cancel_flag)) {
        context->user_canceled      = 1;
        return 1;
    }

    __resolve_response__(context);

    if (!context->resolved) {
        return 0;
    }

    if (context->total < 0 && dltotal > 0) {
        context->total              = dltotal + context->offset;
    }

    context->session_bytes          = dlnow;
    double t                        = __now_sec__();
    __update_speed__(context, t, dlnow);
    double interval                 = context->options->progress_interval_seconds > 0.0 ? context->options->progress_interval_seconds : DEFAULT_PROGRESS_DT;

    if (t - context->t_last_emit >= interval) {
        context->t_last_emit        = t;

        if (__emit_progress__(context, t, 0) != 0) {
            context->user_canceled  = 1;
            return 1;
        }
    }

    return 0;
}

static const char *__http_version_name__(long version) {
    switch (version) {
    case CURL_HTTP_VERSION_1_0: return "HTTP/1.0";
    case CURL_HTTP_VERSION_1_1: return "HTTP/1.1";
    case CURL_HTTP_VERSION_2_0: return "HTTP/2";
#ifdef CURL_HTTP_VERSION_3
    case CURL_HTTP_VERSION_3:   return "HTTP/3";
#endif
    default:                    return "HTTP/?";
    }
}

static void __populate_summary__(DownloadContext *context, CURLResult *result) {
    CURL *h                         = context->curl;
    curl_off_t speed                = 0;
    curl_off_t t_total              = 0;
    curl_off_t t_dns                = 0;
    curl_off_t t_conn               = 0;
    curl_off_t t_tls                = 0;
    curl_off_t t_ttfb               = 0;
    long version                    = 0;
    char *url                       = NULL;
    char *ip                        = NULL;

    curl_easy_getinfo(h, CURLINFO_SPEED_DOWNLOAD_T,     &speed);
    curl_easy_getinfo(h, CURLINFO_TOTAL_TIME_T,         &t_total);
    curl_easy_getinfo(h, CURLINFO_NAMELOOKUP_TIME_T,    &t_dns);
    curl_easy_getinfo(h, CURLINFO_CONNECT_TIME_T,       &t_conn);
    curl_easy_getinfo(h, CURLINFO_APPCONNECT_TIME_T,    &t_tls);
    curl_easy_getinfo(h, CURLINFO_STARTTRANSFER_TIME_T, &t_ttfb);
    curl_easy_getinfo(h, CURLINFO_HTTP_VERSION,         &version);
    curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL,        &url);
    curl_easy_getinfo(h, CURLINFO_PRIMARY_IP,           &ip);

    result->http_code               = context->http_code;
    result->total_bytes             = (int64_t)(context->offset + context->session_bytes);
    result->session_bytes           = (int64_t)context->session_bytes;
    result->avg_speed_bps           = (double)speed;

    snprintf(result->http_version, sizeof(result->http_version), "%s", __http_version_name__(version));
    snprintf(result->save_path, sizeof(result->save_path), "%s", context->path ? context->path : "");
    snprintf(result->effective_url, sizeof(result->effective_url), "%s", url ? url : "");
    snprintf(result->server_ip, sizeof(result->server_ip), "%s", ip ? ip : "");

    result->total_ms                = (double)t_total / 1e3;
    result->dns_ms                  = (double)t_dns   / 1e3;
    result->connect_ms              = (double)t_conn  / 1e3;
    result->tls_ms                  = (double)t_tls   / 1e3;
    result->ttfb_ms                 = (double)t_ttfb  / 1e3;
}

void __init_options__(CURLOptions *options, const char *url) {
    if (!options) {
        return;
    }

    memset(options, 0, sizeof(*options));
    options->url                    = url;
    options->connect_timeout_seconds = 30;
    options->stall_timeout_seconds  = 30;
    options->progress_interval_seconds = DEFAULT_PROGRESS_DT;
}

CURLStatus __download__(const CURLOptions *options, CURLResult *result) {
    TRACE_SCOPE();
    CURLResult local_result;
    CURLResult *res                 = result ? result : &local_result;
    memset(res, 0, sizeof(*res));

    if (!options || !options->url || !*options->url) {
        res->status                 = CURLDL_ERR_BAD_ARG;
        snprintf(res->error_message, sizeof(res->error_message), "Invalid or empty URL");
        return res->status;
    }

    CURL *curl                      = curl_easy_init();

    if (!curl) {
        res->status                 = CURLDL_ERR_INIT;
        snprintf(res->error_message, sizeof(res->error_message), "curl_easy_init failed");
        return res->status;
    }

    char *auto_name                 = NULL;
    const char *out_name            = options->output_path;

    if (!out_name || !*out_name) {
        auto_name                   = __filename_from_url__(curl, options->url);
        out_name                    = auto_name ? auto_name : "download.bin";
    }

    DownloadContext context;
    memset(&context, 0, sizeof(context));
    context.curl                    = curl;
    context.options                 = options;
    context.path                    = out_name;
    context.total                   = -1;

    if (options->resume) {
        int64_t existing            = __get_existing_file_size__(out_name);

        if (existing > 0) {
            context.resume_from     = (curl_off_t)existing;
        }
    }

    context.offset                  = context.resume_from;
    char error_buffer[CURL_ERROR_SIZE] = "";
    long connection_timeout         = options->connect_timeout_seconds > 0 ? options->connect_timeout_seconds : 30L;
    long stall_timeout              = options->stall_timeout_seconds   > 0 ? options->stall_timeout_seconds   : 30L;

    curl_easy_setopt(curl, CURLOPT_URL,              options->url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,        20L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR,      1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,        options->user_agent ? options->user_agent : "curldl/1.0 (libcurl)");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER,      error_buffer);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,         1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,   connection_timeout);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT,  1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,   stall_timeout);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE,    1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    __write_cb__);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,        &context);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, __xferinfo_cb__);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     &context);

    if (context.resume_from > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, context.resume_from);
    }

    if (options->max_speed_bytes_per_seconds > 0) {
        curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)options->max_speed_bytes_per_seconds);
    }

    if (options->insecure_skip_verify) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    context.t_begin                 = __now_sec__();
    CURLcode code                   = curl_easy_perform(curl);
    double t_end                    = __now_sec__();

    /* Server ignored Range header: restart cleanly from byte 0 */
    if ((code == CURLE_RANGE_ERROR || (code == CURLE_WRITE_ERROR && context.range_ignored)) && context.resume_from > 0) {
        if (context.fp) {
            fclose(context.fp);
            context.fp              = NULL;
        }

        context.opened              = 0;
        context.resume_from         = 0;
        context.offset              = 0;
        context.total               = -1;
        context.session_bytes       = 0;
        context.resolved            = 0;
        context.range_ignored       = 0;
        context.have_speed          = 0;
        context.restarted           = 1;
        context.speed               = 0.0;
        context.t_last_emit         = 0.0;
        error_buffer[0]             = '\0';

        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)0);
        context.t_begin             = __now_sec__();
        code                        = curl_easy_perform(curl);
        t_end                       = __now_sec__();
    }

    long http_code                  = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    context.http_code               = http_code;

    /* Ensure zero-byte files are still created on disk on success */
    if (code == CURLE_OK && http_code != 416 && !context.range_ignored) {
        if (!context.fp && !context.opened) {
            context.fp              = __open_file_utf8__(out_name, context.offset > 0);

            if (!context.fp) {
                context.io_errno    = errno;
                code                = CURLE_WRITE_ERROR;
            } else {
                context.opened      = 1;
            }
        }
    }

    if (context.fp) {
        if (fclose(context.fp) != 0 && code == CURLE_OK) {
            context.io_errno        = errno;
            code                    = CURLE_WRITE_ERROR;
        }

        context.fp                  = NULL;
    }

    int already_done                = context.resume_from > 0 && ((http_code == 416 && (code == CURLE_OK || code == CURLE_HTTP_RETURNED_ERROR)) || (code == CURLE_OK && context.range_ignored));

    if (already_done) {
        res->status                 = CURLDL_ALREADY_COMPLETE;
        __populate_summary__(&context, res);
    } else if (code == CURLE_OK) {
        curl_off_t got              = 0;
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &got);
        context.session_bytes       = got;

        if (context.total < 0) {
            context.total           = context.offset + got;
        }

        if (context.resolved) {
            __emit_progress__(&context, t_end, 1);
        }

        res->status                 = CURLDL_OK;
        __populate_summary__(&context, res);
    } else if (code == CURLE_ABORTED_BY_CALLBACK && context.user_canceled) {
        res->status                 = CURLDL_CANCELED;
        __populate_summary__(&context, res);
        snprintf(res->error_message, sizeof(res->error_message), "Canceled by user");
    } else if (code == CURLE_WRITE_ERROR && context.io_errno != 0) {
        res->status                 = CURLDL_ERR_FILE_IO;
        res->os_errno               = context.io_errno;
        __populate_summary__(&context, res);
        snprintf(res->error_message, sizeof(res->error_message), "File I/O error: %s", strerror(context.io_errno));
    } else {
        res->status                 = CURLDL_ERR_NETWORK;
        __populate_summary__(&context, res);
        snprintf(res->error_message, sizeof(res->error_message), "%s", error_buffer[0] ? error_buffer : curl_easy_strerror(code));
    }

    res->curl_code                  = (int)code;

    curl_easy_cleanup(curl);
    free(auto_name);
    return res->status;
}

INITIALIZER(__CURL_GLOBAL_INIT__) {
    CURLcode code                   = curl_global_init(CURL_GLOBAL_DEFAULT);

    if (code != CURLE_OK) {
        fprintf(stderr, "curl_global_init() failed: %s\n", curl_easy_strerror(code));
        return;
    }
}

DESTRUCTOR(__CURL_GLOBAL_CLEANUP__) {
    curl_global_cleanup();
}
