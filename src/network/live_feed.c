#include <network/live_feed.h>
#include <core/logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(USE_GTK4)
    #include <glib.h>
#else
    #include <stdatomic.h>
    #if defined(_WIN32)
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
        #include <windows.h>
    #else
        #include <pthread.h>
        #include <unistd.h>
    #endif
#endif

struct LiveFeedClient {
    char                host[128];
    int                 port;
    char                path[128];
    bool                use_ssl;

#if defined(USE_GTK4)
    gint                running;
    GThread             *worker_thread;
    GMutex              mutex;
#else
    atomic_int          running;
#if defined(_WIN32)
    HANDLE              worker_thread;
    CRITICAL_SECTION    mutex;
#else
    pthread_t           worker_thread;
    pthread_mutex_t     mutex;
#endif
#endif

    LiveStatsCallback   callback;
    void                *user_data;

    LiveGameStats       current_stats;
};

#if defined(USE_GTK4)
typedef struct {
    LiveStatsCallback callback;
    LiveGameStats     stats;
    void              *user_data;
} UiNotifyPayload;

static gboolean __notify_ui_callback__(gpointer user_data) {
    UiNotifyPayload *p = (UiNotifyPayload *)user_data;
    if (p && p->callback) {
        p->callback(&p->stats, p->user_data);
    }
    g_free(p);
    return G_SOURCE_REMOVE;
}

static void __dispatch_stats_to_ui__(LiveFeedClient *client) {
    if (!client || !client->callback) return;

    UiNotifyPayload *p = g_new0(UiNotifyPayload, 1);
    p->callback = client->callback;
    p->user_data = client->user_data;

    g_mutex_lock(&client->mutex);
    p->stats = client->current_stats;
    g_mutex_unlock(&client->mutex);

    g_idle_add(__notify_ui_callback__, p);
}
#else
static void __dispatch_stats_to_ui__(LiveFeedClient *client) {
    if (!client) return;

#if defined(_WIN32)
    EnterCriticalSection(&client->mutex);
#else
    pthread_mutex_lock(&client->mutex);
#endif

    LiveStatsCallback cb = client->callback;
    void *ud = client->user_data;
    LiveGameStats st = client->current_stats;

#if defined(_WIN32)
    LeaveCriticalSection(&client->mutex);
#else
    pthread_mutex_unlock(&client->mutex);
#endif

    if (cb) {
        cb(&st, ud);
    }
}
#endif

void __live_feed_simulate_tick__(LiveFeedClient *client) {
    if (!client) return;

#if defined(USE_GTK4)
    g_mutex_lock(&client->mutex);
#elif defined(_WIN32)
    EnterCriticalSection(&client->mutex);
#else
    pthread_mutex_lock(&client->mutex);
#endif

    int online_delta = (rand() % 15) - 7;
    int ingame_delta = (rand() % 11) - 5;

    int new_online = (int)client->current_stats.online_players + online_delta;
    if (new_online < 500) new_online = 500;
    client->current_stats.online_players = (uint32_t)new_online;

    int new_ingame = (int)client->current_stats.ingame_players + ingame_delta;
    if (new_ingame < 400) new_ingame = 400;
    if (new_ingame > (int)client->current_stats.online_players) {
        new_ingame = (int)client->current_stats.online_players - 20;
    }
    client->current_stats.ingame_players = (uint32_t)new_ingame;

    client->current_stats.latency_ms = (uint32_t)(18 + (rand() % 12));
    client->current_stats.status = REALM_ONLINE;

#if defined(USE_GTK4)
    g_mutex_unlock(&client->mutex);
#elif defined(_WIN32)
    LeaveCriticalSection(&client->mutex);
#else
    pthread_mutex_unlock(&client->mutex);
#endif

    __dispatch_stats_to_ui__(client);
}

#if defined(USE_GTK4)
static gpointer __live_feed_worker__(gpointer data) {
    LiveFeedClient *client = (LiveFeedClient *)data;
    if (!client) return NULL;

    while (g_atomic_int_get(&client->running)) {
        __live_feed_simulate_tick__(client);
        g_usleep(2 * G_USEC_PER_SEC);
    }

    return NULL;
}
#elif defined(_WIN32)
static DWORD WINAPI __live_feed_worker__(LPVOID data) {
    LiveFeedClient *client = (LiveFeedClient *)data;
    if (!client) return 0;

    while (atomic_load(&client->running)) {
        __live_feed_simulate_tick__(client);
        Sleep(2000);
    }

    return 0;
}
#else
static void *__live_feed_worker__(void *data) {
    LiveFeedClient *client = (LiveFeedClient *)data;
    if (!client) return NULL;

    while (atomic_load(&client->running)) {
        __live_feed_simulate_tick__(client);
        sleep(2);
    }

    return NULL;
}
#endif

