#include "os.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "os_alerts.h"
#include "os_screens.h"
#include "os_store.h"
#include "bsp/display.h"
#include "watch_buttons.h"
#include "watch_display.h"
#include "watch_power.h"

#define OS_STACK_DEPTH 6
#define OS_MAX_FOCUS 16
#define OS_LOOP_MS 20
#define OS_PWR_POLL_MS 50
#define OS_BOOT_LONG_MS 700
#define OS_BATTERY_PERIOD_US 5000000
#define OS_DIM_US 3000000   // dimmed screen before it turns off
#define OS_DIM_PERCENT 15   // dimmed brightness, as a share of the set brightness
#define OS_DIM_MIN 3

static const char *TAG = "os";

static struct {
    const os_screen_t *stack[OS_STACK_DEPTH];
    int depth;
    lv_obj_t *root;
    lv_obj_t *title_clock;
    lv_obj_t *focus[OS_MAX_FOCUS];
    int focus_count;
    int focus_index;
    bool sleep_requested;
    watch_battery_t battery;
    int64_t battery_at;
    bool battery_valid; // false forces a read on the next os_battery()
    bool dimmed;
    int64_t dim_start;
    lv_obj_t *dim_shield;   // transparent layer that swallows the touch that undims
    volatile bool dim_touched;
} s_os;

// ---------- screen stack ----------

static void show_top(void)
{
    const os_screen_t *screen = s_os.stack[s_os.depth - 1];
    lv_obj_t *old = s_os.root;

    s_os.focus_count = 0;
    s_os.focus_index = -1;
    s_os.title_clock = NULL;
    s_os.root = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_os.root);
    lv_obj_set_size(s_os.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_os.root, lv_color_hex(OS_BG), 0);
    lv_obj_set_style_bg_opa(s_os.root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_os.root, lv_color_hex(OS_TEXT), 0);
    lv_obj_set_style_text_font(s_os.root, &font_barlow_18, 0);
    lv_obj_remove_flag(s_os.root, LV_OBJ_FLAG_SCROLLABLE);
    screen->create(s_os.root);
    lv_screen_load(s_os.root);
    if (screen->tick != NULL) {
        screen->tick();
    }
    if (old != NULL) {
        // Deferred: a click handler inside the old screen may be running this.
        lv_obj_delete_async(old);
    }
    ESP_LOGD(TAG, "Screen %s", screen->name);
}

static void destroy_top(void)
{
    const os_screen_t *screen = s_os.stack[s_os.depth - 1];
    if (screen->destroy != NULL) {
        screen->destroy();
    }
}

void os_push(const os_screen_t *screen)
{
    if (s_os.depth == OS_STACK_DEPTH) {
        return;
    }
    if (s_os.depth > 0) {
        destroy_top();
    }
    s_os.stack[s_os.depth++] = screen;
    show_top();
}

void os_replace(const os_screen_t *screen)
{
    destroy_top();
    s_os.stack[s_os.depth - 1] = screen;
    show_top();
}

void os_back(void)
{
    if (s_os.depth <= 1) {
        return;
    }
    destroy_top();
    s_os.depth--;
    show_top();
}

void os_home(void)
{
    if (s_os.depth > 1) {
        destroy_top();
        s_os.depth = 1;
    }
    show_top();
}

void os_request_sleep(void)
{
    s_os.sleep_requested = true;
}

// ---------- focus ----------

void os_focus_add(lv_obj_t *obj)
{
    if (s_os.focus_count < OS_MAX_FOCUS) {
        s_os.focus[s_os.focus_count++] = obj;
    }
}

void os_focus_set(int index)
{
    if (s_os.focus_count == 0) {
        return;
    }
    if (s_os.focus_index >= 0) {
        lv_obj_remove_state(s_os.focus[s_os.focus_index], OS_FOCUS_STATE);
    }
    s_os.focus_index = ((index % s_os.focus_count) + s_os.focus_count) % s_os.focus_count;
    lv_obj_add_state(s_os.focus[s_os.focus_index], OS_FOCUS_STATE);
    lv_obj_scroll_to_view_recursive(s_os.focus[s_os.focus_index], LV_ANIM_ON);
}

