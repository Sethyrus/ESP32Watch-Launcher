// Watch face: battery, date, big time, seconds bar, status chips, Apps/Ajustes.
#include <stdio.h>

#include "os_screens.h"
#include "os_store.h"

static struct {
    lv_obj_t *battery;
    lv_obj_t *charging;
    lv_obj_t *pct;
    lv_obj_t *date;
    lv_obj_t *hours;
    lv_obj_t *minutes;
    lv_obj_t *seconds;
    lv_obj_t *chips;
    lv_obj_t *stopwatch_chip;
    lv_obj_t *stopwatch_text;
    lv_obj_t *timer_chip;
    lv_obj_t *timer_text;
    lv_obj_t *alarm_chip;
    lv_obj_t *alarm_text;
} s_face;

static void open_apps(lv_event_t *e)
{
    os_push(&os_apps_screen);
}

static void open_settings(lv_event_t *e)
{
    os_push(&os_settings_screen);
}

static void gesture_cb(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) {
        os_push(&os_menu_screen);
    }
}

static lv_obj_t *chip(lv_obj_t *parent, const char *icon, uint32_t icon_color, lv_obj_t **text)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 7, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(OS_BORDER), 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 8, 0);
    os_label(c, &font_icons_24, icon_color, icon);
    *text = os_label(c, &font_barlow_18, OS_TEXT_2, "");
    return c;
}

static void face_create(lv_obj_t *root)
{
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(col, 36, 0);
    lv_obj_set_style_pad_bottom(col, 34, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *status = lv_obj_create(col);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 8, 0);
    s_face.battery = os_battery_icon(status, 1);
    s_face.charging = os_label(status, &font_icons_24, OS_OK, OS_ICON_ZAP);
    lv_obj_set_style_transform_scale(s_face.charging, 180, 0); // 24 px glyph shown at ~17 px
    s_face.pct = os_label(status, &font_barlow_18, OS_TEXT_2, "");

    lv_obj_t *grow = lv_obj_create(col);
    lv_obj_remove_style_all(grow);
    lv_obj_set_flex_grow(grow, 1);

    s_face.date = os_label(col, &font_barlow_18, OS_MUTED, "");
    lv_obj_set_style_text_letter_space(s_face.date, 3, 0);

    lv_obj_t *time_row = lv_obj_create(col);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_face.hours = os_label(time_row, &font_clock_158, OS_TEXT, "");
    lv_obj_t *colon = os_label(time_row, &font_clock_158, OS_ACCENT, ":");
    lv_obj_set_style_pad_hor(colon, 2, 0);
    s_face.minutes = os_label(time_row, &font_clock_158, OS_TEXT, "");

    s_face.seconds = lv_bar_create(col);
    lv_obj_set_size(s_face.seconds, 236, 4);
    lv_bar_set_range(s_face.seconds, 0, 59);
    lv_obj_set_style_bg_color(s_face.seconds, lv_color_hex(OS_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_face.seconds, lv_color_hex(OS_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_face.seconds, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_face.seconds, 2, LV_PART_INDICATOR);
    lv_obj_set_style_margin_top(s_face.seconds, 10, 0);

    s_face.chips = lv_obj_create(col);
    lv_obj_remove_style_all(s_face.chips);
    lv_obj_set_size(s_face.chips, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(s_face.chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_face.chips, 12, 0);
    lv_obj_set_style_margin_top(s_face.chips, 24, 0);
    s_face.alarm_chip = chip(s_face.chips, OS_ICON_BELL, OS_ACCENT, &s_face.alarm_text);
    s_face.timer_chip = chip(s_face.chips, OS_ICON_HOURGLASS, OS_ACCENT, &s_face.timer_text);
    s_face.stopwatch_chip = chip(s_face.chips, OS_ICON_STOPWATCH, OS_ACCENT, &s_face.stopwatch_text);

    lv_obj_t *grow2 = lv_obj_create(col);
    lv_obj_remove_style_all(grow2);
    lv_obj_set_flex_grow(grow2, 1);

    lv_obj_t *buttons = lv_obj_create(col);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 44, 0);
    os_round_button(buttons, 58, OS_ICON_APPS, &font_icons_24, open_apps, NULL);
    os_round_button(buttons, 58, OS_ICON_SETTINGS, &font_icons_24, open_settings, NULL);
}

static void format_elapsed(char *out, size_t size, int64_t ms)
{
    const int64_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(out, size, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    } else {
        snprintf(out, size, "%02d:%02d", (int)(s / 60), (int)(s % 60));
    }
}

static void face_tick(void)
{
    struct tm tm;
    os_now(&tm);
    lv_label_set_text_fmt(s_face.hours, "%02d", tm.tm_hour);
    lv_label_set_text_fmt(s_face.minutes, "%02d", tm.tm_min);
    lv_bar_set_value(s_face.seconds, tm.tm_sec > 59 ? 59 : tm.tm_sec, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_face.date, "%s %d %s", os_weekday_name(tm.tm_wday), tm.tm_mday, os_month_short(tm.tm_mon));

    const watch_battery_t *b = os_battery();
    os_battery_icon_set(s_face.battery, b);
    if (b->state == WATCH_BATTERY_CHARGING) {
        lv_obj_remove_flag(s_face.charging, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_face.charging, LV_OBJ_FLAG_HIDDEN);
    }
    if (b->percent >= 0) {
        lv_label_set_text_fmt(s_face.pct, "%d %%", b->percent);
    } else {
        lv_label_set_text(s_face.pct, "USB");
    }

    const int64_t sw = os_stopwatch_elapsed_ms();
    if (os_stopwatch_running() || sw > 0) {
        char text[16];
        format_elapsed(text, sizeof(text), sw);
        lv_label_set_text(s_face.stopwatch_text, text);
        lv_obj_remove_flag(s_face.stopwatch_chip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_face.stopwatch_chip, LV_OBJ_FLAG_HIDDEN);
    }

    if (os_timer_state() != OS_TIMER_IDLE) {
        char text[16];
        format_elapsed(text, sizeof(text), (os_timer_remaining_ms() + 999) / 1000 * 1000);
        lv_label_set_text(s_face.timer_text, text);
        lv_obj_remove_flag(s_face.timer_chip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_face.timer_chip, LV_OBJ_FLAG_HIDDEN);
    }

    int hour;
    int minute;
    if (os_alarm_next(&hour, &minute)) {
        lv_label_set_text_fmt(s_face.alarm_text, "%02d:%02d", hour, minute);
        lv_obj_remove_flag(s_face.alarm_chip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_face.alarm_chip, LV_OBJ_FLAG_HIDDEN);
    }
}

// BOOT opens the menu; PWR turns the screen off (default on the root screen).
static bool face_boot(void)
{
    os_push(&os_menu_screen);
    return true;
}

const os_screen_t os_face_screen = {
    .name = "face",
    .create = face_create,
    .tick = face_tick,
    .on_boot = face_boot,
};
