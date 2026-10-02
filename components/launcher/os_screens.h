#pragma once

#include "os.h"
#include "os_alerts.h"

extern const os_screen_t os_face_screen;
extern const os_screen_t os_menu_screen;
extern const os_screen_t os_apps_screen;
extern const os_screen_t os_settings_screen;
extern const os_screen_t os_time_screen;
extern const os_screen_t os_battery_screen;
extern const os_screen_t os_about_screen;
extern const os_screen_t os_poweroff_screen;
extern const os_screen_t os_usb_screen;
extern const os_screen_t os_stopwatch_screen;
extern const os_screen_t os_flashlight_screen;
extern const os_screen_t os_timer_screen;
extern const os_screen_t os_alarms_screen;
extern const os_screen_t os_alarm_edit_screen;
extern const os_screen_t os_soon_screen;

// Gives the USB port back to USB-Serial-JTAG (console, flashing) after USB mode (os_usb.c).
void os_usb_restore_port(void);

// Apps in the OTA slots (os_apps.c).
void os_apps_scan(void);
int os_apps_count(void);
const char *os_apps_name(const char *label); // app name in that slot, or the label

// Title shown by os_soon_screen.
void os_soon_set_title(const char *title);

// Shows the ring/missed screen over the face and starts the sound (os_alarms.c).
void os_alert_present(const os_alert_t *alert);
// True while that screen is up: the system task then holds back further alerts.
bool os_alert_showing(void);
