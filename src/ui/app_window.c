#include <ui/app_window.h>
#include <core/Version.h>
#include <core/dat.h>
#include <core/vector.h>
#include <network/api.h>
#include <network/hwid.h>
#include <network/live_feed.h>
#include <network/downloader.h>
#include <network/mfil.h>
#include <core/logger.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    GtkBuilder          *builder;
    GtkEntry            *server_entry;
    GtkEntry            *username_entry;
    GtkPasswordEntry    *password_entry;
    GtkLabel            *client_info_label;
    GtkLabel            *auth_status_label;
    GtkTextView         *log_view;

    /* Live realm status */
    GtkLabel            *realm_name_label;
    GtkLabel            *online_players_label;
    GtkLabel            *ingame_players_label;
    GtkLabel            *ping_label;
    GtkLabel            *motd_label;

    /* Downloader and patch status */
    GtkLabel            *patch_status_label;
    GtkButton           *update_btn;
    GtkButton           *play_btn;
    GtkProgressBar      *download_progress;
    GtkLabel            *download_speed_label;
    GtkLabel            *download_eta_label;
    GtkLabel            *download_file_label;

    /* Background updater worker */
    GThread             *patch_thread;
    volatile int        patch_cancel;

    LiveFeedClient      *live_feed;
} AppWindowContext;

static void __on_logger_ui_dispatch__(LogLevel level, const char *system, const char *formatted_line, void *user_data) {
    (void)level;
    (void)system;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !ctx->log_view || !formatted_line) return;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(ctx->log_view);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);

    char line[1400];
    snprintf(line, sizeof(line), "%s\n", formatted_line);
    gtk_text_buffer_insert(buf, &end, line, -1);
}

static void __append_log__(AppWindowContext *ctx, const char *system, const char *message) {
    (void)ctx;
    LOG_INFO(system, "%s", message);
}

static void __on_live_stats_updated__(const LiveGameStats *stats, void *user_data) {
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !stats) return;

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

    if (ctx->motd_label && strlen(stats->motd) > 0) {
        gtk_label_set_text(ctx->motd_label, stats->motd);
    }
}

static void __on_verify_clicked__(GtkButton *btn, gpointer user_data) {
    TRACE_SCOPE();
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return;

    const char *user = gtk_editable_get_text(GTK_EDITABLE(ctx->username_entry));
    const char *pass = gtk_editable_get_text(GTK_EDITABLE(ctx->password_entry));

    const char *hwid = __hwid_get__();
    char ip[64];
    __network_get_client_ip__(ip, sizeof(ip));

    const char *endpoint = NULL;
    if (ctx->server_entry) {
        const char *txt = gtk_editable_get_text(GTK_EDITABLE(ctx->server_entry));
        if (txt && strlen(txt) > 0) {
            endpoint = txt;
        }
    }

    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "Requesting handshake token (HWID: %.12s... | IP: %s)", hwid, ip);
    __append_log__(ctx, "AUTH", log_buf);

    /* Acquire handshake token */
    ApiPreAuthResult preauth = __api_request_temporary_token__(endpoint, hwid, ip);
    if (!preauth.success) {
        char err[256];
        snprintf(err, sizeof(err), "Handshake failed: %s", preauth.error_message);
        gtk_label_set_text(ctx->auth_status_label, err);
        __append_log__(ctx, "AUTH", err);
        return;
    }

    snprintf(log_buf, sizeof(log_buf), "Handshake established (Token: %.18s...)", preauth.temporary_token);
    __append_log__(ctx, "AUTH", log_buf);

    /* Authenticate credentials with realm */
    snprintf(log_buf, sizeof(log_buf), "Authenticating credentials for '%s'...", user);
    __append_log__(ctx, "AUTH", log_buf);

    ApiAuthResult auth = __api_authenticate_credentials__(endpoint, preauth.temporary_token, user, pass, hwid);
    if (!auth.success) {
        char err[256];
        snprintf(err, sizeof(err), "Authentication rejected: %s", auth.error_message);
        gtk_label_set_text(ctx->auth_status_label, err);
        __append_log__(ctx, "AUTH", err);
        return;
    }

    snprintf(log_buf, sizeof(log_buf), "Session authenticated (Token: %.20s...)", auth.permanent_token);
    __append_log__(ctx, "AUTH", log_buf);

    /* Persist updated session token */
    if (gStructure) {
        strncpy(gStructure->credential.username, user, sizeof(gStructure->credential.username) - 1);
        strncpy(gStructure->credential.password, pass, sizeof(gStructure->credential.password) - 1);
        strncpy(gStructure->credential.token, auth.permanent_token, sizeof(gStructure->credential.token) - 1);

        if (__save__()) {
            __append_log__(ctx, "DAT", "Account credentials and session token saved to Account.dat");
            char status[256];
            snprintf(status, sizeof(status), "Authenticated (Token: %.16s...)", auth.permanent_token);
            gtk_label_set_text(ctx->auth_status_label, status);
        } else {
            __append_log__(ctx, "DAT", "Warning: Failed to save Account.dat");
        }
    }
}

