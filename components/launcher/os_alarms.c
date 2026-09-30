// Alarm list and editor, and the screen shown when a timer or alarm goes off.
#include <stdio.h>

#include "os_alerts.h"
#include "os_screens.h"

// ---------- Alarm list ----------

static int s_edit; // alarm being edited

static void row_clicked(lv_event_t *e)
{
    s_edit = (int)(intptr_t)lv_event_get_user_data(e);
    os_push(&os_alarm_edit_screen);
}

static void pill_clicked(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    const os_alarm_t *a = os_alarm_get(i);
    os_alarm_set(i, !a->enabled, a->repeat, a->hour, a->minute);
    os_replace(&os_alarms_screen);
}

static void alarms_create(lv_obj_t *root)
{
    os_title(root, "Alarmas");
    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 350, LV_SIZE_CONTENT);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 12, 0);

    for (int i = 0; i < OS_ALARM_COUNT; i++) {
        const os_alarm_t *a = os_alarm_get(i);
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 78);
        lv_obj_set_style_radius(row, 22, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(OS_SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(OS_BORDER), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_hor(row, 20, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        os_style_focus_ring(row, 3);
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        char text[8];
        snprintf(text, sizeof(text), "%02d:%02d", a->hour, a->minute);
        lv_obj_t *time = os_label(row, &font_title_30, a->enabled ? OS_TEXT : OS_MUTED, text);
        lv_obj_align(time, LV_ALIGN_LEFT_MID, 0, -10);
        lv_obj_t *sub = os_label(row, &font_barlow_16, OS_MUTED, !a->enabled ? "Desactivada" : a->repeat ? "Cada día" : "Una vez");
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 0, 18);

        lv_obj_t *pill = lv_button_create(row);
        lv_obj_remove_style_all(pill);
        lv_obj_set_size(pill, 76, 40);
        lv_obj_align(pill, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, lv_color_hex(a->enabled ? OS_ACCENT : 0x1F1F1F), 0);
        lv_obj_add_event_cb(pill, pill_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(os_label(pill, &font_barlow_semibold_22, a->enabled ? OS_BG : OS_MUTED, a->enabled ? "ON" : "OFF"));
        os_focus_add(row);
    }
    os_hint(root, "BOOT editar · PWR volver");
}

const os_screen_t os_alarms_screen = {
    .name = "alarms",
    .create = alarms_create,
};

// ---------- Alarm editor ----------

static struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *repeat_label;
    bool repeat;
} s_ed;

static void edit_save(lv_event_t *e)
{
    os_alarm_set(s_edit, true, s_ed.repeat, (int)lv_roller_get_selected(s_ed.hour),
                 (int)lv_roller_get_selected(s_ed.minute));
    os_back();
}

static void edit_off(lv_event_t *e)
{
    const os_alarm_t *a = os_alarm_get(s_edit);
    os_alarm_set(s_edit, false, a->repeat, a->hour, a->minute);
    os_back();
}

static void edit_repeat(lv_event_t *e)
{
    s_ed.repeat = !s_ed.repeat;
    lv_label_set_text(s_ed.repeat_label, s_ed.repeat ? "Cada día" : "Una vez");
}

static lv_obj_t *pill_button(lv_obj_t *parent, int width, const char *text, uint32_t bg, uint32_t fg,
                             lv_event_cb_t cb, lv_obj_t **label)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, width, 50);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(OS_BORDER), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    os_style_focus_ring(b, 3);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = os_label(b, &font_barlow_semibold_22, fg, text);
    lv_obj_center(l);
    if (label != NULL) {
        *label = l;
    }
    return b;
}

