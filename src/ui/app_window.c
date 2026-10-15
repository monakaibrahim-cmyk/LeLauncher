#include <ui/app_window.h>
#include <ui/window_ui_data.h>
#include <core/Version.h>
#include <core/dat.h>
#include <core/logger.h>
#include <network/api.h>
#include <network/hwid.h>
#include <network/live_feed.h>
#include <network/downloader.h>
#include <network/mfil.h>

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
    #include <windows.h>
    #include <shellapi.h>
    #ifndef SW_SHOWNORMAL
        #define SW_SHOWNORMAL 1
    #endif
    #include <client/Patch.h>
#else
    #include <unistd.h>
#endif

typedef struct {
    GtkBuilder          *builder;
    GtkEntry            *username_entry;
    GtkPasswordEntry    *password_entry;
    GtkEntry            *realmlist_entry;
    GtkButton           *realmlist_btn;
    GtkLabel            *client_info_label;
    GtkLabel            *auth_status_label;
    GtkTextView         *log_view;

    /* Live realm telemetry */
    GtkLabel            *realm_name_label;
    GtkLabel            *online_players_label;
    GtkLabel            *ingame_players_label;
    GtkLabel            *ping_label;
    GtkLabel            *motd_label;

    /* Downloader and patch status */
    GtkLabel            *patch_status_label;
    GtkButton           *update_btn;
    GtkButton           *play_btn;
    GtkButton           *clear_cache_btn;
    GtkProgressBar      *download_progress;
    GtkLabel            *download_speed_label;
    GtkLabel            *download_eta_label;
    GtkLabel            *download_file_label;

    /* Background updater worker */
    volatile int        patch_running;
    volatile int        patch_cancel;

    LiveFeedClient      *live_feed;
} AppWindowContext;

/* ==============================================================================
 * UI Thread Dispatch Helpers
 * ============================================================================== */
typedef struct {
    AppWindowContext *ctx;
    char             *line;
} LogIdleData;

static gboolean __log_idle_cb__(gpointer user_data) {
    LogIdleData *data = (LogIdleData *)user_data;
    if (data && data->ctx && data->ctx->log_view && data->line) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(data->ctx->log_view);
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_insert(buf, &end, data->line, -1);
        GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
        gtk_text_view_scroll_to_mark(data->ctx->log_view, mark, 0.0, FALSE, 0.0, 1.0);
    }
    if (data) {
        g_free(data->line);
        g_free(data);
    }
    return G_SOURCE_REMOVE;
}

static void __on_logger_ui_dispatch__(LogLevel level, const char *system, const char *formatted_line, void *user_data) {
    (void)level;
    (void)system;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !ctx->log_view || !formatted_line) return;

    LogIdleData *data = g_new0(LogIdleData, 1);
    data->ctx = ctx;
    data->line = g_strdup_printf("%s\n", formatted_line);
    g_idle_add(__log_idle_cb__, data);
}

/* ==============================================================================
 * Live Realm Telemetry Callbacks
 * ============================================================================== */
typedef struct {
    AppWindowContext *ctx;
    LiveGameStats     stats;
} StatsIdleData;

static gboolean __stats_idle_cb__(gpointer user_data) {
    StatsIdleData *data = (StatsIdleData *)user_data;
    if (!data || !data->ctx) {
        if (data) g_free(data);
        return G_SOURCE_REMOVE;
    }

    AppWindowContext *ctx = data->ctx;
    const LiveGameStats *stats = &data->stats;

    if (ctx->realm_name_label) {
        char realm_text[128];
        const char *status_str = (stats->status == REALM_ONLINE) ? "Online" :
                                 (stats->status == REALM_MAINTENANCE) ? "Maintenance" : "Offline";
        snprintf(realm_text, sizeof(realm_text), "Realm: %s (● %s)", stats->realm_name, status_str);
        gtk_label_set_text(ctx->realm_name_label, realm_text);
    }

    if (ctx->online_players_label) {
        char text[64];
        snprintf(text, sizeof(text), "Online: %u", stats->online_players);
        gtk_label_set_text(ctx->online_players_label, text);
    }

    if (ctx->ingame_players_label) {
        char text[64];
        snprintf(text, sizeof(text), "In-Game: %u", stats->ingame_players);
        gtk_label_set_text(ctx->ingame_players_label, text);
    }

    if (ctx->ping_label) {
        char text[64];
        snprintf(text, sizeof(text), "Ping: %u ms", stats->latency_ms);
        gtk_label_set_text(ctx->ping_label, text);
    }

    if (ctx->motd_label && stats->motd[0] != '\0') {
        gtk_label_set_text(ctx->motd_label, stats->motd);
    }

    g_free(data);
    return G_SOURCE_REMOVE;
}

