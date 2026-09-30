#include "launcher.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "os_screens.h"
#include "os_store.h"
#include "watch_battery.h"
#include "watch_buttons.h"
#include "watch_display.h"
#include "watch_nvs.h"
#include "watch_power.h"
#include "watch_rtc.h"

static const char *TAG = "launcher";

esp_err_t launcher_start(void)
{
    esp_err_t err = watch_nvs_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable, settings not kept: %s", esp_err_to_name(err));
    }
    os_store_load();

    // An app may have left the IMU or the speaker amp on across the reboot.
    watch_power_quiet_peripherals();

    watch_display_config_t display = WATCH_DISPLAY_CONFIG_DEFAULT();
    display.brightness = os_settings()->brightness;
    if (watch_display_start(&display) == NULL) {
        return ESP_FAIL;
    }

    if ((err = watch_boot_button_init()) != ESP_OK) {
        ESP_LOGW(TAG, "BOOT button unavailable: %s", esp_err_to_name(err));
    }
    if ((err = watch_pwr_key_init()) != ESP_OK) {
        ESP_LOGW(TAG, "PWR key unavailable: %s", esp_err_to_name(err));
    }
    if ((err = watch_battery_init()) != ESP_OK) {
        ESP_LOGW(TAG, "Battery unavailable: %s", esp_err_to_name(err));
    }
    if ((err = watch_rtc_init(false)) != ESP_OK) {
        ESP_LOGW(TAG, "RTC unavailable: %s", esp_err_to_name(err));
    }
    os_apps_scan();

    if (!bsp_display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    os_start(&os_face_screen);
    if (watch_rtc_time_was_lost()) {
        os_push(&os_time_screen); // the RTC lost power: ask for the time first
    }
    bsp_display_unlock();

    ESP_LOGI(TAG, "Watch OS ready, %d app(s)", os_apps_count());
    return ESP_OK;
}