static void __on_save_clicked__(GtkButton *btn, gpointer user_data) {
    TRACE_SCOPE();
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx || !gStructure) return;

    const char *user = gtk_editable_get_text(GTK_EDITABLE(ctx->username_entry));
    const char *pass = gtk_editable_get_text(GTK_EDITABLE(ctx->password_entry));

    strncpy(gStructure->credential.username, user, sizeof(gStructure->credential.username) - 1);
    strncpy(gStructure->credential.password, pass, sizeof(gStructure->credential.password) - 1);

    if (__save__()) {
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Saved to Account.dat (Checksum: %.16s...)", gStructure->header.checksum);
        gtk_label_set_text(ctx->auth_status_label, status_msg);
        __append_log__(ctx, "DAT", "Credentials saved to Account.dat");
    } else {
        gtk_label_set_text(ctx->auth_status_label, "Failed to save container!");
        __append_log__(ctx, "DAT", "Error: __save__ failed");
    }
}

static void __on_refresh_clicked__(GtkButton *btn, gpointer user_data) {
    TRACE_SCOPE();
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return;

    if (__load__()) {
        gtk_editable_set_text(GTK_EDITABLE(ctx->username_entry), gStructure->credential.username);
        gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), gStructure->credential.password);

        char status_msg[256];
        if (strlen(gStructure->credential.token) > 0) {
            snprintf(status_msg, sizeof(status_msg), "Loaded Account.dat (User: %s | Token: %.12s...)",
                     gStructure->credential.username, gStructure->credential.token);
        } else {
            snprintf(status_msg, sizeof(status_msg), "Loaded Account.dat (v%d). No saved token.",
                     gStructure->header.version);
        }
        gtk_label_set_text(ctx->auth_status_label, status_msg);
        __append_log__(ctx, "DAT", "Loaded credentials from Account.dat");
    }

    if (ctx->live_feed) {
        __live_feed_simulate_tick__(ctx->live_feed);
        __append_log__(ctx, "SOCKET", "Live server stats pinged");
    }
}

typedef struct {
    AppWindowContext *ctx;
    double           fraction;
    char             text[64];
    char             speed_str[64];
    char             eta_str[64];
    char             file_str[128];
    char             status_str[128];
} PatchProgressUpdate;

static gboolean __on_patch_progress_idle__(gpointer user_data) {
    PatchProgressUpdate *u = (PatchProgressUpdate *)user_data;
    if (u && u->ctx) {
        if (u->ctx->download_progress) {
            gtk_progress_bar_set_fraction(u->ctx->download_progress, u->fraction);
            if (strlen(u->text) > 0) {
                gtk_progress_bar_set_text(u->ctx->download_progress, u->text);
            }
        }
        if (u->ctx->download_speed_label && strlen(u->speed_str) > 0) {
            gtk_label_set_text(u->ctx->download_speed_label, u->speed_str);
        }
        if (u->ctx->download_eta_label && strlen(u->eta_str) > 0) {
            gtk_label_set_text(u->ctx->download_eta_label, u->eta_str);
        }
        if (u->ctx->download_file_label && strlen(u->file_str) > 0) {
            gtk_label_set_text(u->ctx->download_file_label, u->file_str);
        }
        if (u->ctx->patch_status_label && strlen(u->status_str) > 0) {
            gtk_label_set_text(u->ctx->patch_status_label, u->status_str);
        }
    }
    g_free(u);
    return G_SOURCE_REMOVE;
}

typedef struct {
    AppWindowContext *ctx;
    bool             success;
    char             message[256];
} PatchFinishedUpdate;

