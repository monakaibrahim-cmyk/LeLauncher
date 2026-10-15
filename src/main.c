#include <core/Version.h>
#include <core/logger.h>
#include <ui/app_window.h>

int main(int argc, char *argv[]) {
    LOG_INFO("SYS", "Starting %s v%s (GTK 4)...", APP_NAME, APP_VERSION_STRING);
    return app_window_run(argc, argv);
}