int os_focus_get(void)
{
    return s_os.focus_index;
}

// ---------- input dispatch (LVGL lock held) ----------

static void on_boot_short(void)
{
    const os_screen_t *screen = s_os.stack[s_os.depth - 1];
    if (screen->on_boot != NULL && screen->on_boot()) {
        return;
    }
    if (s_os.focus_count > 0) {
        if (s_os.focus_index < 0) {
            os_focus_set(0);
        }
        lv_obj_send_event(s_os.focus[s_os.focus_index], LV_EVENT_CLICKED, NULL);
    }
}

static void on_boot_long(void)
{
    os_focus_set(s_os.focus_index + 1);
}

static void on_pwr(void)
{
    const os_screen_t *screen = s_os.stack[s_os.depth - 1];
    if (screen->on_pwr != NULL && screen->on_pwr()) {
        return;
    }
    if (s_os.depth > 1) {
        os_back();
    } else {
        s_os.sleep_requested = true;
    }
}

static void title_clock_update(void);

// ---------- dimming (LVGL lock held) ----------

static void shield_pressed(lv_event_t *e)
{
    s_os.dim_touched = true;
}

// Timeout reached: dim first, and put a clickable transparent layer on top so a touch
// during the dim only brings the screen back instead of reaching the widget under it.
static void dim_start(int64_t now)
{
    int level = watch_display_get_brightness() * OS_DIM_PERCENT / 100;
    bsp_display_brightness_set(level < OS_DIM_MIN ? OS_DIM_MIN : level); // keeps the saved level
    s_os.dim_shield = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_os.dim_shield);
    lv_obj_set_size(s_os.dim_shield, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_os.dim_shield, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_os.dim_shield, shield_pressed, LV_EVENT_PRESSED, NULL);
    s_os.dim_touched = false;
    s_os.dim_start = now;
    s_os.dimmed = true;
}

static void dim_end(bool restore)
{
    if (s_os.dim_shield != NULL) {
        lv_obj_delete(s_os.dim_shield);
        s_os.dim_shield = NULL;
    }
    if (restore) {
        watch_display_set_brightness(watch_display_get_brightness());
        lv_display_trigger_activity(NULL);
    }
    s_os.dimmed = false;
}

// ---------- system task ----------

static void system_task(void *arg)
{
    watch_boot_debouncer_t boot;
    watch_boot_debouncer_init(&boot, 0, OS_BOOT_LONG_MS);
    int64_t last_pwr = 0;
    int64_t last_tick = 0;
    bool swallow_boot = false; // the BOOT press that undimmed: ignore it until released
    for (;;) {
        const int64_t now = esp_timer_get_time();
        const watch_boot_event_t ev = watch_boot_debouncer_poll(&boot);
        bool pwr = false;
        if (now - last_pwr >= OS_PWR_POLL_MS * 1000) {
            last_pwr = now;
            watch_pwr_key_take_short_press(&pwr);
        }

        bsp_display_lock(0);
        if (s_os.dimmed && (ev.down || pwr || s_os.dim_touched)) {
            dim_end(true);
            swallow_boot = ev.down || ev.held;
            pwr = false;
        }
        if (swallow_boot) {
            swallow_boot = ev.held;
        } else {
            if (ev.down || pwr) {
                lv_display_trigger_activity(NULL);
            }
            if (ev.short_press) {
                on_boot_short();
            }
            if (ev.long_press) {
                on_boot_long();
            }
            if (pwr) {
                on_pwr();
            }
        }
        os_alert_t alert;
        if (!os_alert_showing() && os_alerts_poll(&alert)) {
            if (s_os.dimmed) {
                dim_end(true);
            }
            lv_display_trigger_activity(NULL);
            os_alert_present(&alert);
            s_os.sleep_requested = false; // a PWR press in this same pass must not hide it
        }
        const os_screen_t *screen = s_os.stack[s_os.depth - 1];
        if (now - last_tick >= 1000000) {
            last_tick = now;
            if (screen->tick != NULL) {
                screen->tick();
            }
            title_clock_update();
        }
        const uint32_t idle_ms = lv_display_get_inactive_time(NULL);
        if (s_os.dimmed) {
            if (now - s_os.dim_start >= OS_DIM_US) {
                s_os.sleep_requested = true;
            }
        } else if (!screen->keep_awake && idle_ms > (uint32_t)os_settings()->screen_timeout_s * 1000) {
            dim_start(now);
        }
        if (s_os.sleep_requested && s_os.dimmed) {
            dim_end(false); // watch_power_sleep() restores the saved brightness on wake
        }
        const bool sleep = s_os.sleep_requested;
        s_os.sleep_requested = false;
        bsp_display_unlock();

        if (sleep) {
            // Sleep only until the next timer/alarm is due (+50 ms so it is due on waking).
            const int64_t next = os_alerts_next_in_ms();
            const watch_wake_t why = watch_power_sleep(next < 0 ? 0 : (uint32_t)(next + 50));
            ESP_LOGI(TAG, "Woke up by %s", why == WATCH_WAKE_BOOT ? "BOOT" : why == WATCH_WAKE_PWR ? "PWR" : "timer");
            watch_boot_debouncer_init(&boot, 0, OS_BOOT_LONG_MS);
            s_os.battery_valid = false;
            bsp_display_lock(0);
            os_home();
            bsp_display_unlock();
            last_tick = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(OS_LOOP_MS));
    }
}

