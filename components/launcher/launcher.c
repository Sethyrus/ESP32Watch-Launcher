#include "launcher.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "lvgl.h"
#include "nvs.h"
#include "watch_buttons.h"
#include "watch_nvs.h"

#define LAUNCHER_MAX_APPS 8
#define LAUNCHER_BUTTON_POLL_MS 20
#define LAUNCHER_PWR_POLL_MS 50
#define LAUNCHER_BOOT_DEBOUNCE_MS 30
#define LAUNCHER_NVS_NAMESPACE "launcher"
#define LAUNCHER_NVS_KEY_LAST "last"
#define LAUNCHER_PROJECT_PREFIX "ESP32Watch"

static const char *TAG = "launcher";

typedef struct {
    const esp_partition_t *partition;
    char name[32];
    char version[32];
} launcher_app_t;

static struct {
    launcher_app_t apps[LAUNCHER_MAX_APPS];
    lv_obj_t *buttons[LAUNCHER_MAX_APPS];
    int app_count;
    int selected;
    lv_obj_t *status;
    bool launching;
    bool boot_pressed;
    uint32_t boot_change_ms;
    uint32_t pwr_last_poll_ms;
} s_launcher;

static const struct {
    const char *name;
    uint32_t color;
} APP_COLORS[] = {
    {"Maze", 0x1f7a8c},
    {"Doom", 0x9f1239},
    {"Fluid", 0x1d4ed8},
};

static uint32_t app_color(const char *name)
{
    for (size_t i = 0; i < sizeof(APP_COLORS) / sizeof(APP_COLORS[0]); ++i) {
        if (strcasecmp(name, APP_COLORS[i].name) == 0) {
            return APP_COLORS[i].color;
        }
    }
    return 0x334155;
}

// Every app slot (ota_N) with a readable app image, in partition table order.
static void scan_apps(void)
{
    s_launcher.app_count = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *partition = esp_partition_get(it);
        if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            continue;
        }
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(partition, &desc) != ESP_OK) {
            ESP_LOGI(TAG, "Slot %s: empty", partition->label);
            continue;
        }
        if (s_launcher.app_count >= LAUNCHER_MAX_APPS) {
            ESP_LOGW(TAG, "More than %d apps, ignoring slot %s", LAUNCHER_MAX_APPS, partition->label);
            continue;
        }

        launcher_app_t *app = &s_launcher.apps[s_launcher.app_count++];
        app->partition = partition;
        const char *name = desc.project_name;
        const size_t prefix_len = strlen(LAUNCHER_PROJECT_PREFIX);
        if (strncmp(name, LAUNCHER_PROJECT_PREFIX, prefix_len) == 0 && name[prefix_len] != '\0') {
            name += prefix_len;
        }
        strlcpy(app->name, name, sizeof(app->name));
        strlcpy(app->version, desc.version, sizeof(app->version));
        ESP_LOGI(TAG, "Slot %s: %s (%s)", partition->label, desc.project_name, desc.version);
    }
    esp_partition_iterator_release(it);
}

