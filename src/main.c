#include <core/Version.h>
#include <core/logger.h>
#include <ui/raygui_window.h>

int main(int argc, char *argv[]) {
    LOG_INFO("SYS", "Starting %s v%s...", APP_NAME, APP_VERSION_STRING);
    return raygui_window_run(argc, argv);
}