static void edit_create(lv_obj_t *root)
{
    static char hours[24 * 3 + 1];
    static char minutes[60 * 3 + 1];
    const os_alarm_t *a = os_alarm_get(s_edit);
    struct tm tm;
    os_now(&tm);
    // A fresh alarm starts from the current time; an existing one from its own.
    const int hour = a->enabled || a->hour != 0 || a->minute != 0 ? a->hour : tm.tm_hour;
    const int minute = a->enabled || a->hour != 0 || a->minute != 0 ? a->minute : tm.tm_min;
    s_ed.repeat = a->repeat;

    char title[24];
    snprintf(title, sizeof(title), "Alarma %d", s_edit + 1);
    os_title(root, title);
    lv_obj_t *r = os_roller_row(root, "HORA", 92);
    s_ed.hour = os_roller(r, os_range_options(hours, sizeof(hours), 0, 23), hour, 90);
    os_label(r, &font_title_30, OS_ACCENT, ":");
    s_ed.minute = os_roller(r, os_range_options(minutes, sizeof(minutes), 0, 59), minute, 90);

    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_t *save = pill_button(col, 220, "Guardar", OS_ACCENT, OS_BG, edit_save, NULL);
    lv_obj_t *repeat = pill_button(col, 220, s_ed.repeat ? "Cada día" : "Una vez", OS_SURFACE, OS_TEXT, edit_repeat,
                                   &s_ed.repeat_label);
    os_focus_add(save);
    os_focus_add(repeat);
    if (a->enabled) {
        os_focus_add(pill_button(col, 220, "Desactivar", OS_SURFACE, OS_WARN, edit_off, NULL));
    }
    os_hint(root, "BOOT aceptar · PWR volver");
}

const os_screen_t os_alarm_edit_screen = {
    .name = "alarm_edit",
    .create = edit_create,
};

// ---------- Alert ----------

static struct {
    os_alert_t alert;
    bool showing;
} s_al;

bool os_alert_showing(void)
{
    return s_al.showing;
}

static void dismiss(void)
{
    os_alert_output_stop();
    os_back();
}

static void dismiss_clicked(lv_event_t *e)
{
    dismiss();
}

static void alert_create(lv_obj_t *root)
{
    const bool timer = s_al.alert.kind == OS_ALERT_TIMER;
    const os_alarm_t *a = os_alarm_get(s_al.alert.index);
    const uint32_t color = s_al.alert.missed ? OS_MUTED : OS_ACCENT;

    lv_obj_t *icon = os_label(root, &font_icons_32, color, timer ? OS_ICON_HOURGLASS : OS_ICON_BELL);
    lv_obj_set_style_transform_scale(icon, 512, 0); // 32 px glyph shown at ~64 px
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -150);

    const char *title = timer ? "Temporizador" : "Alarma";
    lv_obj_t *label = os_label(root, &font_title_30, OS_TEXT, title);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -60);

    char text[40];
    if (timer) {
        snprintf(text, sizeof(text), s_al.alert.missed ? "Terminó mientras usabas una app" : "Ha terminado");
    } else {
        snprintf(text, sizeof(text), s_al.alert.missed ? "%02d:%02d · perdida" : "%02d:%02d", a->hour, a->minute);
    }
    lv_obj_t *detail = os_label(root, timer ? &font_barlow_20 : &font_digits_72, OS_TEXT_2, text);
    if (!timer && !s_al.alert.missed) {
        lv_obj_set_style_text_color(detail, lv_color_hex(OS_TEXT), 0);
    } else if (!timer) {
        lv_obj_set_style_text_font(detail, &font_barlow_20, 0);
    }
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *stop = lv_button_create(root);
    lv_obj_remove_style_all(stop);
    lv_obj_set_size(stop, 220, 56);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -92);
    lv_obj_set_style_radius(stop, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(stop, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(color), 0);
    os_style_focus_ring(stop, 3);
    lv_obj_add_event_cb(stop, dismiss_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_center(os_label(stop, &font_barlow_semibold_22, OS_BG, s_al.alert.missed ? "Cerrar" : "Parar"));
    os_focus_add(stop);
    os_hint(root, "BOOT o PWR para parar");
    s_al.showing = true;
}

static void alert_destroy(void)
{
    s_al.showing = false;
    os_alert_output_stop();
}

// When the output gives up after 60 s the notice stays until dismissed.
static bool alert_boot(void)
{
    dismiss();
    return true;
}

static bool alert_pwr(void)
{
    dismiss();
    return true;
}

static const os_screen_t alert_screen = {
    .name = "alert",
    .create = alert_create,
    .destroy = alert_destroy,
    .on_boot = alert_boot,
    .on_pwr = alert_pwr,
    .keep_awake = true,
};

void os_alert_present(const os_alert_t *alert)
{
    s_al.alert = *alert;
    os_home();
    os_push(&alert_screen);
    if (!alert->missed) {
        os_alert_output_start();
    }
}
