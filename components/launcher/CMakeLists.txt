idf_component_register(
    SRCS "launcher.c"
    INCLUDE_DIRS "include"
    REQUIRES watch_board lvgl
    PRIV_REQUIRES app_update esp_partition nvs_flash
)