static gboolean __on_patch_finished_idle__(gpointer user_data) {
    PatchFinishedUpdate *f = (PatchFinishedUpdate *)user_data;
    if (f && f->ctx) {
        if (f->ctx->update_btn) {
            gtk_widget_set_sensitive(GTK_WIDGET(f->ctx->update_btn), TRUE);
        }
        if (f->ctx->patch_status_label) {
            gtk_label_set_text(f->ctx->patch_status_label, f->message);
        }
        if (f->success) {
            if (f->ctx->download_progress) {
                gtk_progress_bar_set_fraction(f->ctx->download_progress, 1.0);
                gtk_progress_bar_set_text(f->ctx->download_progress, "100% - Ready");
            }
            if (f->ctx->play_btn) {
                gtk_widget_set_sensitive(GTK_WIDGET(f->ctx->play_btn), TRUE);
            }
            if (f->ctx->download_speed_label) {
                gtk_label_set_text(f->ctx->download_speed_label, "Complete");
            }
            if (f->ctx->download_eta_label) {
                gtk_label_set_text(f->ctx->download_eta_label, "--:--");
            }
        }
        f->ctx->patch_thread = NULL;
    }
    g_free(f);
    return G_SOURCE_REMOVE;
}

typedef struct {
    AppWindowContext *ctx;
    const char       *current_file;
    size_t           file_index;
    size_t           total_files;
} DownloadUserContext;

static int __on_download_progress_cb__(const CURLProgress *prog, void *user_data) {
    DownloadUserContext *dc = (DownloadUserContext *)user_data;
    if (!dc || !dc->ctx) return 0;
    if (dc->ctx->patch_cancel) return 1;

    PatchProgressUpdate *u = g_new0(PatchProgressUpdate, 1);
    u->ctx = dc->ctx;
    u->fraction = (prog->fraction > 0.0) ? prog->fraction : 0.0;
    snprintf(u->text, sizeof(u->text), "%.1f%% (%zu/%zu)",
             u->fraction * 100.0, dc->file_index + 1, dc->total_files);

    char spd[32];
    __format_bytes__(prog->speed_bytes_per_seconds, spd, sizeof(spd));
    snprintf(u->speed_str, sizeof(u->speed_str), "%s/s", spd);

    char eta[32];
    __format_duration__(prog->eta_seconds, eta, sizeof(eta));
    snprintf(u->eta_str, sizeof(u->eta_str), "ETA: %s", eta);

    snprintf(u->file_str, sizeof(u->file_str), "%s", dc->current_file);

    g_idle_add(__on_patch_progress_idle__, u);
    return 0;
}

static gpointer __patch_worker__(gpointer data) {
    TRACE_SCOPE();
    AppWindowContext *ctx = (AppWindowContext *)data;
    if (!ctx) return NULL;

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

    MfilManifest manifest;
    char local_manifest_path[512];
    snprintf(local_manifest_path, sizeof(local_manifest_path), "www/wow-pod-retail/US/12340.direct/%s", partial_file);

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
        PatchFinishedUpdate *f = g_new0(PatchFinishedUpdate, 1);
        f->ctx = ctx;
        f->success = false;
        snprintf(f->message, sizeof(f->message), "Failed to load manifest: %s", partial_file);
        g_idle_add(__on_patch_finished_idle__, f);
        if (has_master) __mfil_free_master__(&master);
        return NULL;
    }

    size_t verified = 0;
    size_t downloaded = 0;
    for (size_t i = 0; i < manifest.file_count; i++) {
        if (ctx->patch_cancel) break;

        const MfilFile *file = &manifest.files[i];

        if (__mfil_verify_file_checksum__(file->name, file->checksum)) {
            verified++;
            PatchProgressUpdate *u = g_new0(PatchProgressUpdate, 1);
            u->ctx = ctx;
            u->fraction = (double)(i + 1) / (double)manifest.file_count;
            snprintf(u->text, sizeof(u->text), "Verifying: %zu/%zu", i + 1, manifest.file_count);
            snprintf(u->file_str, sizeof(u->file_str), "%s (OK)", file->name);
            snprintf(u->status_str, sizeof(u->status_str), "Verifying game assets...");
            g_idle_add(__on_patch_progress_idle__, u);
            continue;
        }

        CURLOptions opts;
        __init_options__(&opts, NULL);
        opts.cancel_flag = (volatile int *)&ctx->patch_cancel;
        opts.resume = 1;

        DownloadUserContext dc = {
            .ctx = ctx,
            .current_file = file->name,
            .file_index = i,
            .total_files = manifest.file_count
        };
        opts.on_progress = __on_download_progress_cb__;
        opts.user_data = &dc;

        CURLResult dl_res;
        CURLStatus dl_st = __mfil_download_file__(base_location, &manifest, file, NULL, &opts, &dl_res);
        if (dl_st == CURLDL_OK || dl_st == CURLDL_ALREADY_COMPLETE) {
            downloaded++;
            verified++;
        }
    }

    PatchFinishedUpdate *f = g_new0(PatchFinishedUpdate, 1);
    f->ctx = ctx;
    f->success = (verified == manifest.file_count);
    if (f->success) {
        snprintf(f->message, sizeof(f->message), "Game client is up to date (%zu files verified)", verified);
    } else {
        snprintf(f->message, sizeof(f->message), "Patch incomplete (%zu/%zu verified)", verified, manifest.file_count);
    }
    g_idle_add(__on_patch_finished_idle__, f);

    __mfil_free_manifest__(&manifest);
    if (has_master) __mfil_free_master__(&master);
    return NULL;
}