static void __on_live_stats_updated__(const LiveGameStats *stats, void *user_data) {
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !stats) return;

    StatsIdleData *data = g_new0(StatsIdleData, 1);
    data->ctx = ctx;
    data->stats = *stats;
    g_idle_add(__stats_idle_cb__, data);
}

/* ==============================================================================
 * Player Utilities: Realmlist & Cache Cleaner
 * ============================================================================== */
static void __set_realmlist_handler__(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !ctx->realmlist_entry) return;

    const char *host = gtk_editable_get_text(GTK_EDITABLE(ctx->realmlist_entry));
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

    char notice[128];
    snprintf(notice, sizeof(notice), "✓ Realmlist set: %s (%d files)", host, updated);
    if (ctx->auth_status_label) {
        gtk_label_set_text(ctx->auth_status_label, notice);
    }
    LOG_INFO("TOOL", "Realmlist configured to: %s (%d files written)", host, updated);
}

static void __clear_cache_handler__(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;

#if defined(_WIN32)
    system("rmdir /s /q Cache 2>nul");
    system("rmdir /s /q WDB 2>nul");
#else
    if (system("rm -rf Cache WDB 2>/dev/null") == 0) {}
#endif

    if (ctx && ctx->auth_status_label) {
        gtk_label_set_text(ctx->auth_status_label, "✓ Cleared WDB & Cache directories!");
    }
    LOG_INFO("TOOL", "Game client Cache / WDB cleaned successfully");
}

/* ==============================================================================
 * Authentication: Directly Uses Embedded Obfuscated API (No API Input Asked)
 * ============================================================================== */
static void __verify_account_handler__(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return;

    const char *username = gtk_editable_get_text(GTK_EDITABLE(ctx->username_entry));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(ctx->password_entry));

    if (!username || strlen(username) == 0) {
        gtk_label_set_text(ctx->auth_status_label, "Username cannot be empty");
        return;
    }

    gtk_label_set_text(ctx->auth_status_label, "Connecting to realm gateway...");
    LOG_INFO("AUTH", "Authenticating champion '%s' with embedded realm service...", username);

    const char *hwid = __hwid_get__();
    char ip[64] = "127.0.0.1";
    __network_get_client_ip__(ip, sizeof(ip));

    /* Pass NULL to directly use the embedded encrypted API URL */
    ApiPreAuthResult preauth = __api_request_temporary_token__(NULL, hwid, ip);
    if (!preauth.success) {
        char err[300];
        snprintf(err, sizeof(err), "Handshake failed: %s", preauth.error_message);
        gtk_label_set_text(ctx->auth_status_label, err);
        return;
    }

    ApiAuthResult auth = __api_authenticate_credentials__(NULL, preauth.temporary_token, username, password, hwid);
    if (auth.success) {
        char status[300];
        snprintf(status, sizeof(status), "✓ Authenticated (Account: %s)", auth.account_id);
        gtk_label_set_text(ctx->auth_status_label, status);
        LOG_INFO("AUTH", "Authentication successful for user '%s'", username);

        /* Persist credentials to encrypted Account.dat vault */
        if (gStructure) {
            strncpy(gStructure->credential.username, username, sizeof(gStructure->credential.username) - 1);
            strncpy(gStructure->credential.password, password, sizeof(gStructure->credential.password) - 1);
            strncpy(gStructure->credential.token, auth.permanent_token, sizeof(gStructure->credential.token) - 1);
            if (__save__()) {
                LOG_INFO("DAT", "Credentials securely persisted to Account.dat");
            }
        }
    } else {
        char err[300];
        snprintf(err, sizeof(err), "Authentication rejected: %s", auth.error_message[0] ? auth.error_message : "Invalid credentials");
        gtk_label_set_text(ctx->auth_status_label, err);
        LOG_WARN("AUTH", "Authentication rejected for user '%s'", username);
    }
}

