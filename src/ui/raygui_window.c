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

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <unistd.h>
#endif

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define MAX_LOG_LINES 64
#define LOG_LINE_LEN  256

/* ==============================================================================
 * Launcher State Architecture
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
    /* Credentials */
    char server_url[128];
    char username[64];
    char password[64];
    char auth_status[128];
    bool auth_success;
    int  active_input_box; /* 0 = none, 1 = server, 2 = user, 3 = pass */

    /* Realm / Live Feed */
    char realm_name[64];
    int  realm_status;
    unsigned int online_players;
    unsigned int ingame_players;
    unsigned int latency_ms;
    char motd[256];

    /* Patching & Downloads */
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
    int   log_scroll_index;

    /* UI navigation */
    int   active_tab; /* 0 = Launcher, 1 = Server Info, 2 = Logs */

#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t  lock;
#endif

    LiveFeedClient  *live_feed;
} RayguiLauncherState;

static RayguiLauncherState g_state;

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
 * Logger Dispatch Handler
 * ============================================================================== */
static void __on_raygui_log_dispatch__(LogLevel level, const char *system, const char *formatted, void *user_data) {
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
static void __on_raygui_live_stats__(const LiveGameStats *stats, void *user_data) {
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
 * Download Progress Callback for Raygui
 * ============================================================================== */
typedef struct {
    size_t file_index;
    size_t total_files;
    const char *current_file;
} RayguiDlContext;

static int __on_raygui_download_progress__(const CURLProgress *prog, void *user_data) {
    RayguiDlContext *dc = (RayguiDlContext *)user_data;
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
 * Asynchronous Background Patch Worker
 * ============================================================================== */
static void *__raygui_patch_worker__(void *arg) {
    (void)arg;
    LOG_INFO("PATCH", "Background patch worker thread started");

    __state_lock__();
    g_state.patch_status = PATCH_CHECKING;
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Reading manifest files...");
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
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Checking manifest: %s", partial_file);
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
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Downloading manifest from CDN...");
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
        g_state.patch_status = PATCH_ERROR;
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Failed to load manifest: %s", partial_file);
        g_state.patch_worker_running = false;
        __state_unlock__();
        if (has_master) __mfil_free_master__(&master);
        return NULL;
    }

    __state_lock__();
    g_state.patch_status = PATCH_VERIFYING;
    snprintf(g_state.status_message, sizeof(g_state.status_message), "Verifying %zu game assets...", manifest.file_count);
    __state_unlock__();

    size_t verified = 0;
    size_t downloaded = 0;

    for (size_t i = 0; i < manifest.file_count; i++) {
        if (g_state.patch_cancel_requested) break;

        const MfilFile *file = &manifest.files[i];

        __state_lock__();
        snprintf(g_state.current_file, sizeof(g_state.current_file), "%s", file->name);
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Checking: %s (%zu/%zu)", file->name, i + 1, manifest.file_count);
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

        RayguiDlContext dc = {
            .file_index = i,
            .total_files = manifest.file_count,
            .current_file = file->name
        };
        opts.on_progress = __on_raygui_download_progress__;
        opts.user_data = &dc;

        CURLResult dl_res;
        CURLStatus dl_st = __mfil_download_file__(base_location, &manifest, file, NULL, &opts, &dl_res);
        if (dl_st == CURLDL_OK || dl_st == CURLDL_ALREADY_COMPLETE) {
            downloaded++;
            verified++;
        }
    }

    __state_lock__();
    if (verified == manifest.file_count) {
        g_state.patch_status = PATCH_READY;
        g_state.download_progress = 1.0f;
        snprintf(g_state.status_message, sizeof(g_state.status_message), "All %zu game files verified! Ready to play.", verified);
        snprintf(g_state.current_file, sizeof(g_state.current_file), "None (Complete)");
    } else {
        g_state.patch_status = PATCH_ERROR;
        snprintf(g_state.status_message, sizeof(g_state.status_message), "Patch incomplete (%zu/%zu verified)", verified, manifest.file_count);
    }
    g_state.speed_str[0] = '\0';
    g_state.eta_str[0] = '\0';
    g_state.patch_worker_running = false;
    __state_unlock__();

    __mfil_free_manifest__(&manifest);
    if (has_master) __mfil_free_master__(&master);

    LOG_INFO("PATCH", "Patch check finished (%zu verified, %zu downloaded)", verified, downloaded);
    return NULL;
}

static void __raygui_start_patch_thread__(void) {
    if (g_state.patch_worker_running) return;

    g_state.patch_worker_running = true;
    g_state.patch_cancel_requested = false;

#if defined(_WIN32)
    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)__raygui_patch_worker__, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, __raygui_patch_worker__, NULL);
    pthread_detach(thread);
#endif
}

