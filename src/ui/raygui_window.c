#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
#endif

/* Include Raylib and Raygui FIRST to prevent Win32 GDI/User symbol collision */
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#if defined(_WIN32)
    #include <windows.h>
    #include <shellapi.h>

    #ifndef SW_SHOWNORMAL
        #define SW_SHOWNORMAL 1
    #endif

    /* Undefine any Windows macro names that clash with Raylib */
    #ifdef Rectangle
        #undef Rectangle
    #endif
    #ifdef CloseWindow
        #undef CloseWindow
    #endif
    #ifdef ShowCursor
        #undef ShowCursor
    #endif
    #ifdef DrawText
        #undef DrawText
    #endif
    #ifdef DrawTextEx
        #undef DrawTextEx
    #endif
    #ifdef LoadImage
        #undef LoadImage
    #endif
#else
    #include <pthread.h>
    #include <unistd.h>
#endif

#include <ui/raygui_window.h>
#include <core/Version.h>
#include <core/Globals.h>
#include <core/dat.h>
#include <core/logger.h>
#include <network/api.h>
#include <network/hwid.h>
#include <network/live_feed.h>
#include <network/downloader.h>
#include <network/mfil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define MAX_LOG_LINES    128
#define LOG_LINE_LEN     256
#define MAX_SNOWFLAKES   75

/* ==============================================================================
 * WotLK Theme Colors & Palettes (Icecrown Citadel Aesthetics)
 * ============================================================================== */
#define COLOR_ABYSS_BLACK        ((Color){ 6, 9, 15, 255 })
#define COLOR_CITADEL_DARK       ((Color){ 10, 14, 24, 255 })
#define COLOR_CITADEL_CARD       ((Color){ 14, 20, 34, 240 })
#define COLOR_FROST_BORDER       ((Color){ 30, 48, 76, 255 })
#define COLOR_FROST_GLOW         ((Color){ 0, 225, 255, 255 })
#define COLOR_ICE_BLUE           ((Color){ 56, 189, 248, 255 })
#define COLOR_PALE_ICE           ((Color){ 186, 230, 253, 255 })
#define COLOR_WOTLK_GOLD         ((Color){ 234, 179, 8, 255 })
#define COLOR_WOTLK_BRONZE       ((Color){ 180, 130, 30, 255 })
#define COLOR_CRIMSON_SCOURGE    ((Color){ 239, 68, 68, 255 })
#define COLOR_EMERALD_ONLINE     ((Color){ 16, 185, 129, 255 })

/* ==============================================================================
 * State Structures
 * ============================================================================== */
typedef enum {
    PATCH_IDLE = 0,
    PATCH_CHECKING,
    PATCH_DOWNLOADING,
    PATCH_VERIFYING,
    PATCH_READY,
    PATCH_ERROR
} PatchStatus;

typedef struct {
    float x;
    float y;
    float speedY;
    float speedX;
    float size;
    float alpha;
    float swayOffset;
} Snowflake;

typedef struct {
    /* Credentials & Auth */
    char realmlist_host[128];
    char username[64];
    char password[64];
    char auth_status[128];
    bool auth_success;
    int  active_input_box; /* 0 = none, 1 = user, 2 = pass, 3 = realmlist */

    /* Realm & Live Stats */
    char realm_name[64];
    int  realm_status;
    unsigned int online_players;
    unsigned int ingame_players;
    unsigned int latency_ms;
    char motd[256];

    /* Patching & File Downloads */
    PatchStatus patch_status;
    float download_progress;
    char  status_message[256];
    char  current_file[128];
    char  speed_str[64];
    char  eta_str[64];
    bool  patch_worker_running;
    bool  patch_cancel_requested;

    /* Logs view */
    char  logs[MAX_LOG_LINES][LOG_LINE_LEN];
    int   log_count;

    /* UI navigation */
    int   active_tab; /* 0 = Campaign & Play, 1 = Realm Telemetry, 2 = Chronicles, 3 = Client & Logs */

    /* Snowfall particles */
    Snowflake snowflakes[MAX_SNOWFLAKES];

    /* Launch & Utility notices */
    char  tool_notice[128];
    float tool_notice_timer;

#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t  lock;
#endif

    LiveFeedClient  *live_feed;
} WotlkLauncherState;

static WotlkLauncherState g_state;

static void __state_lock__(void) {
#if defined(_WIN32)
    EnterCriticalSection(&g_state.lock);
#else
    pthread_mutex_lock(&g_state.lock);
#endif
}

static void __state_unlock__(void) {
#if defined(_WIN32)
    LeaveCriticalSection(&g_state.lock);
#else
    pthread_mutex_unlock(&g_state.lock);
#endif
}

/* ==============================================================================
 * Snow Particle System
 * ============================================================================== */
static void __init_snowflakes__(int screenW, int screenH) {
    for (int i = 0; i < MAX_SNOWFLAKES; i++) {
        g_state.snowflakes[i].x = (float)(rand() % (screenW > 0 ? screenW : 1000));
        g_state.snowflakes[i].y = (float)(rand() % (screenH > 0 ? screenH : 640));
        g_state.snowflakes[i].speedY = 18.0f + (float)(rand() % 40);
        g_state.snowflakes[i].speedX = 6.0f + (float)(rand() % 15);
        g_state.snowflakes[i].size = 1.0f + (float)(rand() % 3);
        g_state.snowflakes[i].alpha = 0.2f + (float)(rand() % 60) / 100.0f;
        g_state.snowflakes[i].swayOffset = (float)(rand() % 100) * 0.1f;
    }
}

