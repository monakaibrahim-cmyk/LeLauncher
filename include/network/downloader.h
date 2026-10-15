#ifndef __DOWNLOADER_H__
#define __DOWNLOADER_H__

#include <curl/curl.h>
#include <stddef.h>
#include <stdint.h>

/* Status codes returned by __download__() */
typedef enum {
    CURLDL_OK = 0,
    CURLDL_ALREADY_COMPLETE,                                                                                /* File was already fully downloaded (HTTP 416) */
    CURLDL_CANCELED,                                                                                        /* Aborted via cancel_flag or progress callback */
    CURLDL_ERR_BAD_ARG,                                                                                     /* Invalid options passed                       */
    CURLDL_ERR_INIT,                                                                                        /* libcurl initialization failed                */
    CURLDL_ERR_FILE_IO,                                                                                     /* Local disk open/write/close error            */
    CURLDL_ERR_NETWORK                                                                                      /* Transfer or HTTP error (see result.curl_code)*/
} CURLStatus;

/* Fired once when 2xx HTTP headers (or initial data) arrive */
typedef struct {
    long                                                                    http_code;
    const char                                                              *content_type;                  /* May be NULL if server omitted it        */
    const char                                                              *server_ip;                     /* Primary IP string                       */
    const char                                                              *save_path;                     /* Resolved local file path                */
    int64_t                                                                 total_bytes;                    /* -1 if unknown (chunked/no Content-Len)  */
    int64_t                                                                 resumed_offset;                 /* > 0 if resuming an existing file        */
    int                                                                     restarted;                      /* 1 if server ignored Range and restarted */
} CURLResponseInfo;

/* Fired periodically during transfer for UI progress bars / labels */
typedef struct {
    int64_t                                                                 downloaded_bytes;               /* Total bytes on disk (offset + session)  */
    int64_t                                                                 total_bytes;                    /* Total expected bytes, or -1 if unknown  */
    int64_t                                                                 session_bytes;                  /* Bytes downloaded in this session        */
    double                                                                  fraction;                       /* 0.0 .. 1.0 if size known, else -1.0     */
    double                                                                  speed_bytes_per_seconds;        /* Smoothed download speed (bytes/sec)     */
    double                                                                  eta_seconds;                    /* Estimated seconds remaining, -1 if unkn */
    double                                                                  elapsed_seconds;                /* Seconds elapsed since request start     */
} CURLProgress;

/* Final summary populated when __download__() finishes */
typedef struct {
    CURLStatus                                                              status;
    int                                                                     curl_code;                      /* Underlying CURLcode                     */
    int                                                                     os_errno;                       /* Underlying errno on CURLDL_ERR_FILE_IO  */
    long                                                                    http_code;                      /* Final HTTP status code                  */
    char                                                                    http_version[16];
    char                                                                    save_path[1024];
    char                                                                    effective_url[2048];
    char                                                                    server_ip[128];
    char                                                                    error_message[512];

    int64_t                                                                 total_bytes;                    /* Final size on disk                      */
    int64_t                                                                 session_bytes;                  /* Bytes transferred in this session       */
    double                                                                  avg_speed_bps;                  /* Average speed reported by libcurl       */

    /* Timing breakdown in milliseconds */
    double                                                                  total_ms;
    double                                                                  dns_ms;
    double                                                                  connect_ms;
    double                                                                  tls_ms;
    double                                                                  ttfb_ms;
} CURLResult;

/*
 * Callback signatures:
 * - on_headers:  Optional notification when transfer metadata is known.
 * - on_progress: Return 0 to continue, or non-zero to cancel the transfer.
 */
typedef void                                                                (*CURLHeadersFunction)(const CURLResponseInfo *info, void *user_data);
typedef int                                                                 (*CURLProgressFunction)(const CURLProgress *prog, void *user_data);

/* Configuration passed to __download__() */
typedef struct {
    const char                                                              *url;                           /* Required: source URL            */
    const char                                                              *output_path;                   /* Optional: NULL auto-derives     */
    const char                                                              *user_agent;                    /* Optional: NULL uses default     */

    int                                                                     resume;                         /* 1 = continue partial file       */
    int                                                                     insecure_skip_verify;           /* 1 = skip TLS verification       */
    int64_t                                                                 max_speed_bytes_per_seconds;    /* 0 = unlimited                   */
    long                                                                    connect_timeout_seconds;        /* 0 = default (30s)               */
    long                                                                    stall_timeout_seconds;          /* 0 = default (30s < 1 B/s)       */
    double                                                                  progress_interval_seconds;      /* 0.0 = default (0.1s / 10 Hz)    */

    CURLHeadersFunction                                                     on_headers;                     /* Optional                        */
    CURLProgressFunction                                                    on_progress;                    /* Optional                        */
    void                                                                    *user_data;                     /* Passed to callbacks             */

    /* Optional external flag: set *cancel_flag = 1 from any thread to abort */
    volatile int                                                            *cancel_flag;
} CURLOptions;

/* Initialize options struct with safe defaults */
void __init_options__(CURLOptions *options, const char *url);

/* Execute a download synchronously (run on a worker thread in GUI apps) */
CURLStatus __download__(const CURLOptions *options, CURLResult *result);

/* Optional UI formatting utilities */
void __format_bytes__(double bytes, char *output, size_t size);
void __format_duration__(double seconds, char *output, size_t size);
int64_t __parse_rate__(const char *data);

#endif // __DOWNLOADER_H__
