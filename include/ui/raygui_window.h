#ifndef __RAYGUI_WINDOW_H__
#define __RAYGUI_WINDOW_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and run the Raygui-based LeLauncher graphical interface.
 * 
 * @param argc Command line argument count
 * @param argv Command line argument vector
 * @return int Application exit code
 */
int raygui_window_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // __RAYGUI_WINDOW_H__
