#include "esp_err.h"
#include "esp_log.h"

#include "launcher.h"

static const char *TAG = "ESP32WatchLauncher";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting launcher");

    esp_err_t err = launcher_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start launcher: %s", esp_err_to_name(err));
    }
}
