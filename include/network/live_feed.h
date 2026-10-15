#ifndef __LIVE_FEED_H__
#define __LIVE_FEED_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    REALM_OFFLINE = 0,
    REALM_ONLINE,
    REALM_MAINTENANCE
} RealmStatus;

typedef struct {
    RealmStatus status;
    char        realm_name[64];
    uint32_t    online_players;
    uint32_t    ingame_players;
    uint32_t    queue_count;
    uint32_t    latency_ms;
    char        motd[256];
} LiveGameStats;

typedef void (*LiveStatsCallback)(const LiveGameStats *stats, void *user_data);

typedef struct LiveFeedClient LiveFeedClient;

LiveFeedClient *__live_feed_create__(const char *host, int port, const char *path, bool use_ssl);
void __live_feed_set_callback__(LiveFeedClient *client, LiveStatsCallback callback, void *user_data);
bool __live_feed_start__(LiveFeedClient *client);
void __live_feed_stop__(LiveFeedClient *client);
void __live_feed_simulate_tick__(LiveFeedClient *client);
void __live_feed_destroy__(LiveFeedClient *client);

#endif /* __LIVE_FEED_H__ */
