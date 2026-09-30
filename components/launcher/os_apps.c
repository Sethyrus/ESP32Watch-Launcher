// Apps: every OTA slot with a valid image; opening one reboots into it.
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "os_screens.h"
#include "os_store.h"

#define MAX_APPS 8
#define PROJECT_PREFIX "ESP32Watch"

static const char *TAG = "os_apps";

typedef struct {
    const esp_partition_t *partition;
    char name[32];
    char version[32];
} app_t;

static app_t s_apps[MAX_APPS];
static int s_count;
static lv_obj_t *s_hint;
static bool s_launching;

// Known apps get a colour, an icon and a description; others their version.
static const struct {
    const char *name;
    uint32_t color;
    const char *icon;
    const char *description;
} KNOWN[] = {
    {"Maze", 0x1F7A8C, OS_ICON_GRID3, "Laberinto con la IMU"},
    {"Doom", 0x9F1239, OS_ICON_CROSSHAIR, "doomgeneric"},
    {"Fluid", 0x1D4ED8, OS_ICON_DROPLET, "Simulación de fluido"},
};

static int known_index(const char *name)
{
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++) {
        if (strcasecmp(name, KNOWN[i].name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void os_apps_scan(void)
{
    s_count = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *partition = esp_partition_get(it);
        if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            continue;
        }
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(partition, &desc) != ESP_OK) {
            continue;
        }
        if (s_count == MAX_APPS) {
            ESP_LOGW(TAG, "More than %d apps, ignoring slot %s", MAX_APPS, partition->label);
            continue;
        }
        app_t *app = &s_apps[s_count++];
        app->partition = partition;
        const char *name = desc.project_name;
        const size_t prefix = strlen(PROJECT_PREFIX);
        if (strncmp(name, PROJECT_PREFIX, prefix) == 0 && name[prefix] != '\0') {
            name += prefix;
        }
        strlcpy(app->name, name, sizeof(app->name));
        strlcpy(app->version, desc.version, sizeof(app->version));
        ESP_LOGI(TAG, "Slot %s: %s (%s)", partition->label, desc.project_name, desc.version);
    }
    esp_partition_iterator_release(it);
}

int os_apps_count(void)
{
    return s_count;
}

// One LVGL cycle after the click, so "Abriendo..." is on screen before the reboot.
static void launch_timer_cb(lv_timer_t *timer)
{
    const app_t *app = lv_timer_get_user_data(timer);
    os_set_last_app(app->partition->label);
    // Verifies the image first; the app points the next boot back at the launcher.
    esp_err_t err = esp_ota_set_boot_partition(app->partition);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Booting %s from %s", app->name, app->partition->label);
        esp_restart();
    }
    ESP_LOGE(TAG, "Cannot boot %s: %s", app->name, esp_err_to_name(err));
    if (s_hint != NULL) {
        lv_label_set_text_fmt(s_hint, "No se pudo abrir %s", app->name);
        lv_obj_set_style_text_color(s_hint, lv_color_hex(OS_WARN), 0);
    }
    s_launching = false;
}

static void app_clicked(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_launching) {
        return;
    }
    s_launching = true;
    os_focus_set(i);
    lv_label_set_text_fmt(s_hint, "Abriendo %s...", s_apps[i].name);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(OS_ACCENT), 0);
    lv_timer_t *timer = lv_timer_create(launch_timer_cb, 30, &s_apps[i]);
    lv_timer_set_repeat_count(timer, 1);
}

static lv_obj_t *app_card(lv_obj_t *parent, int i)
{
    const app_t *app = &s_apps[i];
    const int k = known_index(app->name);

    lv_obj_t *card = lv_button_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 338, 78);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(OS_SURFACE), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1F1F1F), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(OS_BORDER), 0);
    os_style_focus_ring(card, 3);
    lv_obj_set_style_pad_hor(card, 18, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 16, 0);
    lv_obj_add_event_cb(card, app_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 46, 46);
    lv_obj_set_style_radius(badge, 14, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(k >= 0 ? KNOWN[k].color : 0x334155), 0);
    lv_obj_t *glyph;
    if (k >= 0) {
        glyph = os_label(badge, &font_icons_24, OS_TEXT, KNOWN[k].icon);
    } else {
        char initial[2] = {app->name[0], '\0'};
        glyph = os_label(badge, &font_title_30, OS_TEXT, initial);
    }
    lv_obj_center(glyph);

    lv_obj_t *text = lv_obj_create(card);
    lv_obj_remove_style_all(text);
    lv_obj_set_size(text, 240, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
    lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
    os_label(text, &font_barlow_semibold_22, OS_TEXT, app->name);
    lv_obj_t *sub = os_label(text, &font_barlow_16, OS_MUTED, k >= 0 ? KNOWN[k].description : app->version);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sub, 240);
    return card;
}

static void apps_create(lv_obj_t *root)
{
    s_launching = false;
    os_title(root, "Apps");
    s_hint = os_hint(root, "BOOT abrir · PWR volver");

    if (s_count == 0) {
        lv_obj_t *empty = os_label(root, &font_barlow_18, OS_MUTED, "No hay apps instaladas.\nGrábalas con flash_all.sh");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty);
        return;
    }

    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 360, 330);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 12, 0);
    lv_obj_set_style_pad_ver(list, 4, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    int focus = 0;
    for (int i = 0; i < s_count; i++) {
        os_focus_add(app_card(list, i));
        if (strcmp(s_apps[i].partition->label, os_last_app()) == 0) {
            focus = i;
        }
    }
    os_focus_set(focus);
}

static void apps_destroy(void)
{
    s_hint = NULL;
}

const os_screen_t os_apps_screen = {
    .name = "apps",
    .create = apps_create,
    .destroy = apps_destroy,
};