esp_err_t os_start(const os_screen_t *face)
{
    s_os.depth = 0;
    os_push(face);
    if (xTaskCreate(system_task, "os_system", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "System task not created: no buttons, timeout or sleep");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// ---------- widgets ----------

lv_obj_t *os_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    return label;
}

void os_label_update(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

void os_label_updatef(lv_obj_t *label, const char *fmt, ...)
{
    char text[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    os_label_update(label, text);
}

static void title_clock_update(void)
{
    if (s_os.title_clock != NULL) {
        struct tm tm;
        os_now(&tm);
        os_label_updatef(s_os.title_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
}

lv_obj_t *os_roller(lv_obj_t *parent, const char *options, int selected, int width)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_roller_set_selected(r, selected, LV_ANIM_OFF);
    lv_obj_set_width(r, width);
    lv_obj_set_style_text_font(r, &font_barlow_semibold_22, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(OS_MUTED), 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(OS_SURFACE), 0);
    lv_obj_set_style_border_color(r, lv_color_hex(OS_BORDER), 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_radius(r, 18, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(OS_TEXT), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x10302D), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    return r;
}

char *os_range_options(char *buf, size_t size, int from, int to)
{
    size_t n = 0;
    buf[0] = '\0';
    for (int v = from; v <= to && n < size; v++) {
        n += snprintf(buf + n, size - n, v == from ? "%02d" : "\n%02d", v);
    }
    return buf;
}

lv_obj_t *os_roller_row(lv_obj_t *root, const char *caption, int y)
{
    lv_obj_t *label = os_label(root, &font_barlow_16, OS_MUTED, caption);
    lv_obj_set_style_text_letter_space(label, 2, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *r = lv_obj_create(root);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y + 24);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    return r;
}

lv_obj_t *os_title(lv_obj_t *root, const char *title)
{
    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 30);
    os_label(row, &font_title_30, OS_TEXT, title);
    lv_obj_t *clock = os_label(row, &font_barlow_18, OS_MUTED, "");
    lv_obj_set_style_pad_bottom(clock, 4, 0);
    s_os.title_clock = clock; // updated every second by the system task
    title_clock_update();
    return row;
}

lv_obj_t *os_hint(lv_obj_t *root, const char *text)
{
    lv_obj_t *hint = os_label(root, &font_barlow_16, OS_MUTED, text);
    lv_obj_set_style_text_letter_space(hint, 1, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    return hint;
}

void os_style_focus_ring(lv_obj_t *obj, int width)
{
    lv_obj_set_style_border_color(obj, lv_color_hex(OS_ACCENT), OS_FOCUS_STATE);
    lv_obj_set_style_border_width(obj, width, OS_FOCUS_STATE);
    lv_obj_set_style_border_color(obj, lv_color_hex(OS_ACCENT), LV_STATE_PRESSED);
}

lv_obj_t *os_round_button(lv_obj_t *parent, int diameter, const char *icon, const lv_font_t *font,
                          lv_event_cb_t on_click, void *user)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, diameter, diameter);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(OS_SURFACE), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1F1F1F), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(OS_BORDER), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    os_style_focus_ring(button, 3);
    lv_obj_t *glyph = os_label(button, font, OS_TEXT, icon);
    lv_obj_center(glyph);
    if (on_click != NULL) {
        lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, user);
    }
    return button;
}

// Icon layout: row { body { 5 segments }, nub }.
lv_obj_t *os_battery_icon(lv_obj_t *parent, int scale)
{
    const int w = scale == 1 ? 34 : 120;
    const int h = scale == 1 ? 17 : 58;
    const int border = scale == 1 ? 2 : 3;
    const int pad = scale == 1 ? 2 : 6;
    const int gap = scale == 1 ? 2 : 5;

    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(icon, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icon, scale == 1 ? 1 : 2, 0);

    lv_obj_t *body = lv_obj_create(icon);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, w, h);
    lv_obj_set_style_border_width(body, border, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(OS_MUTED), 0);
    lv_obj_set_style_radius(body, scale == 1 ? 4 : 12, 0);
    lv_obj_set_style_pad_all(body, pad, 0);
    lv_obj_set_style_pad_column(body, gap, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < 5; i++) {
        lv_obj_t *seg = lv_obj_create(body);
        lv_obj_remove_style_all(seg);
        lv_obj_set_height(seg, LV_PCT(100));
        lv_obj_set_flex_grow(seg, 1);
        lv_obj_set_style_radius(seg, scale == 1 ? 1 : 4, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(0x1F1F1F), 0);
    }
    lv_obj_t *nub = lv_obj_create(icon);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, scale == 1 ? 3 : 7, scale == 1 ? 7 : 22);
    lv_obj_set_style_radius(nub, 2, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(nub, lv_color_hex(OS_MUTED), 0);
    return icon;
}

