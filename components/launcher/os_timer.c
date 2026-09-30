// Timer screen: pick minutes and seconds, then a countdown with pause and reset.
#include <stdio.h>

#include "os_alerts.h"
#include "os_screens.h"

static struct {
    lv_obj_t *min;
    lv_obj_t *sec;
    lv_obj_t *digits;
    lv_obj_t *bar;
    lv_obj_t *play;
    lv_timer_t *timer;
} s_tm;

static void start_from_rollers(void)
{
    int seconds = (int)lv_roller_get_selected(s_tm.min) * 60 + (int)lv_roller_get_selected(s_tm.sec);
    if (seconds < 1) {
        seconds = 1;
    }
    os_timer_start(seconds);
    os_replace(&os_timer_screen);
}

static void update_countdown(void)
{
    const int64_t ms = os_timer_remaining_ms();
    const int64_t s = (ms + 999) / 1000; // show the second that is still running
    if (s >= 3600) {
        lv_label_set_text_fmt(s_tm.digits, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    } else {
        lv_label_set_text_fmt(s_tm.digits, "%02d:%02d", (int)(s / 60), (int)(s % 60));
    }
    const int total = os_timer_total_s() * 1000;
    lv_bar_set_value(s_tm.bar, total > 0 ? (int)(ms * 1000 / total) : 0, LV_ANIM_OFF);
    lv_label_set_text(lv_obj_get_child(s_tm.play, 0), os_timer_state() == OS_TIMER_RUNNING ? OS_ICON_PAUSE : OS_ICON_PLAY);
}

static void toggle_running(void)
{
    if (os_timer_state() == OS_TIMER_RUNNING) {
        os_timer_pause();
    } else {
        os_timer_resume();
    }
    update_countdown();
}

static void on_start(lv_event_t *e)
{
    start_from_rollers();
}

static void on_toggle(lv_event_t *e)
{
    toggle_running();
}

static void on_reset(lv_event_t *e)
{
    os_timer_reset();
    os_replace(&os_timer_screen);
}

static void countdown_cb(lv_timer_t *t)
{
    // The alert takes over the screen when it reaches zero; here only repaint.
    if (os_timer_state() != OS_TIMER_IDLE) {
        update_countdown();
    }
}

static void countdown_screen_create(lv_obj_t *root)
{
    s_tm.timer = NULL;
    os_title(root, "Temporizador");

    if (os_timer_state() == OS_TIMER_IDLE) {
        static char minutes[100 * 3 + 1];
        static char seconds[60 * 3 + 1];
        const int total = os_timer_total_s();
        lv_obj_t *r = os_roller_row(root, "MINUTOS · SEGUNDOS", 130);
        s_tm.min = os_roller(r, os_range_options(minutes, sizeof(minutes), 0, 99), total / 60, 100);
        os_label(r, &font_title_30, OS_ACCENT, ":");
        s_tm.sec = os_roller(r, os_range_options(seconds, sizeof(seconds), 0, 59), total % 60, 100);

        lv_obj_t *start = os_round_button(root, 84, OS_ICON_PLAY, &font_icons_32, on_start, NULL);
        lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, -92);
        lv_obj_set_style_bg_color(start, lv_color_hex(0x10302D), 0);
        lv_obj_set_style_border_color(start, lv_color_hex(OS_ACCENT), 0);
        os_focus_add(start);
        os_hint(root, "BOOT iniciar · PWR volver");
        return;
    }

    s_tm.digits = os_label(root, &font_digits_72, OS_TEXT, "");
    lv_obj_align(s_tm.digits, LV_ALIGN_CENTER, 0, -60);
    s_tm.bar = lv_bar_create(root);
    lv_obj_set_size(s_tm.bar, 236, 4);
    lv_bar_set_range(s_tm.bar, 0, 1000);
    lv_obj_set_style_bg_color(s_tm.bar, lv_color_hex(OS_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_tm.bar, lv_color_hex(OS_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_tm.bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tm.bar, 2, LV_PART_INDICATOR);
    lv_obj_align(s_tm.bar, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *buttons = lv_obj_create(root);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(buttons, LV_ALIGN_CENTER, 0, 110);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 36, 0);
    lv_obj_t *reset = os_round_button(buttons, 64, OS_ICON_RESET, &font_icons_24, on_reset, NULL);
    s_tm.play = os_round_button(buttons, 84, OS_ICON_PLAY, &font_icons_32, on_toggle, NULL);
    lv_obj_set_style_bg_color(s_tm.play, lv_color_hex(0x10302D), 0);
    lv_obj_set_style_border_color(s_tm.play, lv_color_hex(OS_ACCENT), 0);
    os_focus_add(s_tm.play);
    os_focus_add(reset);
    os_hint(root, "BOOT pausar · PWR volver");
    s_tm.timer = lv_timer_create(countdown_cb, 200, NULL);
    update_countdown();
}

static void timer_destroy(void)
{
    if (s_tm.timer != NULL) {
        lv_timer_delete(s_tm.timer);
        s_tm.timer = NULL;
    }
}

// BOOT: start when idle, pause/resume otherwise, whatever has the focus ring.
static bool timer_boot(void)
{
    if (os_timer_state() == OS_TIMER_IDLE) {
        start_from_rollers();
    } else {
        toggle_running();
    }
    return true;
}

const os_screen_t os_timer_screen = {
    .name = "timer",
    .create = countdown_screen_create,
    .destroy = timer_destroy,
    .on_boot = timer_boot,
};
