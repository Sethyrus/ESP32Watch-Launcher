#include "os_store.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "nvs.h"

#define NS "launcher"

static const char *TAG = "os_store";

static os_settings_t s_settings = {.brightness = 80, .screen_timeout_s = 15};
static struct {
    bool running;
    int64_t start_ms; // wall-clock ms when (re)started
    int64_t acc_ms;   // accumulated before the last start
} s_sw;
static char s_last_app[17];

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void os_store_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    if (nvs_get_u8(h, "bright", &u8) == ESP_OK && u8 >= 10 && u8 <= 100) {
        s_settings.brightness = u8;
    }
    if (nvs_get_u16(h, "timeout", &u16) == ESP_OK && u16 >= 5 && u16 <= 600) {
        s_settings.screen_timeout_s = u16;
    }
    if (nvs_get_u8(h, "sw_run", &u8) == ESP_OK) {
        s_sw.running = u8 != 0;
    }
    nvs_get_i64(h, "sw_start", &s_sw.start_ms);
    nvs_get_i64(h, "sw_acc", &s_sw.acc_ms);
    size_t len = sizeof(s_last_app);
    if (nvs_get_str(h, "last", s_last_app, &len) != ESP_OK) {
        s_last_app[0] = '\0';
    }
    nvs_close(h);
}

os_settings_t *os_settings(void)
{
    return &s_settings;
}

static void commit(void (*write)(nvs_handle_t))
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS not writable");
        return;
    }
    write(h);
    nvs_commit(h);
    nvs_close(h);
}

static void write_settings(nvs_handle_t h)
{
    nvs_set_u8(h, "bright", (uint8_t)s_settings.brightness);
    nvs_set_u16(h, "timeout", (uint16_t)s_settings.screen_timeout_s);
}

void os_settings_save(void)
{
    commit(write_settings);
}

static void write_stopwatch(nvs_handle_t h)
{
    nvs_set_u8(h, "sw_run", s_sw.running);
    nvs_set_i64(h, "sw_start", s_sw.start_ms);
    nvs_set_i64(h, "sw_acc", s_sw.acc_ms);
}

bool os_stopwatch_running(void)
{
    return s_sw.running;
}

int64_t os_stopwatch_elapsed_ms(void)
{
    int64_t ms = s_sw.acc_ms;
    if (s_sw.running) {
        const int64_t run = now_ms() - s_sw.start_ms;
        ms += run > 0 ? run : 0; // the clock may have been set backwards
    }
    return ms;
}

void os_stopwatch_toggle(void)
{
    if (s_sw.running) {
        s_sw.acc_ms = os_stopwatch_elapsed_ms();
        s_sw.running = false;
    } else {
        s_sw.start_ms = now_ms();
        s_sw.running = true;
    }
    commit(write_stopwatch);
}

void os_stopwatch_reset(void)
{
    s_sw.running = false;
    s_sw.acc_ms = 0;
    s_sw.start_ms = 0;
    commit(write_stopwatch);
}

const char *os_last_app(void)
{
    return s_last_app;
}

static void write_last(nvs_handle_t h)
{
    nvs_set_str(h, "last", s_last_app);
}

void os_set_last_app(const char *label)
{
    strlcpy(s_last_app, label, sizeof(s_last_app));
    commit(write_last);
}
