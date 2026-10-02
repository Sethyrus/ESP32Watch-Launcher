// Settings: time and date, brightness, screen timeout, battery, USB disk, about, power off.
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "os_alerts.h"
#include "os_screens.h"
#include "os_store.h"
#include "watch_display.h"
#include "watch_power.h"
#include "watch_rtc.h"

static const char *TAG = "os_settings";

static const int BRIGHTNESS_STEPS[] = {20, 40, 60, 80, 100};
static const int TIMEOUT_STEPS[] = {10, 15, 30, 60};
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

// ---------- helpers ----------

static lv_obj_t *group(lv_obj_t *root, int y)
{
    lv_obj_t *g = lv_obj_create(root);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, 342, LV_SIZE_CONTENT);
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(g, 24, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g, lv_color_hex(OS_SURFACE), 0);
    lv_obj_set_style_border_width(g, 1, 0);
    lv_obj_set_style_border_color(g, lv_color_hex(0x242424), 0);
    lv_obj_set_style_clip_corner(g, true, 0);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    return g;
}

// A 56 px row: caption on the left, a value label (returned through value) on the right.
static lv_obj_t *row(lv_obj_t *g, const char *caption, uint32_t color, lv_event_cb_t cb, lv_obj_t **value, bool last)
{
    lv_obj_t *r = lv_button_create(g);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 56);
    lv_obj_set_style_pad_hor(r, 18, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x1F1F1F), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, OS_FOCUS_STATE);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x1A1A1A), OS_FOCUS_STATE);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, OS_FOCUS_STATE);
    lv_obj_set_style_border_width(r, 3, OS_FOCUS_STATE);
    lv_obj_set_style_border_color(r, lv_color_hex(OS_ACCENT), OS_FOCUS_STATE);
    if (!last) {
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_color(r, lv_color_hex(0x242424), 0);
    }
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, NULL);
    os_label(r, &font_barlow_18, color, caption);
    lv_obj_t *v = os_label(r, &font_barlow_16, color == OS_TEXT ? 0xA9A69F : color, "");
    if (value != NULL) {
        *value = v;
    }
    os_focus_add(r);
    return r;
}

static int next_step(const int *steps, int count, int current)
{
    for (int i = 0; i < count; i++) {
        if (steps[i] > current) {
            return steps[i];
        }
    }
    return steps[0];
}

// ---------- Ajustes ----------

static struct {
    lv_obj_t *time;
    lv_obj_t *brightness;
    lv_obj_t *timeout;
    lv_obj_t *battery;
    int focus;
} s_set;

static void show_brightness(void)
{
    const int level = os_settings()->brightness;
    for (int i = 0; i < COUNT(BRIGHTNESS_STEPS); i++) {
        lv_obj_t *bar = lv_obj_get_child(s_set.brightness, i);
        lv_obj_set_style_bg_color(bar, lv_color_hex(BRIGHTNESS_STEPS[i] <= level ? OS_ACCENT : OS_BORDER), 0);
    }
}

static void on_time(lv_event_t *e)
{
    s_set.focus = os_focus_get();
    os_push(&os_time_screen);
}

static void on_brightness(lv_event_t *e)
{
    os_settings_t *st = os_settings();
    st->brightness = next_step(BRIGHTNESS_STEPS, COUNT(BRIGHTNESS_STEPS), st->brightness);
    watch_display_set_brightness(st->brightness);
    os_settings_save();
    show_brightness();
}

static void on_timeout(lv_event_t *e)
{
    os_settings_t *st = os_settings();
    st->screen_timeout_s = next_step(TIMEOUT_STEPS, COUNT(TIMEOUT_STEPS), st->screen_timeout_s);
    os_settings_save();
    lv_label_set_text_fmt(s_set.timeout, "%d s", st->screen_timeout_s);
}

static void on_battery(lv_event_t *e)
{
    s_set.focus = os_focus_get();
    os_push(&os_battery_screen);
}

static void on_usb(lv_event_t *e)
{
    s_set.focus = os_focus_get();
    os_push(&os_usb_screen);
}

static void on_about(lv_event_t *e)
{
    s_set.focus = os_focus_get();
    os_push(&os_about_screen);
}

static void on_poweroff(lv_event_t *e)
{
    s_set.focus = os_focus_get();
    os_push(&os_poweroff_screen);
}

