// Conectar al ordenador: the microSD appears as a USB disk on the computer (TinyUSB
// mass storage). The chip has one USB port, used until now by USB-Serial-JTAG (console
// and flashing), so those stop while this is on; leaving reboots to get them back.
#include <stdlib.h>

#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_system.h"
#include "os_screens.h"
#include "sdmmc_cmd.h"
#include "soc/rtc_cntl_struct.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"

static const char *TAG = "os_usb";

static struct {
    bool active; // TinyUSB owns the port: leaving must reboot
    sdmmc_card_t *card;
    tinyusb_msc_storage_handle_t storage;
    lv_obj_t *icon;
    lv_obj_t *status;
    lv_obj_t *note;
    lv_timer_t *timer;       // 300 ms poll of the host connection
    lv_timer_t *start_timer; // pending one-shot start_cb
    bool shown_mounted;
} s_usb;

// Card on the BSP's SDMMC 1-bit pins, without mounting FAT here: the computer owns it.
static esp_err_t card_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = BSP_SD_CLK;
    slot.cmd = BSP_SD_CMD;
    slot.d0 = BSP_SD_D0;
    slot.width = 1;
    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        return err;
    }
    if ((err = sdmmc_host_init_slot(host.slot, &slot)) != ESP_OK) {
        sdmmc_host_deinit();
        return err;
    }
    s_usb.card = calloc(1, sizeof(sdmmc_card_t));
    if (s_usb.card == NULL) {
        sdmmc_host_deinit();
        return ESP_ERR_NO_MEM;
    }
    if ((err = sdmmc_card_init(&host, s_usb.card)) != ESP_OK) {
        free(s_usb.card);
        s_usb.card = NULL;
        sdmmc_host_deinit();
    }
    return err;
}

static esp_err_t usb_start(void)
{
    esp_err_t err = card_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No microSD: %s", esp_err_to_name(err));
        return err;
    }
    const tinyusb_msc_storage_config_t storage = {
        .medium.card = s_usb.card,
        .fat_fs = {.config.max_files = 2, .do_not_format = true},
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    if ((err = tinyusb_msc_new_storage_sdmmc(&storage, &s_usb.storage)) != ESP_OK) {
        ESP_LOGE(TAG, "MSC storage: %s", esp_err_to_name(err));
        return err;
    }
    const tinyusb_config_t tusb = TINYUSB_DEFAULT_CONFIG(); // default MSC descriptors
    if ((err = tinyusb_driver_install(&tusb)) != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB: %s", esp_err_to_name(err));
        tinyusb_msc_delete_storage(s_usb.storage);
        return err;
    }
    s_usb.active = true;
    ESP_LOGI(TAG, "USB disk on");
    return ESP_OK;
}

void os_usb_restore_port(void)
{
    // The PHY mux is in the RTC domain: a software reset keeps it on USB-OTG, and the
    // console never comes back. Give it to USB-Serial-JTAG under hardware control.
    RTCCNTL.usb_conf.sw_usb_phy_sel = 0;
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
}

// Hands the port back to USB-Serial-JTAG through a reboot (the launcher starts again).
static void usb_stop_and_restart(void)
{
    tinyusb_driver_uninstall();
    tinyusb_msc_delete_storage(s_usb.storage);
    os_usb_restore_port();
    esp_restart();
}

static void show_state(bool mounted)
{
    s_usb.shown_mounted = mounted;
    lv_obj_set_style_text_color(s_usb.icon, lv_color_hex(mounted ? OS_ACCENT : OS_MUTED), 0);
    lv_label_set_text(s_usb.status, mounted ? "Conectado" : "Conecta el cable");
    lv_label_set_text(s_usb.note, mounted ? "La microSD aparece como disco.\nExpúlsalo en el ordenador antes de salir."
                                          : "Al ordenador, por el puerto USB del reloj.");
}

static void poll_cb(lv_timer_t *t)
{
    const bool mounted = tud_mounted();
    if (mounted != s_usb.shown_mounted) {
        show_state(mounted);
    }
}

// One LVGL cycle after the screen shows, so "Preparando..." is visible while it starts.
static void start_cb(lv_timer_t *t)
{
    s_usb.start_timer = NULL; // one-shot: LVGL deletes it after this call
    if (usb_start() == ESP_OK) {
        show_state(false);
        s_usb.timer = lv_timer_create(poll_cb, 300, NULL);
    } else {
        lv_obj_set_style_text_color(s_usb.icon, lv_color_hex(OS_WARN), 0);
        lv_label_set_text(s_usb.status, "Sin microSD");
        lv_label_set_text(s_usb.note, "Inserta una tarjeta y vuelve a entrar.");
    }
}

static void exit_cb(lv_timer_t *t)
{
    usb_stop_and_restart();
}

static void leave(void)
{
    if (!s_usb.active) {
        os_back();
        return;
    }
    lv_label_set_text(s_usb.status, "Saliendo...");
    lv_label_set_text(s_usb.note, "");
    lv_timer_set_repeat_count(lv_timer_create(exit_cb, 50, NULL), 1);
}

static void exit_clicked(lv_event_t *e)
{
    leave();
}

static bool usb_pwr(void)
{
    leave();
    return true;
}

static void usb_create(lv_obj_t *root)
{
    os_title(root, "USB");
    s_usb.icon = os_label(root, &font_icons_32, OS_MUTED, OS_ICON_USB);
    lv_obj_align(s_usb.icon, LV_ALIGN_CENTER, 0, -120);
    s_usb.status = os_label(root, &font_title_30, OS_TEXT, "Preparando...");
    lv_obj_align(s_usb.status, LV_ALIGN_CENTER, 0, -64);
    s_usb.note = os_label(root, &font_barlow_16, OS_MUTED, "");
    lv_obj_set_style_text_align(s_usb.note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_usb.note, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *button = lv_button_create(root);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 150, 50);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(OS_SURFACE), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(OS_BORDER), 0);
    os_style_focus_ring(button, 3);
    lv_obj_add_event_cb(button, exit_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_center(os_label(button, &font_barlow_semibold_22, OS_TEXT, "Salir"));
    os_focus_add(button);
    os_focus_set(0);
    os_hint(root, "BOOT o PWR salir");

    if (s_usb.active) { // back here after an alert: the disk is still on
        show_state(tud_mounted());
        s_usb.timer = lv_timer_create(poll_cb, 300, NULL);
    } else {
        s_usb.start_timer = lv_timer_create(start_cb, 30, NULL);
        lv_timer_set_repeat_count(s_usb.start_timer, 1);
    }
}

static void usb_destroy(void)
{
    if (s_usb.start_timer != NULL) { // left (or an alert took over) before USB started
        lv_timer_delete(s_usb.start_timer);
        s_usb.start_timer = NULL;
    }
    if (s_usb.timer != NULL) {
        lv_timer_delete(s_usb.timer);
        s_usb.timer = NULL;
    }
}

const os_screen_t os_usb_screen = {
    .name = "usb",
    .create = usb_create,
    .destroy = usb_destroy,
    .on_pwr = usb_pwr,
    .keep_awake = true, // light sleep would drop the USB connection
};
