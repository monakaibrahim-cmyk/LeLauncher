#include <core/Version.h>
#include <core/logger.h>

#if defined(USE_RAYGUI)
    #include <ui/raygui_window.h>
#elif defined(USE_GTK4)
    #include <gtk/gtk.h>
    #include <ui/app_window.h>

    static void __on_activate__(GtkApplication *app, gpointer user_data) {
        (void)user_data;
        GtkWidget *window = __app_window_create__(app);
        if (window) {
            gtk_window_present(GTK_WINDOW(window));
        }
    }
#endif

int main(int argc, char *argv[]) {
#if defined(USE_RAYGUI)
    LOG_INFO("SYS", "Starting %s with Raygui backend...", APP_NAME);
    return raygui_window_run(argc, argv);
#elif defined(USE_GTK4)
    LOG_INFO("SYS", "Starting %s with GTK 4 backend...", APP_NAME);
    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(__on_activate__), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
#else
    #error "No GUI backend configured! Define USE_GTK4 or USE_RAYGUI."
#endif
}