static void settings_create(lv_obj_t *root)
{
    os_title(root, "Ajustes");
    lv_obj_t *g = group(root, 84);
    row(g, "Hora y fecha", OS_TEXT, on_time, &s_set.time, false);

    lv_obj_t *placeholder = NULL;
    lv_obj_t *r = row(g, "Brillo", OS_TEXT, on_brightness, &placeholder, false);
    lv_obj_delete(placeholder);
    s_set.brightness = lv_obj_create(r);
    lv_obj_remove_style_all(s_set.brightness);
    lv_obj_set_size(s_set.brightness, LV_SIZE_CONTENT, 24);
    lv_obj_set_flex_flow(s_set.brightness, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_set.brightness, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(s_set.brightness, 4, 0);
    lv_obj_remove_flag(s_set.brightness, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < COUNT(BRIGHTNESS_STEPS); i++) {
        lv_obj_t *bar = lv_obj_create(s_set.brightness);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 8, 8 + i * 4);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    show_brightness();

    row(g, "Apagar pantalla", OS_TEXT, on_timeout, &s_set.timeout, false);
    lv_label_set_text_fmt(s_set.timeout, "%d s", os_settings()->screen_timeout_s);
    row(g, "Batería", OS_TEXT, on_battery, &s_set.battery, false);
    lv_obj_t *usb = NULL;
    row(g, "Conectar al ordenador", OS_TEXT, on_usb, &usb, false);
    lv_label_set_text(usb, OS_ICON_USB);
    lv_obj_set_style_text_font(usb, &font_icons_24, 0);
    lv_obj_t *about = NULL;
    row(g, "Acerca de", OS_TEXT, on_about, &about, false);
    lv_label_set_text_fmt(about, "%d apps", os_apps_count());
    lv_obj_t *power = NULL;
    row(g, "Apagar reloj", OS_WARN, on_poweroff, &power, true);
    lv_label_set_text(power, OS_ICON_POWER);
    lv_obj_set_style_text_font(power, &font_icons_24, 0);
    os_focus_set(s_set.focus);
}

static void settings_tick(void)
{
    struct tm tm;
    os_now(&tm);
    lv_label_set_text_fmt(s_set.time, "%02d:%02d · %02d/%02d", tm.tm_hour, tm.tm_min, tm.tm_mday, tm.tm_mon + 1);
    const watch_battery_t *b = os_battery();
    if (b->percent >= 0) {
        lv_label_set_text_fmt(s_set.battery, "%d %% · %d,%02d V", b->percent, b->millivolts / 1000,
                              b->millivolts % 1000 / 10);
    } else {
        lv_label_set_text(s_set.battery, "Sin batería");
    }
}

const os_screen_t os_settings_screen = {
    .name = "settings",
    .create = settings_create,
    .tick = settings_tick,
};

// ---------- Hora y fecha ----------

static struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *day;
    lv_obj_t *month;
    lv_obj_t *year;
} s_time;

static void time_save(lv_event_t *e)
{
    struct tm tm = {0};
    tm.tm_hour = (int)lv_roller_get_selected(s_time.hour);
    tm.tm_min = (int)lv_roller_get_selected(s_time.minute);
    tm.tm_mday = (int)lv_roller_get_selected(s_time.day) + 1;
    tm.tm_mon = (int)lv_roller_get_selected(s_time.month);
    tm.tm_year = 2024 + (int)lv_roller_get_selected(s_time.year) - 1900;
    struct timeval before;
    struct timeval after;
    gettimeofday(&before, NULL);
    const esp_err_t err = watch_rtc_set_datetime(&tm); // sets the system clock even if the RTC fails
    gettimeofday(&after, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC not written (%s): the time is lost on the next reboot", esp_err_to_name(err));
    }
    const int64_t delta_ms = ((int64_t)after.tv_sec - before.tv_sec) * 1000 + (after.tv_usec - before.tv_usec) / 1000;
    os_alerts_clock_changed(delta_ms);
    os_stopwatch_clock_changed(delta_ms);
    os_back();
}