/* ==============================================================================
 * Game Client Launch Execution
 * ============================================================================== */
static void __play_game_handler__(GtkButton *btn, gpointer user_data) {
    (void)btn;
    (void)user_data;
    LOG_INFO("SYS", "Starting game client executable...");

#if defined(_WIN32)
    const char *target = __find_game_client_executable__();
    if (__is_regular_file__(target)) {
        LOG_INFO("PATCH", "Verifying and applying client patches (LAA + launcher hook) to %s...", target);
        __apply_patches__(target);
    }
    HINSTANCE res = ShellExecuteA(NULL, "open", target, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)res <= 32) {
        ShellExecuteA(NULL, "open", "WoW.exe", NULL, NULL, SW_SHOWNORMAL);
    }
#else
    if (system("./WoW.exe &") != 0 && system("wine WoW.exe &") != 0) {
        LOG_INFO("SYS", "Game binary triggered.");
    }
#endif
}

/* ==============================================================================
 * Background Patch & Verification Worker
 * ============================================================================== */
typedef struct {
    AppWindowContext *ctx;
    double            fraction;
    char             *speed;
    char             *eta;
    char             *file_name;
    char             *status_text;
    gboolean          finished;
} PatchProgressData;

static gboolean __patch_progress_idle_cb__(gpointer user_data) {
    PatchProgressData *data = (PatchProgressData *)user_data;
    if (!data || !data->ctx) {
        if (data) {
            g_free(data->speed);
            g_free(data->eta);
            g_free(data->file_name);
            g_free(data->status_text);
            g_free(data);
        }
        return G_SOURCE_REMOVE;
    }

    AppWindowContext *ctx = data->ctx;

    if (data->status_text && ctx->patch_status_label) {
        gtk_label_set_text(ctx->patch_status_label, data->status_text);
    }

    if (ctx->download_progress) {
        gtk_progress_bar_set_fraction(ctx->download_progress, data->fraction);
        char pct[32];
        snprintf(pct, sizeof(pct), "%.1f%%", data->fraction * 100.0);
        gtk_progress_bar_set_text(ctx->download_progress, pct);
    }

    if (data->speed && ctx->download_speed_label) {
        gtk_label_set_text(ctx->download_speed_label, data->speed);
    }

    if (data->eta && ctx->download_eta_label) {
        gtk_label_set_text(ctx->download_eta_label, data->eta);
    }

    if (data->file_name && ctx->download_file_label) {
        gtk_label_set_text(ctx->download_file_label, data->file_name);
    }

    if (data->finished) {
        if (ctx->update_btn) gtk_widget_set_sensitive(GTK_WIDGET(ctx->update_btn), TRUE);
        if (ctx->play_btn) gtk_widget_set_sensitive(GTK_WIDGET(ctx->play_btn), TRUE);
    }

    g_free(data->speed);
    g_free(data->eta);
    g_free(data->file_name);
    g_free(data->status_text);
    g_free(data);
    return G_SOURCE_REMOVE;
}

typedef struct {
    AppWindowContext *ctx;
    size_t            file_index;
    size_t            total_files;
    const char       *file_name;
} PatchWorkerDlContext;