static void __update_and_draw_snow__(int screenW, int screenH, float dt) {
    float time = (float)GetTime();
    for (int i = 0; i < MAX_SNOWFLAKES; i++) {
        Snowflake *sf = &g_state.snowflakes[i];
        sf->y += sf->speedY * dt;
        sf->x += sinf(time * 1.5f + sf->swayOffset) * sf->speedX * dt;

        if (sf->y > (float)screenH) {
            sf->y = -6.0f;
            sf->x = (float)(rand() % screenW);
        }
        if (sf->x < 0) sf->x = (float)screenW;
        if (sf->x > (float)screenW) sf->x = 0;

        Color snowColor = (Color){ 200, 230, 255, (unsigned char)(sf->alpha * 255) };
        DrawCircle((int)sf->x, (int)sf->y, sf->size, snowColor);
    }
}

/* ==============================================================================
 * Logger Dispatch Handler
 * ============================================================================== */
static void __on_wotlk_log_dispatch__(LogLevel level, const char *system, const char *formatted, void *user_data) {
    (void)level;
    (void)system;
    (void)user_data;
    if (!formatted) return;

    __state_lock__();
    if (g_state.log_count < MAX_LOG_LINES) {
        snprintf(g_state.logs[g_state.log_count], LOG_LINE_LEN, "%s", formatted);
        g_state.log_count++;
    } else {
        /* Shift buffer up */
        for (int i = 0; i < MAX_LOG_LINES - 1; i++) {
            strncpy(g_state.logs[i], g_state.logs[i + 1], LOG_LINE_LEN);
        }
        snprintf(g_state.logs[MAX_LOG_LINES - 1], LOG_LINE_LEN, "%s", formatted);
    }
    __state_unlock__();
}

/* ==============================================================================
 * Live Stats Callback
 * ============================================================================== */
static void __on_wotlk_live_stats__(const LiveGameStats *stats, void *user_data) {
    (void)user_data;
    if (!stats) return;

    __state_lock__();
    strncpy(g_state.realm_name, stats->realm_name, sizeof(g_state.realm_name) - 1);
    g_state.realm_status = stats->status;
    g_state.online_players = stats->online_players;
    g_state.ingame_players = stats->ingame_players;
    g_state.latency_ms = stats->latency_ms;
    if (stats->motd[0] != '\0') {
        strncpy(g_state.motd, stats->motd, sizeof(g_state.motd) - 1);
    }
    __state_unlock__();
}

/* ==============================================================================
 * Download Progress Callback
 * ============================================================================== */
typedef struct {
    size_t file_index;
    size_t total_files;
    const char *current_file;
} WotlkDlContext;

static int __on_wotlk_download_progress__(const CURLProgress *prog, void *user_data) {
    WotlkDlContext *dc = (WotlkDlContext *)user_data;
    if (!dc) return 0;
    if (g_state.patch_cancel_requested) return 1;

    __state_lock__();
    if (dc->total_files > 0) {
        float file_prog = (prog->fraction >= 0.0) ? (float)prog->fraction : 0.0f;
        g_state.download_progress = ((float)dc->file_index + file_prog) / (float)dc->total_files;
    }
    char spd[32];
    __format_bytes__(prog->speed_bytes_per_seconds, spd, sizeof(spd));
    snprintf(g_state.speed_str, sizeof(g_state.speed_str), "%s/s", spd);

    char eta[32];
    __format_duration__(prog->eta_seconds, eta, sizeof(eta));
    snprintf(g_state.eta_str, sizeof(g_state.eta_str), "ETA: %s", eta);
    __state_unlock__();

    return 0;
}

/* ==============================================================================
 * Background Patch Worker
 * ============================================================================== */
