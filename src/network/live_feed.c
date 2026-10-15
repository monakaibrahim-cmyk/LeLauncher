#include <network/live_feed.h>
#include <core/logger.h>
#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LiveFeedClient {
    char                host[128];
    int                 port;
    char                path[128];
    bool                use_ssl;

    gint                running;
    GThread             *worker_thread;
    GMutex              mutex;

    LiveStatsCallback   callback;
    void                *user_data;

    LiveGameStats       current_stats;
};

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

void __live_feed_simulate_tick__(LiveFeedClient *client) {
    if (!client) return;

    g_mutex_lock(&client->mutex);
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
    g_mutex_unlock(&client->mutex);

    __dispatch_stats_to_ui__(client);
}

static gpointer __live_feed_worker__(gpointer data) {
    LiveFeedClient *client = (LiveFeedClient *)data;
    if (!client) return NULL;

    while (g_atomic_int_get(&client->running)) {
        __live_feed_simulate_tick__(client);
        g_usleep(2 * G_USEC_PER_SEC);
    }

    return NULL;
}

LiveFeedClient *__live_feed_create__(const char *host, int port, const char *path, bool use_ssl) {
    TRACE_SCOPE();
    LiveFeedClient *client = g_new0(LiveFeedClient, 1);

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

    g_mutex_init(&client->mutex);

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

    g_mutex_lock(&client->mutex);
    client->callback = callback;
    client->user_data = user_data;
    g_mutex_unlock(&client->mutex);

    __dispatch_stats_to_ui__(client);
}

bool __live_feed_start__(LiveFeedClient *client) {
    TRACE_SCOPE();
    if (!client || g_atomic_int_get(&client->running)) return false;

    g_atomic_int_set(&client->running, 1);
    client->worker_thread = g_thread_new("live_feed_worker", __live_feed_worker__, client);
    if (!client->worker_thread) {
        g_atomic_int_set(&client->running, 0);
        return false;
    }
    return true;
}

void __live_feed_stop__(LiveFeedClient *client) {
    TRACE_SCOPE();
    if (!client || !g_atomic_int_get(&client->running)) return;

    g_atomic_int_set(&client->running, 0);
    if (client->worker_thread) {
        g_thread_join(client->worker_thread);
        client->worker_thread = NULL;
    }
}

void __live_feed_destroy__(LiveFeedClient *client) {
    TRACE_SCOPE();
    if (!client) return;

    __live_feed_stop__(client);
    g_mutex_clear(&client->mutex);
    g_free(client);
}
