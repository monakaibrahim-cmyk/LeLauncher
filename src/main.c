#include <gtk/gtk.h>
#include <core/Version.h>
#include <core/dat.h>
#include <ui/app_window.h>

static void __on_activate__(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    GtkWidget *window = __app_window_create__(app);
    if (window) {
        gtk_window_present(GTK_WINDOW(window));
    }
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(__on_activate__), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