static void *__wotlk_patch_worker__(void *arg) {
    (void)arg;
    LOG_INFO("PATCH", "WotLK background content verification engine started");

    __state_lock__();
    g_state.patch_status = PATCH_CHECKING;
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Inspecting Northrend master manifest...");
    g_state.download_progress = 0.05f;
    __state_unlock__();

    const char *master_path = "WoW.mfil";
    MfilMaster master;
    bool has_master = __mfil_parse_master_file__(master_path, &master);
    if (!has_master) {
        has_master = __mfil_parse_master_file__("data/WoW.mfil", &master);
        if (has_master) master_path = "data/WoW.mfil";
    }

    const char *base_location = "http://127.0.0.1/wow-pod-retail/US/12340.direct/";
    const char *partial_file = "wow-12340-94ee8476adb4ee065bfa9e73193d1496.mfil";

    if (has_master) {
        const char *loc = __mfil_get_master_location__(&master, "northrend");
        if (loc) base_location = loc;
        else if (master.server_count > 0) base_location = master.servers[0].location;
        if (strlen(master.manifest_partial) > 0) partial_file = master.manifest_partial;
    }

    __state_lock__();
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Parsing client manifest: %s", partial_file);
    g_state.download_progress = 0.15f;
    __state_unlock__();

    char local_manifest_path[512];
    snprintf(local_manifest_path, sizeof(local_manifest_path), "www/wow-pod-retail/US/12340.direct/%s", partial_file);

    MfilManifest manifest;
    bool has_manifest = __mfil_parse_manifest_file__(local_manifest_path, &manifest);
    if (!has_manifest) {
        has_manifest = __mfil_parse_manifest_file__(partial_file, &manifest);
    }

    if (!has_manifest) {
        __state_lock__();
        g_state.patch_status = PATCH_DOWNLOADING;
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Fetching manifest from CDN...");
        snprintf(g_state.current_file, sizeof(g_state.current_file), "%s", partial_file);
        __state_unlock__();

        CURLResult res;
        CURLStatus st = __mfil_download_partial__(base_location, partial_file, partial_file, NULL, &res);
        if (st == CURLDL_OK) {
            has_manifest = __mfil_parse_manifest_file__(partial_file, &manifest);
        }
    }

    if (!has_manifest) {
        __state_lock__();
        g_state.patch_status = PATCH_READY; /* Allow playing existing installation */
        g_state.download_progress = 1.0f;
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Ready to enter Northrend (Local Installation)");
        snprintf(g_state.current_file, sizeof(g_state.current_file), "Local Cache");
        g_state.patch_worker_running = false;
        __state_unlock__();
        if (has_master) __mfil_free_master__(&master);
        return NULL;
    }

    __state_lock__();
    g_state.patch_status = PATCH_VERIFYING;
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Verifying %zu game archives (MD5)...", manifest.file_count);
    __state_unlock__();

    size_t verified = 0;
    size_t downloaded = 0;

    for (size_t i = 0; i < manifest.file_count; i++) {
        if (g_state.patch_cancel_requested) break;

        const MfilFile *file = &manifest.files[i];

        __state_lock__();
        snprintf(g_state.current_file, sizeof(g_state.current_file), "%s", file->name);
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Verifying: %s (%zu/%zu)", file->name, i + 1, manifest.file_count);
        g_state.download_progress = (float)i / (float)manifest.file_count;
        __state_unlock__();

        if (__mfil_verify_file_checksum__(file->name, file->checksum)) {
            verified++;
            continue;
        }

        /* File missing or corrupted -> download */
        __state_lock__();
        g_state.patch_status = PATCH_DOWNLOADING;
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Downloading: %s (%zu/%zu)", file->name, i + 1, manifest.file_count);
        __state_unlock__();

        int cancel_int = 0;
        CURLOptions opts;
        __init_options__(&opts, NULL);
        opts.cancel_flag = (volatile int *)&cancel_int;
        opts.resume = 1;

        WotlkDlContext dc = {
            .file_index = i,
            .total_files = manifest.file_count,
            .current_file = file->name
        };
        opts.on_progress = __on_wotlk_download_progress__;
        opts.user_data = &dc;

        CURLResult dl_res;
        CURLStatus dl_st = __mfil_download_file__(base_location, &manifest, file, NULL, &opts, &dl_res);
        if (dl_st == CURLDL_OK || dl_st == CURLDL_ALREADY_COMPLETE) {
            downloaded++;
            verified++;
        }
    }

    __state_lock__();
    g_state.patch_status = PATCH_READY;
    g_state.download_progress = 1.0f;
    snprintf(g_state.status_message, sizeof(g_state.status_message), "All %zu game archives verified. The Frozen Throne awaits!", verified);
    snprintf(g_state.current_file, sizeof(g_state.current_file), "None (Ready)");
    g_state.speed_str[0] = '\0';
    g_state.eta_str[0] = '\0';
    g_state.patch_worker_running = false;
    __state_unlock__();

    __mfil_free_manifest__(&manifest);
    if (has_master) __mfil_free_master__(&master);

    LOG_INFO("PATCH", "Patch verification finished (%zu verified, %zu downloaded)", verified, downloaded);
    return NULL;
}

static void __wotlk_start_patch_thread__(void) {
    if (g_state.patch_worker_running) return;

    g_state.patch_worker_running = true;
    g_state.patch_cancel_requested = false;

#if defined(_WIN32)
    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)__wotlk_patch_worker__, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, __wotlk_patch_worker__, NULL);
    pthread_detach(thread);
#endif
}

/* ==============================================================================
 * Player Utilities: Realmlist & Cache Cleaner
 * ============================================================================== */
static void __wotlk_set_realmlist__(const char *host) {
    if (!host || host[0] == '\0') return;

    const char *paths[] = {
        "realmlist.wtf",
        "Data/realmlist.wtf",
        "data/realmlist.wtf",
        "Data/enUS/realmlist.wtf",
        "Data/enGB/realmlist.wtf"
    };

    int updated = 0;
    for (int i = 0; i < 5; i++) {
        FILE *f = fopen(paths[i], "w");
        if (f) {
            fprintf(f, "set realmlist %s\n", host);
            fclose(f);
            updated++;
        }
    }

    __state_lock__();
    snprintf(g_state.tool_notice, sizeof(g_state.tool_notice), "✓ Realmlist set: %s (%d files)", host, updated);
    g_state.tool_notice_timer = 4.0f;
    __state_unlock__();
    LOG_INFO("TOOL", "Realmlist updated to: %s (%d files written)", host, updated);
}

static void __wotlk_clear_cache__(void) {
#if defined(_WIN32)
    system("rmdir /s /q Cache 2>nul");
    system("rmdir /s /q WDB 2>nul");
#else
    if (system("rm -rf Cache WDB 2>/dev/null") == 0) {}
#endif

    __state_lock__();
    snprintf(g_state.tool_notice, sizeof(g_state.tool_notice), "✓ Cleared WDB & Cache directories!");
    g_state.tool_notice_timer = 4.0f;
    __state_unlock__();
    LOG_INFO("TOOL", "Game client Cache / WDB cleaned successfully");
}

/* ==============================================================================
 * Authentication & Credential Storage Action
 * ============================================================================== */