static void __on_update_clicked__(GtkButton *btn, gpointer user_data) {
    TRACE_SCOPE();
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return;

    if (ctx->patch_thread) {
        __append_log__(ctx, "PATCH", "Update process already running");
        return;
    }

    ctx->patch_cancel = 0;
    if (ctx->update_btn) {
        gtk_widget_set_sensitive(GTK_WIDGET(ctx->update_btn), FALSE);
    }
    if (ctx->play_btn) {
        gtk_widget_set_sensitive(GTK_WIDGET(ctx->play_btn), FALSE);
    }
    if (ctx->patch_status_label) {
        gtk_label_set_text(ctx->patch_status_label, "Connecting to CDN and verifying manifest...");
    }

    __append_log__(ctx, "PATCH", "Starting Blizzard .mfil manifest check and download worker...");
    ctx->patch_thread = g_thread_new("patch_worker", __patch_worker__, ctx);
}

static void __on_play_clicked__(GtkButton *btn, gpointer user_data) {
    TRACE_SCOPE();
    (void)btn;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (!ctx) return;

    __append_log__(ctx, "GAME", "Launching World of Warcraft (WoW.exe)...");

    const char *game_binaries[] = { "WoW.exe", "wow.exe", "World of Warcraft.exe", NULL };
    const char *chosen = NULL;
    for (int i = 0; game_binaries[i]; i++) {
        if (g_file_test(game_binaries[i], G_FILE_TEST_EXISTS)) {
            chosen = game_binaries[i];
            break;
        }
    }

    if (chosen) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Starting game executable: %s", chosen);
        __append_log__(ctx, "GAME", msg);
        g_spawn_command_line_async(chosen, NULL);
    } else {
        __append_log__(ctx, "GAME", "Game executable (WoW.exe) ready. (Place WoW.exe in client directory to launch).");
    }
}

static void __on_window_destroy__(GtkWidget *widget, gpointer user_data) {
    TRACE_SCOPE();
    (void)widget;
    AppWindowContext *ctx = (AppWindowContext *)user_data;
    if (ctx) {
        ctx->patch_cancel = 1;
        if (ctx->patch_thread) {
            g_thread_join(ctx->patch_thread);
            ctx->patch_thread = NULL;
        }
        if (ctx->live_feed) {
            __live_feed_destroy__(ctx->live_feed);
            ctx->live_feed = NULL;
        }
        __logger_set_ui_callback__(NULL, NULL);
        if (ctx->builder) {
            g_object_unref(ctx->builder);
        }
        g_free(ctx);
    }
}