// Last launched app, by slot label, so the list opens on it.
static int load_last_selected(void)
{
    nvs_handle_t handle;
    if (nvs_open(LAUNCHER_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    char label[sizeof(((esp_partition_t *)0)->label)] = {0};
    size_t len = sizeof(label);
    int selected = 0;
    if (nvs_get_str(handle, LAUNCHER_NVS_KEY_LAST, label, &len) == ESP_OK) {
        for (int i = 0; i < s_launcher.app_count; ++i) {
            if (strcmp(s_launcher.apps[i].partition->label, label) == 0) {
                selected = i;
                break;
            }
        }
    }
    nvs_close(handle);
    return selected;
}

static void save_last_selected(const launcher_app_t *app)
{
    nvs_handle_t handle;
    if (nvs_open(LAUNCHER_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (nvs_set_str(handle, LAUNCHER_NVS_KEY_LAST, app->partition->label) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static void select_app(int index)
{
    if (s_launcher.app_count == 0) {
        return;
    }
    s_launcher.selected = index;
    for (int i = 0; i < s_launcher.app_count; ++i) {
        lv_obj_set_style_border_width(s_launcher.buttons[i], i == index ? 4 : 0, LV_PART_MAIN);
    }
    lv_obj_scroll_to_view(s_launcher.buttons[index], LV_ANIM_ON);
}

// Runs one LVGL cycle after launch() so "Abriendo..." is on screen before the reboot.
static void launch_timer_cb(lv_timer_t *timer)
{
    const launcher_app_t *app = lv_timer_get_user_data(timer);
    save_last_selected(app);

    // Verifies the image before switching; the app points the next boot back here.
    esp_err_t err = esp_ota_set_boot_partition(app->partition);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Booting %s from %s", app->name, app->partition->label);
        esp_restart();
    }

    ESP_LOGE(TAG, "Cannot boot %s: %s", app->name, esp_err_to_name(err));
    lv_label_set_text_fmt(s_launcher.status, "No se pudo abrir %s", app->name);
    lv_obj_set_style_text_color(s_launcher.status, lv_color_hex(0xfca5a5), LV_PART_MAIN);
    s_launcher.launching = false;
}

static void launch(int index)
{
    if (s_launcher.launching || index < 0 || index >= s_launcher.app_count) {
        return;
    }
    s_launcher.launching = true;
    select_app(index);
    lv_label_set_text_fmt(s_launcher.status, "Abriendo %s...", s_launcher.apps[index].name);
    lv_obj_set_style_text_color(s_launcher.status, lv_color_hex(0x9ccbd8), LV_PART_MAIN);
    lv_timer_t *timer = lv_timer_create(launch_timer_cb, 30, &s_launcher.apps[index]);
    lv_timer_set_repeat_count(timer, 1);
}

static void app_clicked(lv_event_t *event)
{
    launch((int)(intptr_t)lv_event_get_user_data(event));
}

// Button convention (ESP32Watch-core docs/ARCHITECTURE.md): BOOT = accept (open),
// PWR = next app, since the launcher has nothing to go back to.
static void button_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = lv_tick_get();

    const bool boot = watch_boot_button_is_pressed();
    if (boot != s_launcher.boot_pressed && now - s_launcher.boot_change_ms >= LAUNCHER_BOOT_DEBOUNCE_MS) {
        s_launcher.boot_pressed = boot;
        s_launcher.boot_change_ms = now;
        if (boot) {
            launch(s_launcher.selected);
        }
    }

    // PWR short press comes from the AXP2101 over I2C; polled slower since each poll is a bus transaction.
    if (watch_pwr_key_is_available() && now - s_launcher.pwr_last_poll_ms >= LAUNCHER_PWR_POLL_MS) {
        s_launcher.pwr_last_poll_ms = now;
        bool pressed = false;
        (void)watch_pwr_key_take_short_press(&pressed);
        if (pressed && !s_launcher.launching && s_launcher.app_count > 0) {
            select_app((s_launcher.selected + 1) % s_launcher.app_count);
        }
    }
}

static lv_obj_t *create_app_button(lv_obj_t *parent, int index)
{
    const launcher_app_t *app = &s_launcher.apps[index];

    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 320, 92);
    lv_obj_set_style_radius(button, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(app_color(app->name)), LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0xf8fafc), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(button, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, app_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *name = lv_label_create(button);
    lv_label_set_text(name, app->name);
    lv_obj_set_style_text_color(name, lv_color_hex(0xf8fafc), LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(name, &lv_font_montserrat_28, LV_PART_MAIN);
#endif
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, -12);

    lv_obj_t *version = lv_label_create(button);
    lv_label_set_text(version, app->version);
    lv_label_set_long_mode(version, LV_LABEL_LONG_DOT);
    lv_obj_set_width(version, 280);
    lv_obj_set_style_text_color(version, lv_color_hex(0xcbd5e1), LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(version, &lv_font_montserrat_16, LV_PART_MAIN);
#endif
    lv_obj_align(version, LV_ALIGN_LEFT_MID, 12, 22);
    return button;
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x070b12), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "ESP32Watch");
    lv_obj_set_style_text_color(title, lv_color_hex(0xf8fafc), LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_size(list, 370, 330);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_launcher.app_count; ++i) {
        s_launcher.buttons[i] = create_app_button(list, i);
    }

    s_launcher.status = lv_label_create(screen);
    lv_obj_set_width(s_launcher.status, 360);
    lv_obj_set_style_text_align(s_launcher.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_launcher.status, lv_color_hex(0x94a3b8), LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(s_launcher.status, &lv_font_montserrat_16, LV_PART_MAIN);
#endif
    lv_obj_align(s_launcher.status, LV_ALIGN_BOTTOM_MID, 0, -40);

    if (s_launcher.app_count == 0) {
        lv_label_set_text(s_launcher.status, "No hay apps instaladas.\nGrabalas con flash_all.sh");
        lv_obj_align(s_launcher.status, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    lv_label_set_text(s_launcher.status, "Toca una app\nBOOT abrir  /  PWR siguiente");
    select_app(load_last_selected());
}

esp_err_t launcher_start(void)
{
    esp_err_t err = watch_nvs_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable, last app not remembered: %s", esp_err_to_name(err));
    }

    scan_apps();

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        return ESP_FAIL;
    }
    err = bsp_display_brightness_set(80);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set display brightness: %s", esp_err_to_name(err));
    }

    err = watch_boot_button_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BOOT button unavailable: %s", esp_err_to_name(err));
    }
    err = watch_pwr_key_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PWR key unavailable: %s", esp_err_to_name(err));
    }
    // A BOOT held across the reset must not launch anything until it is released.
    s_launcher.boot_pressed = watch_boot_button_is_pressed();

    if (!bsp_display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    create_ui();
    lv_timer_create(button_timer_cb, LAUNCHER_BUTTON_POLL_MS, NULL);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Launcher ready, %d app(s)", s_launcher.app_count);
    return ESP_OK;
}
