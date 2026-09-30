#pragma once

// Internal API of the watch OS (the launcher): theme, screen stack, button focus and
// shared widgets. Every os_* call that touches LVGL runs with the LVGL lock held: the
// system task takes it before calling into screens.

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "lvgl.h"
#include "watch_battery.h"

// ---- Theme (docs: README "Diseno") ----
#define OS_BG 0x000000
#define OS_TEXT 0xF4F1EA
#define OS_TEXT_2 0xC9C6BE // secondary text
#define OS_MUTED 0x8C8A84  // captions, hints
#define OS_SURFACE 0x121212
#define OS_BORDER 0x2A2A2A
#define OS_TRACK 0x1C1C1C
#define OS_ACCENT 0x4FD1C5
#define OS_WARN 0xF2A541   // low battery, power off
#define OS_OK 0x7BE07F     // charging
#define OS_FOCUS_STATE LV_STATE_USER_1

LV_FONT_DECLARE(font_clock_158);
LV_FONT_DECLARE(font_digits_72);
LV_FONT_DECLARE(font_title_30);
LV_FONT_DECLARE(font_barlow_16);
LV_FONT_DECLARE(font_barlow_18);
LV_FONT_DECLARE(font_barlow_20);
LV_FONT_DECLARE(font_barlow_semibold_22);
LV_FONT_DECLARE(font_icons_24);
LV_FONT_DECLARE(font_icons_32);

// Lucide glyphs in font_icons_24/32 (UTF-8).
#define OS_ICON_APPS "\xee\x83\xbf"        // layout-grid
#define OS_ICON_STOPWATCH "\xee\x87\xa0"   // timer
#define OS_ICON_HOURGLASS "\xee\x8a\x96"
#define OS_ICON_BELL "\xee\x81\x99"
#define OS_ICON_FLASHLIGHT "\xee\x83\x93"
#define OS_ICON_SETTINGS "\xee\x85\x94"
#define OS_ICON_POWER "\xee\x85\x80"
#define OS_ICON_SUN "\xee\x85\xb8"
#define OS_ICON_BATTERY "\xee\x81\x93"
#define OS_ICON_CLOCK "\xee\x82\x87"
#define OS_ICON_INFO "\xee\x83\xb9"
#define OS_ICON_CHEVRON "\xee\x81\xaf"
#define OS_ICON_ZAP "\xee\x86\xb4"
#define OS_ICON_PLAY "\xee\x84\xbc"
#define OS_ICON_PAUSE "\xee\x84\xae"
#define OS_ICON_RESET "\xee\x85\x88"       // rotate-ccw
#define OS_ICON_FLAG "\xee\x83\x91"
#define OS_ICON_ALARM "\xee\x80\xba"
#define OS_ICON_SCREEN_OFF "\xee\x87\x9c"
#define OS_ICON_CALENDAR "\xee\x81\xa3"
#define OS_ICON_CROSSHAIR "\xee\x82\xac"
#define OS_ICON_DROPLET "\xee\x82\xb4"
#define OS_ICON_GRID3 "\xee\x83\xa9"
#define OS_ICON_GAMEPAD "\xee\x83\x9f"
#define OS_ICON_MIC "\xee\x84\x98"
#define OS_ICON_USB "\xee\x8d\x96"

// ---- Screens ----
typedef struct {
    const char *name;
    void (*create)(lv_obj_t *root); // build the screen under root (black, 410x502)
    void (*destroy)(void);          // optional: forget pointers into the deleted tree
    void (*tick)(void);             // optional: once a second and right after creation
    bool (*on_boot)(void);          // optional: BOOT short press; false = click the focus
    bool (*on_pwr)(void);           // optional: PWR short press; false = back (sleep on the face)
    bool keep_awake;                // no screen-off timeout while shown
} os_screen_t;

void os_push(const os_screen_t *screen);
void os_replace(const os_screen_t *screen); // swap the top screen
void os_back(void);
void os_home(void); // back to the watch face, dropping the stack
void os_request_sleep(void); // turn the screen off as soon as the system task can

// Starts the system task (buttons, screen timeout, sleep, ticks) on the face.
void os_start(const os_screen_t *face);

// ---- Button focus ----
// Focusable objects of the current screen, in order: BOOT long press moves the focus
// ring to the next one, BOOT short press clicks it. Cleared on every screen change.
void os_focus_add(lv_obj_t *obj);
void os_focus_set(int index);
int os_focus_get(void);

// ---- Widgets ----
lv_obj_t *os_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
// Title row "<title>  HH:MM" at the top; returns the row.
lv_obj_t *os_title(lv_obj_t *root, const char *title);
// Bottom hint line ("BOOT abrir · PWR volver").
lv_obj_t *os_hint(lv_obj_t *root, const char *text);
// Round icon button; diameter px, icon font 24 or 32. Focus ring included.
lv_obj_t *os_round_button(lv_obj_t *parent, int diameter, const char *icon, const lv_font_t *font,
                          lv_event_cb_t on_click, void *user);
// Touch roller (3 rows) styled for the OS; options are "\n"-separated.
lv_obj_t *os_roller(lv_obj_t *parent, const char *options, int selected, int width);
// Fills buf with "from\n...\nto" as two-digit numbers.
char *os_range_options(char *buf, size_t size, int from, int to);
// Caption at y plus a centred row to hold rollers; returns the row.
lv_obj_t *os_roller_row(lv_obj_t *root, const char *caption, int y);
// Adds the focus ring style (accent border in OS_FOCUS_STATE) to any object.
void os_style_focus_ring(lv_obj_t *obj, int width);

// Segmented battery icon. scale 1 = status bar (34x17), 3 = large (~120x58).
lv_obj_t *os_battery_icon(lv_obj_t *parent, int scale);
void os_battery_icon_set(lv_obj_t *icon, const watch_battery_t *battery);
uint32_t os_battery_color(const watch_battery_t *battery);

// Cached battery reading, refreshed at most every few seconds.
const watch_battery_t *os_battery(void);

// Local time helpers.
void os_now(struct tm *out);
const char *os_weekday_name(int wday); // "LUNES"...
const char *os_month_short(int mon);   // "ENE"...