GtkWidget *__app_window_create__(GtkApplication *app) {
    TRACE_SCOPE();
    GtkBuilder *builder = gtk_builder_new_from_resource("/com/example/LeLauncher/window.ui");
    if (!builder) {
        g_printerr("Failed to load GResource /com/example/LeLauncher/window.ui\n");
        return NULL;
    }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "app_window"));
    if (!window) {
        g_printerr("Could not find root widget 'app_window' in Blueprint UI\n");
        g_object_unref(builder);
        return NULL;
    }

    AppWindowContext *ctx = g_new0(AppWindowContext, 1);
    ctx->builder = builder;
    __logger_set_ui_callback__(__on_logger_ui_dispatch__, ctx);
    ctx->server_entry = GTK_ENTRY(gtk_builder_get_object(builder, "server_entry"));
    const char *env_api_url = getenv("LELAUNCHER_API_URL");
    if (env_api_url && ctx->server_entry) {
        gtk_editable_set_text(GTK_EDITABLE(ctx->server_entry), env_api_url);
    }
    ctx->username_entry = GTK_ENTRY(gtk_builder_get_object(builder, "username_entry"));
    ctx->password_entry = GTK_PASSWORD_ENTRY(gtk_builder_get_object(builder, "password_entry"));
    ctx->client_info_label = GTK_LABEL(gtk_builder_get_object(builder, "client_info_label"));
    ctx->auth_status_label = GTK_LABEL(gtk_builder_get_object(builder, "auth_status_label"));
    ctx->log_view = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "log_view"));

    /* Bind live realm widgets */
    ctx->realm_name_label = GTK_LABEL(gtk_builder_get_object(builder, "realm_name_label"));
    ctx->online_players_label = GTK_LABEL(gtk_builder_get_object(builder, "online_players_label"));
    ctx->ingame_players_label = GTK_LABEL(gtk_builder_get_object(builder, "ingame_players_label"));
    ctx->ping_label = GTK_LABEL(gtk_builder_get_object(builder, "ping_label"));
    ctx->motd_label = GTK_LABEL(gtk_builder_get_object(builder, "motd_label"));

    GtkButton *verify_btn = GTK_BUTTON(gtk_builder_get_object(builder, "verify_btn"));
    GtkButton *save_btn = GTK_BUTTON(gtk_builder_get_object(builder, "save_btn"));
    GtkButton *refresh_btn = GTK_BUTTON(gtk_builder_get_object(builder, "refresh_btn"));

    /* Bind downloader and patch widgets */
    ctx->patch_status_label = GTK_LABEL(gtk_builder_get_object(builder, "patch_status_label"));
    ctx->update_btn = GTK_BUTTON(gtk_builder_get_object(builder, "update_btn"));
    ctx->play_btn = GTK_BUTTON(gtk_builder_get_object(builder, "play_btn"));
    ctx->download_progress = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, "download_progress"));
    ctx->download_speed_label = GTK_LABEL(gtk_builder_get_object(builder, "download_speed_label"));
    ctx->download_eta_label = GTK_LABEL(gtk_builder_get_object(builder, "download_eta_label"));
    ctx->download_file_label = GTK_LABEL(gtk_builder_get_object(builder, "download_file_label"));

    if (verify_btn) {
        g_signal_connect(verify_btn, "clicked", G_CALLBACK(__on_verify_clicked__), ctx);
    }
    if (save_btn) {
        g_signal_connect(save_btn, "clicked", G_CALLBACK(__on_save_clicked__), ctx);
    }
    if (refresh_btn) {
        g_signal_connect(refresh_btn, "clicked", G_CALLBACK(__on_refresh_clicked__), ctx);
    }
    if (ctx->update_btn) {
        g_signal_connect(ctx->update_btn, "clicked", G_CALLBACK(__on_update_clicked__), ctx);
    }
    if (ctx->play_btn) {
        g_signal_connect(ctx->play_btn, "clicked", G_CALLBACK(__on_play_clicked__), ctx);
    }

    g_signal_connect(window, "destroy", G_CALLBACK(__on_window_destroy__), ctx);

    const char *hwid = __hwid_get__();
    char ip[64];
    __network_get_client_ip__(ip, sizeof(ip));

    if (ctx->client_info_label) {
        char info[256];
        snprintf(info, sizeof(info), "Hardware ID: %.16s... | Network IP: %s", hwid, ip);
        gtk_label_set_text(ctx->client_info_label, info);
    }

    if (gStructure) {
        gtk_editable_set_text(GTK_EDITABLE(ctx->username_entry), gStructure->credential.username);
        gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), gStructure->credential.password);

        char init_log[256];
        if (strlen(gStructure->credential.token) > 0) {
            snprintf(init_log, sizeof(init_log), "%s v%s ready (User: %s | Permanent Token: %.12s...)",
                     APP_NAME, APP_VERSION_STRING, gStructure->credential.username, gStructure->credential.token);
        } else {
            snprintf(init_log, sizeof(init_log), "%s v%s ready (User: %s)",
                     APP_NAME, APP_VERSION_STRING,
                     strlen(gStructure->credential.username) > 0 ? gStructure->credential.username : "None");
        }
        __append_log__(ctx, "CORE", init_log);
    }

    ctx->live_feed = __live_feed_create__("127.0.0.1", 8080, "/live", false);
    __live_feed_set_callback__(ctx->live_feed, __on_live_stats_updated__, ctx);
    __live_feed_start__(ctx->live_feed);
    __append_log__(ctx, "SOCKET", "Live server feed started");

    gtk_application_add_window(app, GTK_WINDOW(window));
    return window;
}
