#include "os_alerts.h"

#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define NS "launcher"
#define MISSED_AFTER_MS 120000 // due this long ago: not worth a sound
#define OUTPUT_MAX_MS 60000
#define MOTOR_GPIO GPIO_NUM_18 // from the schematic, not validated on the board
#define BEEP_RATE 16000
#define BEEP_HZ 1000
#define BEEP_MS 180

static const char *TAG = "os_alerts";

static os_alarm_t s_alarms[OS_ALARM_COUNT];
static struct {
    os_timer_state_t state;
    int64_t end_ms;       // running: wall-clock end
    int64_t remaining_ms; // paused: what was left
    int total_s;
} s_timer = {.total_s = 300};

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// ---------- persistence ----------

static void save_alarms(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "alarms", s_alarms, sizeof(s_alarms));
    nvs_commit(h);
    nvs_close(h);
}

static void save_timer(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "tmr_state", (uint8_t)s_timer.state);
    nvs_set_i64(h, "tmr_end", s_timer.end_ms);
    nvs_set_i64(h, "tmr_rem", s_timer.remaining_ms);
    nvs_set_u32(h, "tmr_tot", (uint32_t)s_timer.total_s);
    nvs_commit(h);
    nvs_close(h);
}

void os_alerts_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_alarms);
    os_alarm_t loaded[OS_ALARM_COUNT];
    if (nvs_get_blob(h, "alarms", loaded, &len) == ESP_OK && len == sizeof(loaded)) {
        memcpy(s_alarms, loaded, sizeof(s_alarms));
    }
    uint8_t state = 0;
    uint32_t total = 0;
    if (nvs_get_u8(h, "tmr_state", &state) == ESP_OK && state <= OS_TIMER_PAUSED) {
        s_timer.state = (os_timer_state_t)state;
    }
    nvs_get_i64(h, "tmr_end", &s_timer.end_ms);
    nvs_get_i64(h, "tmr_rem", &s_timer.remaining_ms);
    if (nvs_get_u32(h, "tmr_tot", &total) == ESP_OK && total >= 1 && total <= 99 * 60 + 59) {
        s_timer.total_s = (int)total;
    }
    nvs_close(h);
}

// ---------- alarms ----------

// First hour:minute strictly after now.
static int64_t next_occurrence(int hour, int minute)
{
    const time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    time_t t = mktime(&tm);
    if (t <= now) {
        tm.tm_mday += 1;
        t = mktime(&tm);
    }
    return (int64_t)t * 1000;
}

const os_alarm_t *os_alarm_get(int index)
{
    return &s_alarms[index];
}

void os_alarm_set(int index, bool enabled, bool repeat, int hour, int minute)
{
    os_alarm_t *a = &s_alarms[index];
    a->enabled = enabled;
    a->repeat = repeat;
    a->hour = (uint8_t)hour;
    a->minute = (uint8_t)minute;
    a->next_ms = enabled ? next_occurrence(hour, minute) : 0;
    save_alarms();
}

void os_alarms_reschedule(void)
{
    for (int i = 0; i < OS_ALARM_COUNT; i++) {
        if (s_alarms[i].enabled) {
            s_alarms[i].next_ms = next_occurrence(s_alarms[i].hour, s_alarms[i].minute);
        }
    }
    save_alarms();
}

bool os_alarm_next(int *hour, int *minute)
{
    int best = -1;
    for (int i = 0; i < OS_ALARM_COUNT; i++) {
        if (s_alarms[i].enabled && (best < 0 || s_alarms[i].next_ms < s_alarms[best].next_ms)) {
            best = i;
        }
    }
    if (best < 0) {
        return false;
    }
    *hour = s_alarms[best].hour;
    *minute = s_alarms[best].minute;
    return true;
}

// ---------- timer ----------

os_timer_state_t os_timer_state(void)
{
    return s_timer.state;
}

int64_t os_timer_remaining_ms(void)
{
    if (s_timer.state == OS_TIMER_RUNNING) {
        const int64_t left = s_timer.end_ms - now_ms();
        return left > 0 ? left : 0;
    }
    return s_timer.state == OS_TIMER_PAUSED ? s_timer.remaining_ms : 0;
}

int os_timer_total_s(void)
{
    return s_timer.total_s;
}

void os_timer_start(int seconds)
{
    s_timer.total_s = seconds;
    s_timer.end_ms = now_ms() + (int64_t)seconds * 1000;
    s_timer.state = OS_TIMER_RUNNING;
    save_timer();
}

void os_timer_pause(void)
{
    s_timer.remaining_ms = os_timer_remaining_ms();
    s_timer.state = OS_TIMER_PAUSED;
    save_timer();
}