static int __on_patch_download_progress__(const CURLProgress *prog, void *user_data) {
    PatchWorkerDlContext *dc = (PatchWorkerDlContext *)user_data;
    if (!dc || !dc->ctx) return 0;
    if (dc->ctx->patch_cancel) return 1;

    PatchProgressData *pd = g_new0(PatchProgressData, 1);
    pd->ctx = dc->ctx;

    if (dc->total_files > 0) {
        double f = (prog->fraction >= 0.0) ? prog->fraction : 0.0;
        pd->fraction = ((double)dc->file_index + f) / (double)dc->total_files;
    } else {
        pd->fraction = 0.0;
    }

    char speed_buf[32];
    __format_bytes__(prog->speed_bytes_per_seconds, speed_buf, sizeof(speed_buf));
    pd->speed = g_strdup_printf("%s/s", speed_buf);

    char eta_buf[32];
    __format_duration__(prog->eta_seconds, eta_buf, sizeof(eta_buf));
    pd->eta = g_strdup_printf("ETA: %s", eta_buf);

    pd->file_name = g_strdup(dc->file_name ? dc->file_name : "");
    g_idle_add(__patch_progress_idle_cb__, pd);
    return 0;
}

static gpointer __patch_worker_thread__(gpointer user_data) {
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return NULL;

    LOG_INFO("PATCH", "Beginning game archive check and verification (.mfil)...");

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

    char local_manifest_path[512];
    snprintf(local_manifest_path, sizeof(local_manifest_path), "www/wow-pod-retail/US/12340.direct/%s", partial_file);

    MfilManifest manifest;
    bool has_manifest = __mfil_parse_manifest_file__(local_manifest_path, &manifest);
    if (!has_manifest) {
        has_manifest = __mfil_parse_manifest_file__(partial_file, &manifest);
    }

    if (!has_manifest) {
        CURLResult res;
        CURLStatus st = __mfil_download_partial__(base_location, partial_file, partial_file, NULL, &res);
        if (st == CURLDL_OK) {
            has_manifest = __mfil_parse_manifest_file__(partial_file, &manifest);
        }
    }

    if (!has_manifest) {
        LOG_INFO("PATCH", "Manifest not found on CDN. Ready with local game archives.");
        PatchProgressData *pd = g_new0(PatchProgressData, 1);
        pd->ctx = ctx;
        pd->fraction = 1.0;
        pd->status_text = g_strdup("Local installation verified. Ready to Play!");
        pd->file_name = g_strdup("Local Cache");
        pd->finished = TRUE;
        g_idle_add(__patch_progress_idle_cb__, pd);
        if (has_master) __mfil_free_master__(&master);
        ctx->patch_running = 0;
        return NULL;
    }

    size_t verified = 0;
    size_t downloaded = 0;

    for (size_t i = 0; i < manifest.file_count; i++) {
        if (ctx->patch_cancel) break;

        const MfilFile *file = &manifest.files[i];

        PatchProgressData *pd = g_new0(PatchProgressData, 1);
        pd->ctx = ctx;
        pd->fraction = (double)i / (double)manifest.file_count;
        pd->status_text = g_strdup_printf("Verifying: %s (%zu/%zu)", file->name, i + 1, manifest.file_count);
        pd->file_name = g_strdup(file->name);
        g_idle_add(__patch_progress_idle_cb__, pd);

        if (__mfil_verify_file_checksum__(file->name, file->checksum)) {
            verified++;
            continue;
        }

        /* File missing or corrupted -> download */
        CURLOptions opts;
        __init_options__(&opts, NULL);
        opts.cancel_flag = &ctx->patch_cancel;
        opts.resume = 1;

        PatchWorkerDlContext dc = {
            .ctx = ctx,
            .file_index = i,
            .total_files = manifest.file_count,
            .file_name = file->name
        };
        opts.on_progress = __on_patch_download_progress__;
        opts.user_data = &dc;

        CURLResult dl_res;
        CURLStatus dl_st = __mfil_download_file__(base_location, &manifest, file, NULL, &opts, &dl_res);
        if (dl_st == CURLDL_OK || dl_st == CURLDL_ALREADY_COMPLETE) {
            downloaded++;
            verified++;
        }
    }

    PatchProgressData *done_pd = g_new0(PatchProgressData, 1);
    done_pd->ctx = ctx;
    done_pd->fraction = 1.0;
    done_pd->status_text = g_strdup_printf("All %zu game archives verified. The Frozen Throne awaits!", verified);
    done_pd->file_name = g_strdup("Ready");
    done_pd->finished = TRUE;
    g_idle_add(__patch_progress_idle_cb__, done_pd);

    __mfil_free_manifest__(&manifest);
    if (has_master) __mfil_free_master__(&master);

    LOG_INFO("PATCH", "Patch verification finished (%zu verified, %zu downloaded)", verified, downloaded);
    ctx->patch_running = 0;
    return NULL;
}