/* ==============================================================================
 * Login Verification Action
 * ============================================================================== */
static void __raygui_trigger_login__(void) {
    LOG_INFO("AUTH", "Attempting login for user: %s", g_state.username);
    __state_lock__();
    snprintf(g_state.auth_status, sizeof(g_state.auth_status), "Authenticating...");
    __state_unlock__();

    const char *hwid = __hwid_get__();
    char ip[64] = "127.0.0.1";
    __network_get_client_ip__(ip, sizeof(ip));

    ApiPreAuthResult preauth = __api_request_temporary_token__(g_state.server_url, hwid, ip);
    if (!preauth.success) {
        __state_lock__();
        g_state.auth_success = false;
        snprintf(g_state.auth_status, sizeof(g_state.auth_status), "Handshake failed: %s", preauth.error_message);
        __state_unlock__();
        return;
    }

    ApiAuthResult auth = __api_authenticate_credentials__(g_state.server_url, preauth.temporary_token, g_state.username, g_state.password, hwid);

    __state_lock__();
    if (auth.success) {
        g_state.auth_success = true;
        snprintf(g_state.auth_status, sizeof(g_state.auth_status), "✓ Authenticated (%s)", auth.account_id);
        LOG_INFO("AUTH", "Authentication success for %s (Account: %s)", g_state.username, auth.account_id);

        /* Persist credentials to Account.dat if container is available */
        if (gStructure) {
            strncpy(gStructure->credential.username, g_state.username, sizeof(gStructure->credential.username) - 1);
            strncpy(gStructure->credential.password, g_state.password, sizeof(gStructure->credential.password) - 1);
            strncpy(gStructure->credential.token, auth.permanent_token, sizeof(gStructure->credential.token) - 1);
            __save__();
        }
    } else {
        g_state.auth_success = false;
        snprintf(g_state.auth_status, sizeof(g_state.auth_status), "✗ Failed: %s", auth.error_message[0] ? auth.error_message : "Connection error");
        LOG_WARN("AUTH", "Authentication failed: %s", g_state.auth_status);
    }
    __state_unlock__();
}

/* ==============================================================================
 * Main Raygui Window Lifecycle
 * ============================================================================== */