uint32_t os_battery_color(const watch_battery_t *b)
{
    if (b->usb) {
        return OS_OK;
    }
    if (b->percent >= 0 && b->percent <= 20) {
        return OS_WARN;
    }
    return OS_TEXT;
}

void os_battery_icon_set(lv_obj_t *icon, const watch_battery_t *b)
{
    lv_obj_t *body = lv_obj_get_child(icon, 0);
    lv_obj_t *nub = lv_obj_get_child(icon, 1);
    const uint32_t color = os_battery_color(b);
    const uint32_t frame = color == OS_TEXT ? OS_MUTED : color;
    const int pct = b->percent < 0 ? 0 : b->percent;
    const int filled = pct == 0 ? 0 : (pct + 19) / 20;
    const uintptr_t shown = ((uintptr_t)color << 4 | (uintptr_t)filled) + 1; // 0 = never set
    if ((uintptr_t)lv_obj_get_user_data(icon) == shown) {
        return;
    }
    lv_obj_set_user_data(icon, (void *)shown);
    lv_obj_set_style_border_color(body, lv_color_hex(frame), 0);
    lv_obj_set_style_bg_color(nub, lv_color_hex(frame), 0);
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(body, i), lv_color_hex(i < filled ? color : 0x1F1F1F), 0);
    }
}

const watch_battery_t *os_battery(void)
{
    const int64_t now = esp_timer_get_time();
    if (!s_os.battery_valid || now - s_os.battery_at >= OS_BATTERY_PERIOD_US) {
        s_os.battery_at = now;
        s_os.battery_valid = watch_battery_read(&s_os.battery) == ESP_OK;
    }
    return &s_os.battery;
}

void os_now(struct tm *out)
{
    const time_t t = time(NULL);
    localtime_r(&t, out);
}

const char *os_weekday_name(int wday)
{
    static const char *const names[] = {"DOMINGO", "LUNES", "MARTES", "MIÉRCOLES", "JUEVES", "VIERNES", "SÁBADO"};
    return names[wday >= 0 && wday < 7 ? wday : 0];
}

const char *os_month_short(int mon)
{
    static const char *const names[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                                        "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
    return names[mon >= 0 && mon < 12 ? mon : 0];
}