void os_timer_resume(void)
{
    s_timer.end_ms = now_ms() + s_timer.remaining_ms;
    s_timer.state = OS_TIMER_RUNNING;
    save_timer();
}

void os_timer_reset(void)
{
    s_timer.state = OS_TIMER_IDLE;
    save_timer();
}

// ---------- firing ----------

bool os_alerts_poll(os_alert_t *out)
{
    const int64_t now = now_ms();
    if (s_timer.state == OS_TIMER_RUNNING && now >= s_timer.end_ms) {
        out->kind = OS_ALERT_TIMER;
        out->index = 0;
        out->missed = now - s_timer.end_ms > MISSED_AFTER_MS;
        s_timer.state = OS_TIMER_IDLE;
        save_timer();
        return true;
    }
    for (int i = 0; i < OS_ALARM_COUNT; i++) {
        os_alarm_t *a = &s_alarms[i];
        if (!a->enabled || now < a->next_ms) {
            continue;
        }
        out->kind = OS_ALERT_ALARM;
        out->index = i;
        out->missed = now - a->next_ms > MISSED_AFTER_MS;
        if (a->repeat) {
            a->next_ms = next_occurrence(a->hour, a->minute);
        } else {
            a->enabled = false;
            a->next_ms = 0;
        }
        save_alarms();
        return true;
    }
    return false;
}

int64_t os_alerts_next_in_ms(void)
{
    int64_t due = INT64_MAX;
    if (s_timer.state == OS_TIMER_RUNNING) {
        due = s_timer.end_ms;
    }
    for (int i = 0; i < OS_ALARM_COUNT; i++) {
        if (s_alarms[i].enabled && s_alarms[i].next_ms < due) {
            due = s_alarms[i].next_ms;
        }
    }
    if (due == INT64_MAX) {
        return -1;
    }
    const int64_t left = due - now_ms();
    return left > 0 ? left : 0;
}

// ---------- output ----------

static TaskHandle_t s_out_task;
static volatile bool s_out_active;

static void beep_pattern(esp_codec_dev_handle_t speaker, int16_t *beep, size_t bytes)
{
    // Two beeps, then a pause, with a motor pulse on each beep.
    for (int i = 0; i < 2 && s_out_active; i++) {
        gpio_set_level(MOTOR_GPIO, 1);
        if (speaker != NULL) {
            esp_codec_dev_write(speaker, beep, (int)bytes);
        } else {
            vTaskDelay(pdMS_TO_TICKS(BEEP_MS));
        }
        gpio_set_level(MOTOR_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    for (int i = 0; i < 6 && s_out_active; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void output_task(void *arg)
{
    const size_t samples = BEEP_RATE * BEEP_MS / 1000;
    int16_t *beep = malloc(samples * sizeof(int16_t));
    if (beep != NULL) {
        for (size_t i = 0; i < samples; i++) {
            // short fade in/out against clicks
            const float edge = fminf(1.0f, fminf(i, samples - 1 - i) / 160.0f);
            beep[i] = (int16_t)(12000.0f * edge * sinf(2.0f * (float)M_PI * BEEP_HZ * i / BEEP_RATE));
        }
    }
    gpio_reset_pin(MOTOR_GPIO);
    gpio_set_direction(MOTOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MOTOR_GPIO, 0);
    esp_codec_dev_handle_t speaker = NULL;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (speaker == NULL && beep != NULL) {
            speaker = bsp_audio_codec_speaker_init();
            if (speaker == NULL) {
                ESP_LOGW(TAG, "Speaker not available: vibration only");
            }
        }
        bool open = false;
        if (speaker != NULL) {
            esp_codec_dev_sample_info_t fs = {
                .sample_rate = BEEP_RATE, .channel = 1, .bits_per_sample = 16};
            esp_codec_dev_set_out_vol(speaker, 80);
            open = esp_codec_dev_open(speaker, &fs) == ESP_CODEC_DEV_OK;
            if (!open) {
                ESP_LOGW(TAG, "Speaker did not open: vibration only");
            }
        }
        const int64_t start = esp_timer_get_time();
        while (s_out_active && (esp_timer_get_time() - start) / 1000 < OUTPUT_MAX_MS) {
            beep_pattern(open ? speaker : NULL, beep, samples * sizeof(int16_t));
        }
        if (open) {
            esp_codec_dev_close(speaker);
        }
        gpio_set_level(MOTOR_GPIO, 0);
        s_out_active = false;
    }
}

void os_alert_output_start(void)
{
    if (s_out_task == NULL) {
        xTaskCreate(output_task, "os_alert", 4096, NULL, 2, &s_out_task);
    }
    s_out_active = true;
    xTaskNotifyGive(s_out_task);
}

void os_alert_output_stop(void)
{
    s_out_active = false;
}

bool os_alert_output_active(void)
{
    return s_out_active;
}