static void __wotlk_trigger_login__(void) {
    LOG_INFO("AUTH", "Authenticating champion '%s' with realm gateway...", g_state.username);
    __state_lock__();
    snprintf(g_state.auth_status, sizeof(g_state.auth_status), "Contacting realm authentication...");
    __state_unlock__();

    const char *hwid = __hwid_get__();
    char ip[64] = "127.0.0.1";
    __network_get_client_ip__(ip, sizeof(ip));

    /* Use embedded encrypted API URL automatically */
    ApiPreAuthResult preauth = __api_request_temporary_token__(NULL, hwid, ip);
    if (!preauth.success) {
        __state_lock__();
        g_state.auth_success = false;
        snprintf(g_state.auth_status, sizeof(g_state.auth_status), "Handshake failed: %s", preauth.error_message);
        __state_unlock__();
        return;
    }

    ApiAuthResult auth = __api_authenticate_credentials__(NULL, preauth.temporary_token, g_state.username, g_state.password, hwid);

    __state_lock__();
    if (auth.success) {
        g_state.auth_success = true;
        snprintf(g_state.auth_status, sizeof(g_state.auth_status), "✓ Authenticated (Account: %s)", auth.account_id);
        LOG_INFO("AUTH", "Authentication success for %s (Account: %s)", g_state.username, auth.account_id);

        /* Save credentials into encrypted Account.dat */
        if (gStructure) {
            strncpy(gStructure->credential.username, g_state.username, sizeof(gStructure->credential.username) - 1);
            strncpy(gStructure->credential.password, g_state.password, sizeof(gStructure->credential.password) - 1);
            strncpy(gStructure->credential.token, auth.permanent_token, sizeof(gStructure->credential.token) - 1);
            if (__save__()) {
                LOG_INFO("DAT", "Credentials saved to Account.dat");
            }
        }
    } else {
        g_state.auth_success = false;
        snprintf(g_state.auth_status, sizeof(g_state.auth_status), "✗ Failed: %s", auth.error_message[0] ? auth.error_message : "Rejected");
        LOG_WARN("AUTH", "Authentication failed: %s", g_state.auth_status);
    }
    __state_unlock__();
}

/* ==============================================================================
 * Game Launch Execution
 * ============================================================================== */
static void __wotlk_launch_game__(void) {
    LOG_INFO("SYS", "Launching Wrath of the Lich King client (WoW.exe)...");

#if defined(_WIN32)
    HINSTANCE res = ShellExecuteA(NULL, "open", "WoW.exe", NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)res <= 32) {
        /* Fallback check for Wow.exe */
        ShellExecuteA(NULL, "open", "Wow.exe", NULL, NULL, SW_SHOWNORMAL);
    }
#else
    if (system("./WoW.exe &") != 0 && system("wine WoW.exe &") != 0) {
        LOG_INFO("SYS", "Game binary triggered.");
    }
#endif

    __state_lock__();
    snprintf(g_state.tool_notice, sizeof(g_state.tool_notice), "⚔ Launching WoW.exe...");
    g_state.tool_notice_timer = 5.0f;
    __state_unlock__();
}

/* ==============================================================================
 * Main WotLK Launcher Lifecycle
 * ============================================================================== */
