file(GLOB FONT_SRCS "fonts/*.c")
idf_component_register(
    SRCS "launcher.c" "os_ui.c" "os_store.c" "os_face.c" "os_menu.c" "os_apps.c" "os_settings.c" "os_tools.c"
         ${FONT_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES watch_board lvgl
    PRIV_REQUIRES app_update esp_partition esp_timer nvs_flash waveshare__esp32_s3_touch_amoled_2_06
)
