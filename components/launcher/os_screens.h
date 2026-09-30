#pragma once

#include "os.h"

extern const os_screen_t os_face_screen;
extern const os_screen_t os_menu_screen;
extern const os_screen_t os_apps_screen;
extern const os_screen_t os_settings_screen;
extern const os_screen_t os_time_screen;
extern const os_screen_t os_battery_screen;
extern const os_screen_t os_about_screen;
extern const os_screen_t os_poweroff_screen;
extern const os_screen_t os_stopwatch_screen;
extern const os_screen_t os_flashlight_screen;
extern const os_screen_t os_soon_screen;

// Apps in the OTA slots (os_apps.c).
void os_apps_scan(void);
int os_apps_count(void);

// Title shown by os_soon_screen.
void os_soon_set_title(const char *title);