LiveFeedClient *__live_feed_create__(const char *host, int port, const char *path, bool use_ssl) {
    TRACE_SCOPE();
#if defined(USE_GTK4)
    LiveFeedClient *client = g_new0(LiveFeedClient, 1);
#else
    LiveFeedClient *client = (LiveFeedClient *)calloc(1, sizeof(LiveFeedClient));
#endif
    if (!client) return NULL;

    if (host) {
        strncpy(client->host, host, sizeof(client->host) - 1);
    } else {
        strncpy(client->host, "127.0.0.1", sizeof(client->host) - 1);
    }
    client->port = port > 0 ? port : 8080;
    if (path) {
        strncpy(client->path, path, sizeof(client->path) - 1);
    } else {
        strncpy(client->path, "/live", sizeof(client->path) - 1);
    }
    client->use_ssl = use_ssl;

#if defined(USE_GTK4)
    g_mutex_init(&client->mutex);
#elif defined(_WIN32)
    InitializeCriticalSection(&client->mutex);
#else
    pthread_mutex_init(&client->mutex, NULL);
#endif

    client->current_stats.status = REALM_ONLINE;
    strncpy(client->current_stats.realm_name, "Azeroth Prime", sizeof(client->current_stats.realm_name) - 1);
    client->current_stats.online_players = 1420;
    client->current_stats.ingame_players = 1180;
    client->current_stats.queue_count = 0;
    client->current_stats.latency_ms = 22;
    strncpy(client->current_stats.motd, "Patch 1.0.0 live! Double XP weekend active.",
            sizeof(client->current_stats.motd) - 1);

    return client;
}

void __live_feed_set_callback__(LiveFeedClient *client, LiveStatsCallback callback, void *user_data) {
    if (!client) return;

#if defined(USE_GTK4)
    g_mutex_lock(&client->mutex);
#elif defined(_WIN32)
    EnterCriticalSection(&client->mutex);
#else
    pthread_mutex_lock(&client->mutex);
#endif

    client->callback = callback;
    client->user_data = user_data;

#if defined(USE_GTK4)
    g_mutex_unlock(&client->mutex);
#elif defined(_WIN32)
    LeaveCriticalSection(&client->mutex);
#else
    pthread_mutex_unlock(&client->mutex);
#endif

    __dispatch_stats_to_ui__(client);
}

bool __live_feed_start__(LiveFeedClient *client) {
    TRACE_SCOPE();
    if (!client) return false;

#if defined(USE_GTK4)
    if (g_atomic_int_get(&client->running)) return false;
    g_atomic_int_set(&client->running, 1);
    client->worker_thread = g_thread_new("live_feed_worker", __live_feed_worker__, client);
    if (!client->worker_thread) {
        g_atomic_int_set(&client->running, 0);
        return false;
    }
    return true;
#else
    if (atomic_load(&client->running)) return false;
    atomic_store(&client->running, 1);

#if defined(_WIN32)
    client->worker_thread = CreateThread(NULL, 0, __live_feed_worker__, client, 0, NULL);
    if (!client->worker_thread) {
        atomic_store(&client->running, 0);
        return false;
    }
#else
    if (pthread_create(&client->worker_thread, NULL, __live_feed_worker__, client) != 0) {
        atomic_store(&client->running, 0);
        return false;
    }
#endif
    return true;
#endif
}

void __live_feed_stop__(LiveFeedClient *client) {
    TRACE_SCOPE();
    if (!client) return;

#if defined(USE_GTK4)
    if (!g_atomic_int_get(&client->running)) return;
    g_atomic_int_set(&client->running, 0);
    if (client->worker_thread) {
        g_thread_join(client->worker_thread);
        client->worker_thread = NULL;
    }
#else
    if (!atomic_load(&client->running)) return;
    atomic_store(&client->running, 0);

#if defined(_WIN32)
    if (client->worker_thread) {
        WaitForSingleObject(client->worker_thread, INFINITE);
        CloseHandle(client->worker_thread);
        client->worker_thread = NULL;
    }
#else
    if (client->worker_thread) {
        pthread_join(client->worker_thread, NULL);
        client->worker_thread = 0;
    }
#endif
#endif
}

void __live_feed_destroy__(LiveFeedClient *client) {
    TRACE_SCOPE();
    if (!client) return;

    __live_feed_stop__(client);

#if defined(USE_GTK4)
    g_mutex_clear(&client->mutex);
    g_free(client);
#elif defined(_WIN32)
    DeleteCriticalSection(&client->mutex);
    free(client);
#else
    pthread_mutex_destroy(&client->mutex);
    free(client);
#endif
}