static void time_create(lv_obj_t *root)
{
    static char hours[24 * 3 + 1];
    static char minutes[60 * 3 + 1];
    static char days[31 * 3 + 1];
    static char years[10 * 5 + 1];
    static const char *const months = "ENE\nFEB\nMAR\nABR\nMAY\nJUN\nJUL\nAGO\nSEP\nOCT\nNOV\nDIC";
    struct tm tm;
    os_now(&tm);

    os_title(root, "Hora y fecha");
    lv_obj_t *r = os_roller_row(root, "HORA", 82);
    s_time.hour = os_roller(r, os_range_options(hours, sizeof(hours), 0, 23), tm.tm_hour, 90);
    os_label(r, &font_title_30, OS_ACCENT, ":");
    s_time.minute = os_roller(r, os_range_options(minutes, sizeof(minutes), 0, 59), tm.tm_min, 90);

    r = os_roller_row(root, "FECHA", 230);
    s_time.day = os_roller(r, os_range_options(days, sizeof(days), 1, 31), tm.tm_mday - 1, 80);
    s_time.month = os_roller(r, months, tm.tm_mon, 90);
    size_t n = 0;
    for (int y = 2024; y <= 2033; y++) {
        n += snprintf(years + n, sizeof(years) - n, y == 2024 ? "%d" : "\n%d", y);
    }
    const int year = tm.tm_year + 1900;
    s_time.year = os_roller(r, years, year >= 2024 && year <= 2033 ? year - 2024 : 0, 100);

    lv_obj_t *save = lv_button_create(root);
    lv_obj_remove_style_all(save);
    lv_obj_set_size(save, 180, 50);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_set_style_radius(save, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(save, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(save, lv_color_hex(OS_ACCENT), 0);
    lv_obj_add_event_cb(save, time_save, LV_EVENT_CLICKED, NULL);
    lv_obj_center(os_label(save, &font_barlow_semibold_22, OS_BG, "Guardar"));
    os_focus_add(save);
    os_hint(root, "BOOT guardar · PWR volver");
}

const os_screen_t os_time_screen = {
    .name = "time",
    .create = time_create,
};

// ---------- Batería ----------

static struct {
    lv_obj_t *icon;
    lv_obj_t *pct;
    lv_obj_t *detail;
} s_bat;

static void battery_create(lv_obj_t *root)
{
    os_title(root, "Batería");
    s_bat.icon = os_battery_icon(root, 3);
    lv_obj_align(s_bat.icon, LV_ALIGN_CENTER, 0, -50);
    s_bat.pct = os_label(root, &font_digits_72, OS_TEXT, "");
    lv_obj_align(s_bat.pct, LV_ALIGN_CENTER, 0, 40);
    s_bat.detail = os_label(root, &font_barlow_18, 0xA9A69F, "");
    lv_obj_align(s_bat.detail, LV_ALIGN_CENTER, 0, 100);
    os_hint(root, "PWR volver");
}

static void battery_tick(void)
{
    const watch_battery_t *b = os_battery();
    os_battery_icon_set(s_bat.icon, b);
    static const char *const states[] = {"en reposo", "cargando", "descargando"};
    if (b->percent >= 0) {
        lv_label_set_text_fmt(s_bat.pct, "%d %%", b->percent);
        lv_label_set_text_fmt(s_bat.detail, "%d,%02d V · %s%s", b->millivolts / 1000, b->millivolts % 1000 / 10,
                              states[b->state], b->usb ? " · USB" : "");
    } else {
        lv_label_set_text(s_bat.pct, "");
        lv_label_set_text(s_bat.detail, "Sin batería · USB");
    }
    lv_obj_set_style_text_color(s_bat.pct, lv_color_hex(os_battery_color(b)), 0);
}

const os_screen_t os_battery_screen = {
    .name = "battery",
    .create = battery_create,
    .tick = battery_tick,
};

// ---------- Acerca de ----------

static const char *reset_reason_name(uint8_t reason)
{
    switch (reason) {
    case ESP_RST_PANIC:
        return "Error";
    case ESP_RST_BROWNOUT:
        return "Tensión baja";
    default:
        return "Watchdog";
    }
}

static void about_create(lv_obj_t *root)
{
    os_title(root, "Acerca de");
    const esp_app_desc_t *desc = esp_app_get_description();
    char resets[160] = "Sin reinicios inesperados";
    const os_reset_t *last = os_reset_last();
    if (last != NULL) {
        char when[48] = "";
        if (last->time != 0) {
            const time_t t = last->time;
            struct tm tm;
            localtime_r(&t, &tm);
            snprintf(when, sizeof(when), " %d/%d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
        }
        snprintf(resets, sizeof(resets), "Reinicios inesperados: %lu\nÚltimo%s\n%s · %s", (unsigned long)os_resets_count(),
                 when, reset_reason_name(last->reason), last->where[0] != '\0' ? os_apps_name(last->where) : "Sistema");
    }
    lv_obj_t *text = os_label(root, &font_barlow_18, OS_TEXT_2, "");
    lv_label_set_text_fmt(text, "ESP32Watch OS\nVersión %s\n\n%d apps instaladas\nESP-IDF %s\n\n%s", desc->version,
                          os_apps_count(), desc->idf_ver, resets);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(text, 6, 0);
    lv_obj_center(text);
    os_hint(root, "PWR volver");
}

const os_screen_t os_about_screen = {
    .name = "about",
    .create = about_create,
};

// ---------- Apagar reloj ----------

static void do_poweroff(lv_event_t *e)
{
    watch_power_off();
}

static void cancel_poweroff(lv_event_t *e)
{
    os_back();
}

static lv_obj_t *pill(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 150, 50);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    os_style_focus_ring(b, 3);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(os_label(b, &font_barlow_semibold_22, fg, text));
    os_focus_add(b);
    return b;
}

static void poweroff_create(lv_obj_t *root)
{
    lv_obj_t *icon = os_label(root, &font_icons_32, OS_WARN, OS_ICON_POWER);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -110);
    lv_obj_t *q = os_label(root, &font_title_30, OS_TEXT, "¿Apagar el reloj?");
    lv_obj_align(q, LV_ALIGN_CENTER, 0, -60);
    lv_obj_t *note = os_label(root, &font_barlow_16, OS_MUTED, "Para encenderlo, pulsa PWR.");
    lv_obj_align(note, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *buttons = lv_obj_create(root);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(buttons, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 16, 0);
    pill(buttons, "Apagar", OS_WARN, OS_BG, do_poweroff);
    pill(buttons, "Cancelar", OS_SURFACE, OS_TEXT, cancel_poweroff);
    os_focus_set(1); // BOOT on the safe option by default
    os_hint(root, "BOOT elegir · PWR volver");
}

const os_screen_t os_poweroff_screen = {
    .name = "poweroff",
    .create = poweroff_create,
};