int raygui_window_run(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Initialize internal state */
    memset(&g_state, 0, sizeof(g_state));
    strncpy(g_state.server_url, "http://127.0.0.1/api/login", sizeof(g_state.server_url) - 1);
    strncpy(g_state.username, "Player1", sizeof(g_state.username) - 1);
    strncpy(g_state.password, "password", sizeof(g_state.password) - 1);
    strncpy(g_state.auth_status, "Ready to login", sizeof(g_state.auth_status) - 1);
    strncpy(g_state.realm_name, "Northrend Realm", sizeof(g_state.realm_name) - 1);
    strncpy(g_state.status_message, "Ready to check updates", sizeof(g_state.status_message) - 1);
    strncpy(g_state.motd, "Welcome to LeLauncher! Double-XP event this weekend.", sizeof(g_state.motd) - 1);
    g_state.realm_status = REALM_ONLINE;
    g_state.online_players = 1420;
    g_state.ingame_players = 1180;
    g_state.latency_ms = 24;

#if defined(_WIN32)
    InitializeCriticalSection(&g_state.lock);
#else
    pthread_mutex_init(&g_state.lock, NULL);
#endif

    /* Hook into async logger */
    __logger_set_ui_callback__(__on_raygui_log_dispatch__, NULL);

    /* Start live realm stats feed */
    g_state.live_feed = __live_feed_create__("127.0.0.1", 8080, "/stats", false);
    if (g_state.live_feed) {
        __live_feed_set_callback__(g_state.live_feed, __on_raygui_live_stats__, NULL);
        __live_feed_start__(g_state.live_feed);
    }

    /* Create Raylib Window */
    const int winWidth = 920;
    const int winHeight = 580;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(winWidth, winHeight, "LeLauncher - Game Content Delivery Client");
    SetWindowMinSize(800, 500);
    SetTargetFPS(60);

    /* Configure Raygui Styles (Dark Modern Theme) */
    GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x181a24ff);
    GuiSetStyle(DEFAULT, LINE_COLOR, 0x2e344aff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xe2e8f0ff);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, 0x242838ff);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, 0xffffffff);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, 0x0088ccff);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, 0x006699ff);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_NORMAL, 0x1a1e2bff);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, 0x00d2ffff);

    /* Automatically kick off update check on startup */
    __raygui_start_patch_thread__();

    /* ==========================================================================
     * Main Render Loop
     * ========================================================================== */
    while (!WindowShouldClose()) {
        int curW = GetScreenWidth();
        int curH = GetScreenHeight();

        BeginDrawing();
        ClearBackground((Color){ 20, 22, 32, 255 });

        /* ----------------------------------------------------------------------
         * 1. Top Header Banner
         * ---------------------------------------------------------------------- */
        DrawRectangle(0, 0, curW, 60, (Color){ 14, 16, 24, 255 });
        DrawRectangle(0, 59, curW, 1, (Color){ 45, 50, 70, 255 });

        /* Brand & Title */
        DrawRectangleRounded((Rectangle){ 18, 14, 32, 32 }, 0.25f, 4, (Color){ 0, 180, 255, 255 });
        DrawText("L", 28, 18, 24, WHITE);
        DrawText("LeLauncher", 60, 15, 20, WHITE);
        DrawText(TextFormat("v%s • %s • %s [Raygui]", APP_VERSION_STRING, BUILD_CONFIG, COMPILER_NAME), 62, 37, 11, (Color){ 140, 150, 175, 255 });

        /* Top Right Realm Status Badge */
        Color statusBadgeColor = (g_state.realm_status == REALM_ONLINE) ? (Color){ 34, 197, 94, 255 } :
                                 (g_state.realm_status == REALM_MAINTENANCE) ? (Color){ 234, 179, 8, 255 } : (Color){ 239, 68, 68, 255 };
        const char *statusText = (g_state.realm_status == REALM_ONLINE) ? "ONLINE" :
                                 (g_state.realm_status == REALM_MAINTENANCE) ? "MAINTENANCE" : "OFFLINE";

        int badgeX = curW - 240;
        DrawRectangleRounded((Rectangle){ (float)badgeX, 16, 220, 28 }, 0.3f, 4, (Color){ 26, 30, 44, 255 });
        DrawCircle(badgeX + 16, 30, 5, statusBadgeColor);
        DrawText(TextFormat("%s: %s (%d ms)", g_state.realm_name, statusText, g_state.latency_ms), badgeX + 28, 23, 11, (Color){ 210, 220, 240, 255 });

        /* ----------------------------------------------------------------------
         * 2. Navigation Tabs
         * ---------------------------------------------------------------------- */
        int tabY = 70;
        if (GuiButton((Rectangle){ 20, (float)tabY, 130, 32 }, "#140# Launcher")) g_state.active_tab = 0;
        if (GuiButton((Rectangle){ 155, (float)tabY, 130, 32 }, "#141# Server Stats")) g_state.active_tab = 1;
        if (GuiButton((Rectangle){ 290, (float)tabY, 130, 32 }, "#142# Live Logs")) g_state.active_tab = 2;

        /* Highlight active tab */
        int activeLineX = 20 + g_state.active_tab * 135;
        DrawRectangle(activeLineX, tabY + 31, 130, 3, (Color){ 0, 180, 255, 255 });

        /* ----------------------------------------------------------------------
         * 3. Tab Contents
         * ---------------------------------------------------------------------- */
        int contentY = 115;
        int contentH = curH - 240;

        if (g_state.active_tab == 0) {
            /* === LAUNCHER / DASHBOARD TAB === */

            /* Left Card: Account & Authentication */
            int cardW = 380;
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)cardW, (float)contentH }, 0.03f, 4, (Color){ 26, 30, 44, 255 });
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)cardW, (float)contentH }, 0.03f, 4, (Color){ 42, 48, 70, 255 });

            DrawText("ACCOUNT & AUTHENTICATION", 38, contentY + 16, 13, (Color){ 0, 180, 255, 255 });

            DrawText("Login Server Endpoint:", 38, contentY + 45, 12, (Color){ 160, 170, 190, 255 });
            if (GuiTextBox((Rectangle){ 38, (float)contentY + 62, (float)(cardW - 36), 28 }, g_state.server_url, 128, g_state.active_input_box == 1)) {
                g_state.active_input_box = (g_state.active_input_box == 1) ? 0 : 1;
            }

            DrawText("Username:", 38, contentY + 100, 12, (Color){ 160, 170, 190, 255 });
            if (GuiTextBox((Rectangle){ 38, (float)contentY + 117, (float)(cardW - 36), 28 }, g_state.username, 64, g_state.active_input_box == 2)) {
                g_state.active_input_box = (g_state.active_input_box == 2) ? 0 : 2;
            }

            DrawText("Password:", 38, contentY + 155, 12, (Color){ 160, 170, 190, 255 });
            if (GuiTextBox((Rectangle){ 38, (float)contentY + 172, (float)(cardW - 36), 28 }, g_state.password, 64, g_state.active_input_box == 3)) {
                g_state.active_input_box = (g_state.active_input_box == 3) ? 0 : 3;
            }

            if (GuiButton((Rectangle){ 38, (float)contentY + 215, (float)(cardW - 36), 34 }, "#113# Verify & Log In")) {
                __raygui_trigger_login__();
            }

            Color authColor = g_state.auth_success ? (Color){ 34, 197, 94, 255 } : (Color){ 245, 175, 40, 255 };
            DrawText(g_state.auth_status, 38, contentY + 260, 12, authColor);

            /* Right Card: News & Patch Notes */
            int rightX = cardW + 35;
            int rightW = curW - rightX - 20;
            DrawRectangleRounded((Rectangle){ (float)rightX, (float)contentY, (float)rightW, (float)contentH }, 0.03f, 4, (Color){ 26, 30, 44, 255 });
            DrawRectangleRoundedLines((Rectangle){ (float)rightX, (float)contentY, (float)rightW, (float)contentH }, 0.03f, 4, (Color){ 42, 48, 70, 255 });

            DrawText("SERVER ANNOUNCEMENT & MOTD", rightX + 18, contentY + 16, 13, (Color){ 0, 180, 255, 255 });
            DrawRectangle(rightX + 18, contentY + 45, rightW - 36, 60, (Color){ 18, 20, 30, 255 });
            DrawText(g_state.motd, rightX + 28, contentY + 62, 12, (Color){ 230, 235, 245, 255 });

            DrawText("GAME CLIENT OVERVIEW", rightX + 18, contentY + 120, 13, (Color){ 0, 180, 255, 255 });
            DrawText("Client Version: 3.3.5a (12340)", rightX + 18, contentY + 145, 12, (Color){ 200, 210, 230, 255 });
            DrawText("Engine Mode: Native High-Performance C23", rightX + 18, contentY + 165, 12, (Color){ 200, 210, 230, 255 });
            DrawText("P2P Chunking: Resumable HTTP Multi-part Engine", rightX + 18, contentY + 185, 12, (Color){ 200, 210, 230, 255 });
            DrawText("Hardware Fingerprint ID: Available", rightX + 18, contentY + 205, 12, (Color){ 34, 197, 94, 255 });

        } else if (g_state.active_tab == 1) {
            /* === SERVER STATS TAB === */
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, (Color){ 26, 30, 44, 255 });
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, (Color){ 42, 48, 70, 255 });

            DrawText("LIVE REALM TELEMETRY", 38, contentY + 20, 15, (Color){ 0, 180, 255, 255 });

            DrawText(TextFormat("Realm Name: %s", g_state.realm_name), 40, contentY + 60, 14, WHITE);
            DrawText(TextFormat("Online Players: %u", g_state.online_players), 40, contentY + 95, 14, (Color){ 34, 197, 94, 255 });
            DrawText(TextFormat("In-Game Active: %u", g_state.ingame_players), 40, contentY + 130, 14, (Color){ 0, 180, 255, 255 });
            DrawText(TextFormat("Roundtrip Latency: %u ms", g_state.latency_ms), 40, contentY + 165, 14, (Color){ 245, 175, 40, 255 });
            DrawText(TextFormat("Announcement: %s", g_state.motd), 40, contentY + 200, 13, (Color){ 200, 210, 230, 255 });

        } else if (g_state.active_tab == 2) {
            /* === LIVE LOGS TAB === */
            DrawRectangleRounded((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, (Color){ 16, 18, 26, 255 });
            DrawRectangleRoundedLines((Rectangle){ 20, (float)contentY, (float)(curW - 40), (float)contentH }, 0.02f, 4, (Color){ 42, 48, 70, 255 });

            DrawText("REAL-TIME LAUNCHER LOG CONSOLE", 38, contentY + 12, 12, (Color){ 140, 150, 175, 255 });

            int logY = contentY + 36;
            int maxVisible = (contentH - 50) / 18;
            int startIdx = (g_state.log_count > maxVisible) ? (g_state.log_count - maxVisible) : 0;

            for (int i = startIdx; i < g_state.log_count; i++) {
                DrawText(g_state.logs[i], 38, logY, 11, (Color){ 200, 210, 230, 255 });
                logY += 18;
            }
        }

        /* ----------------------------------------------------------------------
         * 4. Bottom Patching & Action Control Deck
         * ---------------------------------------------------------------------- */
        int deckY = curH - 110;
        DrawRectangle(0, deckY, curW, 110, (Color){ 14, 16, 24, 255 });
        DrawRectangle(0, deckY, curW, 1, (Color){ 45, 50, 70, 255 });

        /* Status & Current File Info */
        DrawText(g_state.status_message, 24, deckY + 12, 13, (Color){ 220, 230, 245, 255 });
        if (strlen(g_state.speed_str) > 0) {
            DrawText(TextFormat("Speed: %s  |  ETA: %s", g_state.speed_str, g_state.eta_str), curW - 450, deckY + 12, 12, (Color){ 0, 180, 255, 255 });
        }

        /* Progress Bar */
        GuiProgressBar((Rectangle){ 24, (float)deckY + 36, (float)(curW - 350), 22 }, NULL, TextFormat("%d%%", (int)(g_state.download_progress * 100)), &g_state.download_progress, 0.0f, 1.0f);

        /* Action Buttons */
        int btnWidth = 140;
        int btnHeight = 44;
        int btnY = deckY + 25;

        if (GuiButton((Rectangle){ (float)(curW - 310), (float)btnY, (float)btnWidth, (float)btnHeight }, "#149# Check Updates")) {
            __raygui_start_patch_thread__();
        }

        /* Launch / Play Button (Styled Accent) */
        if (g_state.patch_status == PATCH_READY) {
            GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, 0x16a34aff);
            GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, 0x22c55eff);
            GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, 0x15803dff);
        }

        if (GuiButton((Rectangle){ (float)(curW - 155), (float)btnY, (float)btnWidth, (float)btnHeight }, "#143# PLAY NOW")) {
            LOG_INFO("SYS", "PLAY NOW triggered. Starting client application...");
#if defined(_WIN32)
            ShellExecuteA(NULL, "open", "WoW.exe", NULL, NULL, SW_SHOWNORMAL);
#else
            if (system("./WoW.exe &") != 0) {
                LOG_INFO("SYS", "Sample launch executed.");
            }
#endif
        }

        /* Reset button style */
        GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, 0x242838ff);
        GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, 0x0088ccff);
        GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, 0x006699ff);

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