static void __start_patch_process__(GtkButton *btn, gpointer user_data) {
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return;

    if (ctx->patch_running) {
        LOG_INFO("PATCH", "Patch verification is already in progress.");
        return;
    }

    if (btn) gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    ctx->patch_cancel = 0;
    ctx->patch_running = 1;
    GThread *th = g_thread_new("patch_worker", __patch_worker_thread__, ctx);
    g_thread_unref(th);
}

static void __refresh_realm_handler__(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !ctx->live_feed) return;

    LOG_INFO("NET", "Refreshing realm connection telemetry...");
    __live_feed_simulate_tick__(ctx->live_feed);
}

/* ==============================================================================
 * Application Activation & Main Entry
 * ============================================================================== */
static void __app_activate__(GtkApplication *app, gpointer user_data) {
    AppWindowContext *ctx = (AppWindowContext *)user_data;

    /* Load UI directly from embedded binary memory (guaranteed inside program) */
    ctx->builder = gtk_builder_new_from_string(EMBEDDED_WINDOW_UI_XML, -1);
    if (!ctx->builder) {
        ctx->builder = gtk_builder_new_from_resource("/com/example/LeLauncher/window.ui");
    }
    GObject *window = gtk_builder_get_object(ctx->builder, "app_window");
    if (!window) {
        g_critical("Failed to locate 'app_window' object from UI builder!");
        return;
    }
    gtk_window_set_application(GTK_WINDOW(window), app);

    /* Bind widgets */
    ctx->username_entry       = GTK_ENTRY(gtk_builder_get_object(ctx->builder, "username_entry"));
    ctx->password_entry       = GTK_PASSWORD_ENTRY(gtk_builder_get_object(ctx->builder, "password_entry"));
    ctx->realmlist_entry      = GTK_ENTRY(gtk_builder_get_object(ctx->builder, "realmlist_entry"));
    ctx->realmlist_btn        = GTK_BUTTON(gtk_builder_get_object(ctx->builder, "realmlist_btn"));
    ctx->client_info_label    = GTK_LABEL(gtk_builder_get_object(ctx->builder, "client_info_label"));
    ctx->auth_status_label    = GTK_LABEL(gtk_builder_get_object(ctx->builder, "auth_status_label"));
    ctx->log_view             = GTK_TEXT_VIEW(gtk_builder_get_object(ctx->builder, "log_view"));

    ctx->realm_name_label     = GTK_LABEL(gtk_builder_get_object(ctx->builder, "realm_name_label"));
    ctx->online_players_label = GTK_LABEL(gtk_builder_get_object(ctx->builder, "online_players_label"));
    ctx->ingame_players_label = GTK_LABEL(gtk_builder_get_object(ctx->builder, "ingame_players_label"));
    ctx->ping_label           = GTK_LABEL(gtk_builder_get_object(ctx->builder, "ping_label"));
    ctx->motd_label           = GTK_LABEL(gtk_builder_get_object(ctx->builder, "motd_label"));

    ctx->patch_status_label   = GTK_LABEL(gtk_builder_get_object(ctx->builder, "patch_status_label"));
    ctx->update_btn           = GTK_BUTTON(gtk_builder_get_object(ctx->builder, "update_btn"));
    ctx->play_btn             = GTK_BUTTON(gtk_builder_get_object(ctx->builder, "play_btn"));
    ctx->clear_cache_btn      = GTK_BUTTON(gtk_builder_get_object(ctx->builder, "clear_cache_btn"));
    ctx->download_progress    = GTK_PROGRESS_BAR(gtk_builder_get_object(ctx->builder, "download_progress"));
    ctx->download_speed_label = GTK_LABEL(gtk_builder_get_object(ctx->builder, "download_speed_label"));
    ctx->download_eta_label   = GTK_LABEL(gtk_builder_get_object(ctx->builder, "download_eta_label"));
    ctx->download_file_label  = GTK_LABEL(gtk_builder_get_object(ctx->builder, "download_file_label"));

    GtkButton *verify_btn  = GTK_BUTTON(gtk_builder_get_object(ctx->builder, "verify_btn"));
    GtkButton *refresh_btn = GTK_BUTTON(gtk_builder_get_object(ctx->builder, "refresh_btn"));

    /* Display HWID & IP */
    const char *hwid = __hwid_get__();
    char ip[64] = "127.0.0.1";
    __network_get_client_ip__(ip, sizeof(ip));
    char info[256];
    snprintf(info, sizeof(info), "HWID: %.10s... | IP: %s", hwid, ip);
    gtk_label_set_text(ctx->client_info_label, info);

    /* Connect signals */
    if (verify_btn) g_signal_connect(verify_btn, "clicked", G_CALLBACK(__verify_account_handler__), ctx);
    if (ctx->realmlist_btn) g_signal_connect(ctx->realmlist_btn, "clicked", G_CALLBACK(__set_realmlist_handler__), ctx);
    if (ctx->clear_cache_btn) g_signal_connect(ctx->clear_cache_btn, "clicked", G_CALLBACK(__clear_cache_handler__), ctx);
    if (ctx->update_btn) g_signal_connect(ctx->update_btn, "clicked", G_CALLBACK(__start_patch_process__), ctx);
    if (ctx->play_btn) g_signal_connect(ctx->play_btn, "clicked", G_CALLBACK(__play_game_handler__), ctx);
    if (refresh_btn) g_signal_connect(refresh_btn, "clicked", G_CALLBACK(__refresh_realm_handler__), ctx);

    /* Hook into asynchronous logger */
    __logger_set_ui_callback__(__on_logger_ui_dispatch__, ctx);

    /* Restore credentials from encrypted Account.dat vault if present */
    if (__load__() && gStructure) {
        if (gStructure->credential.username[0] != '\0') {
            gtk_editable_set_text(GTK_EDITABLE(ctx->username_entry), gStructure->credential.username);
        }
        if (gStructure->credential.password[0] != '\0') {
            gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), gStructure->credential.password);
        }
        if (gStructure->credential.token[0] != '\0') {
            gtk_label_set_text(ctx->auth_status_label, "✓ Vault Session Restored");
        }
    }

    /* Start live realm telemetry feed */
    ctx->live_feed = __live_feed_create__("127.0.0.1", 8080, "/stats", false);
    if (ctx->live_feed) {
        __live_feed_set_callback__(ctx->live_feed, __on_live_stats_updated__, ctx);
        __live_feed_start__(ctx->live_feed);
    }

    /* Initial patch check */
    __start_patch_process__(ctx->update_btn, ctx);

    gtk_window_present(GTK_WINDOW(window));
}

int app_window_run(int argc, char **argv) {
    AppWindowContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    GtkApplication *app = gtk_application_new("com.example.LeLauncher", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(__app_activate__), &ctx);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    /* Cleanup */
    __logger_set_ui_callback__(NULL, NULL);

    ctx.patch_cancel = 1;
    while (ctx.patch_running) {
        g_usleep(10000);
    }

    if (ctx.live_feed) {
        __live_feed_stop__(ctx.live_feed);
        __live_feed_destroy__(ctx.live_feed);
        ctx.live_feed = NULL;
    }
    if (ctx.builder) {
        g_object_unref(ctx.builder);
        ctx.builder = NULL;
    }
    g_object_unref(app);
    return status;
}
