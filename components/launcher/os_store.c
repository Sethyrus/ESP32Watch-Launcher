#include "os_store.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_system.h"
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
static bool s_in_app; // the last boot went into an app and has not come back yet
static os_reset_t s_last_reset;
static uint32_t s_reset_count;

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
    if (nvs_get_u8(h, "in_app", &u8) == ESP_OK) {
        s_in_app = u8 != 0;
    }
    nvs_get_u32(h, "rst_n", &s_reset_count);
    len = sizeof(s_last_reset);
    if (nvs_get_blob(h, "rst_last", &s_last_reset, &len) != ESP_OK || len != sizeof(s_last_reset)) {
        memset(&s_last_reset, 0, sizeof(s_last_reset));
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

void os_stopwatch_clock_changed(int64_t delta_ms)
{
    if (s_sw.running && delta_ms != 0) {
        s_sw.start_ms += delta_ms;
        commit(write_stopwatch);
    }
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
    nvs_set_u8(h, "in_app", s_in_app);
}

void os_set_last_app(const char *label)
{
    strlcpy(s_last_app, label, sizeof(s_last_app));
    s_in_app = true;
    commit(write_last);
}

// ---------- unexpected resets ----------

static void write_resets(nvs_handle_t h)
{
    nvs_set_u8(h, "in_app", s_in_app);
    nvs_set_u32(h, "rst_n", s_reset_count);
    nvs_set_blob(h, "rst_last", &s_last_reset, sizeof(s_last_reset));
}

void os_resets_check(void)
{
    // The reset reason survives the switch from an app to this firmware: the app's
    // panic or watchdog shows up here.
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool unexpected = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
                            reason == ESP_RST_WDT || reason == ESP_RST_BROWNOUT;
    if (!unexpected && !s_in_app) {
        return;
    }
    if (unexpected) {
        s_last_reset.time = (uint32_t)time(NULL);
        s_last_reset.reason = (uint8_t)reason;
        strlcpy(s_last_reset.where, s_in_app ? s_last_app : "", sizeof(s_last_reset.where));
        s_reset_count++;
        ESP_LOGW(TAG, "Unexpected reset (reason %d) in %s", reason, s_in_app ? s_last_app : "the launcher");
    }
    s_in_app = false;
    commit(write_resets);
}

uint32_t os_resets_count(void)
{
    return s_reset_count;
}

const os_reset_t *os_reset_last(void)
{
    return s_reset_count > 0 ? &s_last_reset : NULL;
}