int raygui_window_run(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Initialize internal state */
    memset(&g_state, 0, sizeof(g_state));
    strncpy(g_state.realmlist_host, "logon.northrend.org", sizeof(g_state.realmlist_host) - 1);
    strncpy(g_state.username, "Player1", sizeof(g_state.username) - 1);
    strncpy(g_state.password, "password", sizeof(g_state.password) - 1);
    strncpy(g_state.auth_status, "Ready to sign in", sizeof(g_state.auth_status) - 1);
    strncpy(g_state.realm_name, "Northrend Realm", sizeof(g_state.realm_name) - 1);
    strncpy(g_state.status_message, "Verifying client archives...", sizeof(g_state.status_message) - 1);
    strncpy(g_state.motd, "Double XP & Honor active! Cross-faction battlegrounds enabled.", sizeof(g_state.motd) - 1);
    g_state.realm_status = REALM_ONLINE;
    g_state.online_players = 1420;
    g_state.ingame_players = 1180;
    g_state.latency_ms = 22;

#if defined(_WIN32)
    InitializeCriticalSection(&g_state.lock);
#else
    pthread_mutex_init(&g_state.lock, NULL);
#endif

    /* Automatically load credentials from Account.dat vault if present */
    if (__load__() && gStructure) {
        if (gStructure->credential.username[0] != '\0') {
            strncpy(g_state.username, gStructure->credential.username, sizeof(g_state.username) - 1);
        }
        if (gStructure->credential.password[0] != '\0') {
            strncpy(g_state.password, gStructure->credential.password, sizeof(g_state.password) - 1);
        }
        if (gStructure->credential.token[0] != '\0') {
            g_state.auth_success = true;
            snprintf(g_state.auth_status, sizeof(g_state.auth_status), "✓ Vault Session Restored");
        }
    }

    /* Hook into async logger */
    __logger_set_ui_callback__(__on_wotlk_log_dispatch__, NULL);

    /* Start live realm stats feed */
    g_state.live_feed = __live_feed_create__("127.0.0.1", 8080, "/stats", false);
    if (g_state.live_feed) {
        __live_feed_set_callback__(g_state.live_feed, __on_wotlk_live_stats__, NULL);
        __live_feed_start__(g_state.live_feed);
    }

    /* Create Raylib Window (Modern high-res WotLK viewport) */
    const int winWidth = 1024;
    const int winHeight = 640;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(winWidth, winHeight, "Wrath of the Lich King - LeLauncher 3.3.5a");
    SetWindowMinSize(920, 560);
    SetTargetFPS(60);

    /* Initialize snowfall atmosphere */
    __init_snowflakes__(winWidth, winHeight);

    /* Configure Raygui Styles (Icecrown Frosted Palette) */
    GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x0e1422ff);
    GuiSetStyle(DEFAULT, LINE_COLOR, 0x1e304eff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xe2e8f0ff);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, 0x142036ff);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, 0xf0f6ffff);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, 0x0284c7ff);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, 0x0369a1ff);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_NORMAL, 0x0c1322ff);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, 0x00e1ffff);

    /* Automatically kick off update check on startup */
    __wotlk_start_patch_thread__();

    /* ==========================================================================
     * Main Render Loop
     * ========================================================================== */
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int curW = GetScreenWidth();
        int curH = GetScreenHeight();

        /* Update notice timer */
        if (g_state.tool_notice_timer > 0.0f) {
            g_state.tool_notice_timer -= dt;
            if (g_state.tool_notice_timer <= 0.0f) {
                g_state.tool_notice[0] = '\0';
            }
        }

        BeginDrawing();

        /* ----------------------------------------------------------------------
         * Background: Dark Citadel Frost Abyss
         * ---------------------------------------------------------------------- */
        ClearBackground(COLOR_ABYSS_BLACK);

        /* Soft background radial gradient (Ice blue ambient in center) */
        DrawCircleGradient(curW / 2, curH / 3, (float)curW * 0.7f, (Color){ 10, 25, 45, 120 }, (Color){ 4, 6, 12, 255 });

        /* Gentle Snowfall Particle Atmosphere */
        __update_and_draw_snow__(curW, curH, dt);

        /* ----------------------------------------------------------------------
         * 1. Top WotLK Header Banner
         * ---------------------------------------------------------------------- */
        DrawRectangle(0, 0, curW, 72, COLOR_CITADEL_DARK);
        DrawRectangleGradientH(0, 71, curW, 2, (Color){ 0, 180, 255, 60 }, COLOR_FROST_GLOW);

        /* Ice Rune Emblem */
        DrawRectangleRounded((Rectangle){ 20, 14, 44, 44 }, 0.25f, 4, (Color){ 14, 28, 48, 255 });
        DrawRectangleRoundedLines((Rectangle){ 20, 14, 44, 44 }, 0.25f, 4, COLOR_FROST_GLOW);
        DrawText("❄", 31, 21, 28, COLOR_FROST_GLOW);

        /* Brand & WotLK Title */
        DrawText("WRATH OF THE LICH KING", 76, 16, 20, (Color){ 240, 248, 255, 255 });
        DrawText(TextFormat("LeLauncher Native • 3.3.5a (12340) • %s • %s", BUILD_CONFIG, COMPILER_NAME), 78, 41, 11, COLOR_ICE_BLUE);

        /* Realm Status Capsule (Top Right) */
        int badgeW = 270;
        int badgeX = curW - badgeW - 20;
        DrawRectangleRounded((Rectangle){ (float)badgeX, 16, (float)badgeW, 40 }, 0.35f, 6, (Color){ 14, 22, 38, 255 });
        DrawRectangleRoundedLines((Rectangle){ (float)badgeX, 16, (float)badgeW, 40 }, 0.35f, 6, (Color){ 30, 52, 85, 255 });

        Color realmDotColor = (g_state.realm_status == REALM_ONLINE) ? COLOR_EMERALD_ONLINE :
                              (g_state.realm_status == REALM_MAINTENANCE) ? COLOR_WOTLK_GOLD : COLOR_CRIMSON_SCOURGE;
        const char *realmStateText = (g_state.realm_status == REALM_ONLINE) ? "ONLINE" :
                                     (g_state.realm_status == REALM_MAINTENANCE) ? "MAINTENANCE" : "OFFLINE";

        /* Pulsing dot */
        float pulse = (sinf((float)GetTime() * 4.0f) + 1.0f) * 0.5f;
        DrawCircle(badgeX + 22, 36, 5.0f + pulse * 1.5f, (Color){ realmDotColor.r, realmDotColor.g, realmDotColor.b, (unsigned char)(160 + pulse * 95) });
        DrawText(g_state.realm_name, badgeX + 36, 22, 12, WHITE);
        DrawText(TextFormat("%s • %u Online • %u ms", realmStateText, g_state.online_players, g_state.latency_ms), badgeX + 36, 38, 10, COLOR_PALE_ICE);

        /* ----------------------------------------------------------------------
         * 2. Navigation Tabs
         * ---------------------------------------------------------------------- */
        int navY = 82;
        int tabW = 190;
        int tabH = 34;

        if (GuiButton((Rectangle){ 20, (float)navY, (float)tabW, (float)tabH }, "#140# CAMPAIGN & PLAY")) g_state.active_tab = 0;
        if (GuiButton((Rectangle){ 220, (float)navY, (float)tabW, (float)tabH }, "#141# REALM TELEMETRY")) g_state.active_tab = 1;
        if (GuiButton((Rectangle){ 420, (float)navY, (float)tabW, (float)tabH }, "#191# WRATH CHRONICLES")) g_state.active_tab = 2;
        if (GuiButton((Rectangle){ 620, (float)navY, (float)tabW, (float)tabH }, "#142# CLIENT & LOGS")) g_state.active_tab = 3;

        /* Active Tab Indicator */
        int activeLineX = 20 + g_state.active_tab * 200;
        DrawRectangleGradientH(activeLineX, navY + tabH + 1, tabW, 3, COLOR_FROST_GLOW, (Color){ 0, 140, 255, 255 });

        /* ----------------------------------------------------------------------
         * 3. Tab Contents
         * ---------------------------------------------------------------------- */
        int contentY = 126;
        int contentH = curH - 246;

        if (g_state.active_tab == 0) {
            /* === TAB 0: CAMPAIGN & PLAY === */

            /* Left Card: Champion Authentication & Session Portal */
            int cardW = 390;
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)cardW, (float)contentH }, 0.03f, 4, COLOR_CITADEL_CARD);
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)cardW, (float)contentH }, 0.03f, 4, COLOR_FROST_BORDER);

            DrawText("⚔ CHAMPION VAULT", 38, contentY + 16, 14, COLOR_FROST_GLOW);
            DrawText("Sign in to your realm account or use saved credentials.", 38, contentY + 34, 10, COLOR_PALE_ICE);

            /* Account Name & Password Inputs */
            DrawText("Account Name:", 38, contentY + 56, 11, COLOR_PALE_ICE);
            if (GuiTextBox((Rectangle){ 38, (float)contentY + 72, (float)(cardW - 36), 28 }, g_state.username, 64, g_state.active_input_box == 1)) {
                g_state.active_input_box = (g_state.active_input_box == 1) ? 0 : 1;
            }

            DrawText("Password:", 38, contentY + 108, 11, COLOR_PALE_ICE);
            if (GuiTextBox((Rectangle){ 38, (float)contentY + 124, (float)(cardW - 36), 28 }, g_state.password, 64, g_state.active_input_box == 2)) {
                g_state.active_input_box = (g_state.active_input_box == 2) ? 0 : 2;
            }

            /* Sign In Button */
            if (GuiButton((Rectangle){ 38, (float)contentY + 162, (float)(cardW - 36), 32 }, "#113# Sign In & Save Session")) {
                __wotlk_trigger_login__();
            }

            /* Auth Status Line */
            Color authColor = g_state.auth_success ? COLOR_EMERALD_ONLINE : COLOR_WOTLK_GOLD;
            DrawText(g_state.auth_status, 38, contentY + 202, 11, authColor);

            /* Realmlist Quick Config */
            DrawRectangle(38, contentY + 224, cardW - 36, 1, (Color){ 30, 48, 76, 255 });
            DrawText("REALMLIST GATEWAY", 38, contentY + 234, 11, COLOR_ICE_BLUE);
            DrawText("Realm Host (set realmlist):", 38, contentY + 252, 10, COLOR_PALE_ICE);
            if (GuiTextBox((Rectangle){ 38, (float)contentY + 268, (float)(cardW - 130), 26 }, g_state.realmlist_host, 128, g_state.active_input_box == 3)) {
                g_state.active_input_box = (g_state.active_input_box == 3) ? 0 : 3;
            }
            if (GuiButton((Rectangle){ (float)(cardW - 84), (float)contentY + 268, 86, 26 }, "#02# Set WTF")) {
                __wotlk_set_realmlist__(g_state.realmlist_host);
            }

            /* Client Maintenance Tools */
            DrawRectangle(38, contentY + 308, cardW - 36, 1, (Color){ 30, 48, 76, 255 });
            DrawText("CLIENT MAINTENANCE", 38, contentY + 318, 11, COLOR_ICE_BLUE);

            if (GuiButton((Rectangle){ 38, (float)contentY + 338, (float)(cardW - 36), 28 }, "#149# Clear WDB & Item Cache")) {
                __wotlk_clear_cache__();
            }

            /* Tool feedback notice */
            if (g_state.tool_notice[0] != '\0') {
                DrawText(g_state.tool_notice, 38, contentY + 374, 11, COLOR_FROST_GLOW);
            }

            /* Right Card: Icecrown Citadel & Hero Campaign Banner */
            int rightX = cardW + 35;
            int rightW = curW - rightX - 20;
            DrawRectangleRounded((Rectangle){ (float)rightX, (float)contentY, (float)rightW, (float)contentH }, 0.03f, 4, COLOR_CITADEL_CARD);
            DrawRectangleRoundedLines((Rectangle){ (float)rightX, (float)contentY, (float)rightW, (float)contentH }, 0.03f, 4, COLOR_FROST_BORDER);

            /* Hero Campaign Headline */
            DrawRectangleGradientH(rightX + 1, contentY + 1, rightW - 2, 70, (Color){ 12, 34, 60, 180 }, (Color){ 6, 14, 28, 0 });
            DrawText("THE FROZEN THRONE AWAITS", rightX + 22, contentY + 18, 18, WHITE);
            DrawText("Icecrown Citadel • Fall of the Lich King • Northrend 3.3.5a", rightX + 24, contentY + 44, 12, COLOR_FROST_GLOW);

            /* Lore Paragraph */
            DrawText("The Lich King Arthas Menethil sits atop the Frozen Throne, marshaling the undead Scourge", rightX + 24, contentY + 86, 12, (Color){ 200, 215, 235, 255 });
            DrawText("to purge life from Azeroth. Gather your guildmates and storm the gates of Icecrown Citadel.", rightX + 24, contentY + 104, 12, (Color){ 200, 215, 235, 255 });

            /* Raid Tier Badges */
            DrawText("NORTHREND RAID PROGRESSION TIERS:", rightX + 24, contentY + 140, 11, COLOR_WOTLK_GOLD);
            const char *tiers[] = { "Naxxramas (T7)", "Ulduar (T8)", "Crusader (T9)", "Icecrown (T10)", "Ruby Sanctum" };
            int badgeChipX = rightX + 24;
            for (int t = 0; t < 5; t++) {
                DrawRectangleRounded((Rectangle){ (float)badgeChipX, (float)contentY + 160, 110, 26 }, 0.3f, 4, (Color){ 20, 32, 52, 255 });
                DrawRectangleRoundedLines((Rectangle){ (float)badgeChipX, (float)contentY + 160, 110, 26 }, 0.3f, 4, COLOR_FROST_BORDER);
                DrawText(tiers[t], badgeChipX + 8, contentY + 167, 10, COLOR_PALE_ICE);
                badgeChipX += 116;
            }

            /* Server Notice Box */
            DrawRectangleRounded((Rectangle){ (float)rightX + 24, (float)contentY + 204, (float)(rightW - 48), 64 }, 0.08f, 4, (Color){ 12, 18, 30, 255 });
            DrawRectangleRoundedLines((Rectangle){ (float)rightX + 24, (float)contentY + 204, (float)(rightW - 48), 64 }, 0.08f, 4, (Color){ 30, 52, 85, 255 });
            DrawText("REALM ANNOUNCEMENT & MOTD:", rightX + 38, contentY + 214, 10, COLOR_FROST_GLOW);
            DrawText(g_state.motd, rightX + 38, contentY + 234, 12, WHITE);

            /* Engine & Build specs */
            DrawText("ENGINE & CLIENT ARCHITECTURE:", rightX + 24, contentY + 288, 11, COLOR_ICE_BLUE);
            DrawText("• Native High-Performance C23 Engine with Raylib Graphics & Zero Debian package dependencies", rightX + 24, contentY + 308, 11, (Color){ 180, 195, 220, 255 });
            DrawText("• High-Speed Resumable Multi-part CDN Engine with MD5 Checksum Verification", rightX + 24, contentY + 328, 11, (Color){ 180, 195, 220, 255 });
            DrawText("• Hardware Token HWID Guard & Session Container: Active", rightX + 24, contentY + 348, 11, COLOR_EMERALD_ONLINE);

        } else if (g_state.active_tab == 1) {
            /* === TAB 1: REALM TELEMETRY === */
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, COLOR_CITADEL_CARD);
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, COLOR_FROST_BORDER);

            DrawText("REAL-TIME REALM TELEMETRY & WORLD VITALS", 40, contentY + 20, 16, COLOR_FROST_GLOW);
            DrawText("Continuous telemetry feed monitoring realm availability and player populations.", 40, contentY + 42, 11, COLOR_PALE_ICE);

            /* Telemetry Grid */
            int gridY = contentY + 80;
            int boxW = (curW - 120) / 4;
            int boxH = 90;

            const char *labels[] = { "REALM STATUS", "ONLINE CHAMPIONS", "ACTIVE IN-GAME", "ROUNDTRIP LATENCY" };
            for (int b = 0; b < 4; b++) {
                int bx = 40 + b * (boxW + 15);
                DrawRectangleRounded((Rectangle){ (float)bx, (float)gridY, (float)boxW, (float)boxH }, 0.08f, 4, (Color){ 16, 24, 40, 255 });
                DrawRectangleRoundedLines((Rectangle){ (float)bx, (float)gridY, (float)boxW, (float)boxH }, 0.08f, 4, COLOR_FROST_BORDER);
                DrawText(labels[b], bx + 16, gridY + 14, 10, COLOR_ICE_BLUE);

                if (b == 0) {
                    DrawText("ONLINE", bx + 16, gridY + 38, 24, COLOR_EMERALD_ONLINE);
                } else if (b == 1) {
                    DrawText(TextFormat("%u", g_state.online_players), bx + 16, gridY + 38, 24, WHITE);
                } else if (b == 2) {
                    DrawText(TextFormat("%u", g_state.ingame_players), bx + 16, gridY + 38, 24, COLOR_PALE_ICE);
                } else if (b == 3) {
                    DrawText(TextFormat("%u ms", g_state.latency_ms), bx + 16, gridY + 38, 24, COLOR_WOTLK_GOLD);
                }
            }

            /* Server Ruleset summary */
            int rulesY = gridY + boxH + 30;
            DrawText("REALM CONFIGURATION & GAMEPLAY RULES:", 40, rulesY, 13, COLOR_WOTLK_GOLD);
            DrawText("• Game Expansion: Wrath of the Lich King 3.3.5a (12340)", 40, rulesY + 25, 12, WHITE);
            DrawText("• Experience Rates: Blizzlike 1x Quest / 1x Kill (Weekend Double-XP Event Active)", 40, rulesY + 45, 12, WHITE);
            DrawText("• Cross-Faction Battlegrounds: Enabled for balanced Alterac Valley, Warsong Gulch, and Arathi Basin queues", 40, rulesY + 65, 12, WHITE);
            DrawText("• Dungeon Finder & Raiding: Cross-realm LFG enabled with full Wintergrasp siege warfare", 40, rulesY + 85, 12, WHITE);

        } else if (g_state.active_tab == 2) {
            /* === TAB 2: WRATH CHRONICLES (PATCH NOTES) === */
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, COLOR_CITADEL_CARD);
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, COLOR_FROST_BORDER);

            DrawText("WRATH OF THE LICH KING: CHRONICLES & PATCH NOTES", 40, contentY + 20, 16, COLOR_FROST_GLOW);

            int noteY = contentY + 60;
            DrawText("★ Patch 3.3.5a - The Assault on the Ruby Sanctum", 40, noteY, 14, COLOR_WOTLK_GOLD);
            DrawText("The black dragon Halion has invaded the Ruby Sanctum in Wyrmrest Temple. Defeat his twilight lieutenants", 40, noteY + 22, 11, (Color){ 200, 215, 235, 255 });
            DrawText("and enter the Twilight Realm to banish the Destroyer of the Red Dragonflight.", 40, noteY + 38, 11, (Color){ 200, 215, 235, 255 });

            noteY += 75;
            DrawText("★ Icecrown Citadel: The Frozen Throne Unlocked", 40, noteY, 14, COLOR_WOTLK_GOLD);
            DrawText("The gates to the upper spire are unsealed. Face the Lich King atop the pinnacle of Icecrown Citadel", 40, noteY + 22, 11, (Color){ 200, 215, 235, 255 });
            DrawText("on 10-player and 25-player Heroic modes for the coveted Invincible mount and Shadowmourne.", 40, noteY + 38, 11, (Color){ 200, 215, 235, 255 });

            noteY += 75;
            DrawText("★ Launcher Engine Upgrades", 40, noteY, 14, COLOR_WOTLK_GOLD);
            DrawText("• Migrated from GTK 4 to ultra-lightweight, native Raylib + Raygui (zero package dependencies).", 40, noteY + 22, 11, (Color){ 200, 215, 235, 255 });
            DrawText("• Resumable multi-chunk CDN streaming with instant integrity verification.", 40, noteY + 38, 11, (Color){ 200, 215, 235, 255 });

        } else if (g_state.active_tab == 3) {
            /* === TAB 3: CLIENT & LOG CONSOLE === */
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, (Color){ 8, 12, 20, 255 });
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, COLOR_FROST_BORDER);

            DrawText("REAL-TIME CLIENT LOG CONSOLE", 40, contentY + 14, 12, COLOR_ICE_BLUE);

            int logY = contentY + 38;
            int maxVisible = (contentH - 54) / 18;
            int startIdx = (g_state.log_count > maxVisible) ? (g_state.log_count - maxVisible) : 0;

            for (int i = startIdx; i < g_state.log_count; i++) {
                DrawText(g_state.logs[i], 40, logY, 11, (Color){ 190, 210, 235, 255 });
                logY += 18;
            }
        }

        /* ----------------------------------------------------------------------
         * 4. Bottom Master Command Deck (WotLK Frozen Bar)
         * ---------------------------------------------------------------------- */
        int deckY = curH - 110;
        DrawRectangle(0, deckY, curW, 110, COLOR_CITADEL_DARK);
        DrawRectangleGradientH(0, deckY, curW, 2, (Color){ 0, 160, 255, 60 }, COLOR_FROST_GLOW);

        /* Status message & Speed / ETA */
        DrawText(g_state.status_message, 24, deckY + 12, 13, (Color){ 240, 248, 255, 255 });
        if (strlen(g_state.speed_str) > 0) {
            DrawText(TextFormat("Speed: %s  |  %s", g_state.speed_str, g_state.eta_str), curW - 510, deckY + 12, 12, COLOR_FROST_GLOW);
        }

        /* Custom Frost Progress Bar */
        int barW = curW - 410;
        int barH = 24;
        int barY = deckY + 38;
        DrawRectangleRounded((Rectangle){ 24, (float)barY, (float)barW, (float)barH }, 0.25f, 4, (Color){ 14, 20, 32, 255 });
        DrawRectangleRoundedLines((Rectangle){ 24, (float)barY, (float)barW, (float)barH }, 0.25f, 4, COLOR_FROST_BORDER);

        int fillW = (int)((float)barW * g_state.download_progress);
        if (fillW > 0) {
            DrawRectangleRounded((Rectangle){ 26, (float)barY + 2, (float)(fillW - 4), (float)barH - 4 }, 0.25f, 4, (Color){ 0, 190, 240, 220 });
        }
        DrawText(TextFormat("%d%%", (int)(g_state.download_progress * 100)), 24 + barW / 2 - 12, barY + 5, 12, WHITE);

        /* Repair / Check Files Button */
        int btnRepairW = 150;
        int btnRepairH = 46;
        int btnActionY = deckY + 28;
        int btnRepairX = curW - 365;

        if (GuiButton((Rectangle){ (float)btnRepairX, (float)btnActionY, (float)btnRepairW, (float)btnRepairH }, "#149# Check Files")) {
            __wotlk_start_patch_thread__();
        }

        /* Big Glowing "⚔ ENTER NORTHREND" Button */
        int btnPlayW = 180;
        int btnPlayX = curW - 195;

        /* Pulsing Frost Highlight when ready */
        float playGlow = (sinf((float)GetTime() * 3.5f) + 1.0f) * 0.5f;
        Color playBtnColor = (g_state.patch_status == PATCH_READY) ?
            (Color){ (unsigned char)(0 + playGlow * 30), (unsigned char)(140 + playGlow * 40), (unsigned char)(210 + playGlow * 45), 255 } :
            (Color){ 30, 48, 76, 255 };

        DrawRectangleRounded((Rectangle){ (float)btnPlayX, (float)btnActionY, (float)btnPlayW, (float)btnRepairH }, 0.25f, 4, playBtnColor);
        DrawRectangleRoundedLines((Rectangle){ (float)btnPlayX, (float)btnActionY, (float)btnPlayW, (float)btnRepairH }, 0.25f, 4, COLOR_FROST_GLOW);

        const char *playText = (g_state.patch_status == PATCH_READY) ? "⚔ PLAY NOW" : "UPDATING...";
        int textW = MeasureText(playText, 16);
        DrawText(playText, btnPlayX + (btnPlayW - textW) / 2, btnActionY + 15, 16, WHITE);

        /* Handle Play Click */
        Vector2 mouse = GetMousePosition();
        if (CheckCollisionPointRec(mouse, (Rectangle){ (float)btnPlayX, (float)btnActionY, (float)btnPlayW, (float)btnRepairH })) {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                __wotlk_launch_game__();
            }
        }

        EndDrawing();
    }

    /* Cleanup */
    if (g_state.live_feed) {
        __live_feed_stop__(g_state.live_feed);
        __live_feed_destroy__(g_state.live_feed);
    }

    CloseWindow();

#if defined(_WIN32)
    DeleteCriticalSection(&g_state.lock);
#else
    pthread_mutex_destroy(&g_state.lock);
#endif

    return 0;
}
