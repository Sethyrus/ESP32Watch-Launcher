// Watch tools: stopwatch, flashlight, and a placeholder for features not built yet.
#include <stdio.h>

#include "esp_err.h"
#include "bsp/display.h"
#include "os_screens.h"
#include "os_store.h"
#include "watch_display.h"

// ---------- Cronómetro ----------

static struct {
    lv_obj_t *digits;
    lv_obj_t *tenths;
    lv_obj_t *play;
    lv_timer_t *timer;
} s_sw;

static void sw_update(void)
{
    const int64_t ms = os_stopwatch_elapsed_ms();
    const int64_t s = ms / 1000;
    if (s >= 3600) {
        lv_label_set_text_fmt(s_sw.digits, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    } else {
        lv_label_set_text_fmt(s_sw.digits, "%02d:%02d", (int)(s / 60), (int)(s % 60));
    }
    lv_label_set_text_fmt(s_sw.tenths, ",%d", (int)(ms / 100 % 10));
    lv_label_set_text(lv_obj_get_child(s_sw.play, 0), os_stopwatch_running() ? OS_ICON_PAUSE : OS_ICON_PLAY);
}

static void sw_timer_cb(lv_timer_t *t)
{
    sw_update();
}

static void sw_toggle(lv_event_t *e)
{
    os_stopwatch_toggle();
    sw_update();
}

static void sw_reset(lv_event_t *e)
{
    os_stopwatch_reset();
    sw_update();
}

static void stopwatch_create(lv_obj_t *root)
{
    os_title(root, "Cronómetro");
    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_sw.digits = os_label(row, &font_digits_72, OS_TEXT, "");
    s_sw.tenths = os_label(row, &font_title_30, OS_ACCENT, "");
    lv_obj_set_style_pad_bottom(s_sw.tenths, 10, 0);

    lv_obj_t *buttons = lv_obj_create(root);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(buttons, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 36, 0);
    lv_obj_t *reset = os_round_button(buttons, 64, OS_ICON_RESET, &font_icons_24, sw_reset, NULL);
    s_sw.play = os_round_button(buttons, 84, OS_ICON_PLAY, &font_icons_32, sw_toggle, NULL);
    lv_obj_set_style_bg_color(s_sw.play, lv_color_hex(0x10302D), 0);
    lv_obj_set_style_border_color(s_sw.play, lv_color_hex(OS_ACCENT), 0);
    os_focus_add(s_sw.play);
    os_focus_add(reset);
    os_hint(root, "BOOT iniciar/parar · PWR volver");
    s_sw.timer = lv_timer_create(sw_timer_cb, 100, NULL);
    sw_update();
}

static void stopwatch_destroy(void)
{
    if (s_sw.timer != NULL) {
        lv_timer_delete(s_sw.timer);
        s_sw.timer = NULL;
    }
}

// BOOT always starts/stops, whatever has the focus ring.
static bool stopwatch_boot(void)
{
    os_stopwatch_toggle();
    sw_update();
    return true;
}

const os_screen_t os_stopwatch_screen = {
    .name = "stopwatch",
    .create = stopwatch_create,
    .destroy = stopwatch_destroy,
    .on_boot = stopwatch_boot,
};

// ---------- Linterna ----------

static void flash_back(lv_event_t *e)
{
    os_back();
}

static void flashlight_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, lv_color_white(), 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, flash_back, LV_EVENT_CLICKED, NULL);
    bsp_display_brightness_set(100); // temporary: watch_display keeps the saved level
}

static void flashlight_destroy(void)
{
    watch_display_set_brightness(os_settings()->brightness);
}

static bool flashlight_boot(void)
{
    os_back();
    return true;
}

const os_screen_t os_flashlight_screen = {
    .name = "flashlight",
    .create = flashlight_create,
    .destroy = flashlight_destroy,
    .on_boot = flashlight_boot,
    .keep_awake = true,
};

// ---------- Próximamente ----------

static const char *s_soon_title = "";

void os_soon_set_title(const char *title)
{
    s_soon_title = title;
}

static void soon_create(lv_obj_t *root)
{
    os_title(root, s_soon_title);
    lv_obj_t *text = os_label(root, &font_barlow_20, OS_MUTED, "Próximamente");
    lv_obj_center(text);
    os_hint(root, "PWR volver");
}

const os_screen_t os_soon_screen = {
    .name = "soon",
    .create = soon_create,
};
