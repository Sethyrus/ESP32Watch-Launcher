#pragma once

// Persistent OS settings and the stopwatch, in NVS namespace "launcher". The stopwatch
// keeps absolute timestamps so it keeps "running" while an app is open (a reboot).

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int brightness;       // percent
    int screen_timeout_s; // screen off after this long without input
} os_settings_t;

void os_store_load(void);
os_settings_t *os_settings(void);
void os_settings_save(void);

// Stopwatch
bool os_stopwatch_running(void);
int64_t os_stopwatch_elapsed_ms(void);
void os_stopwatch_toggle(void);
void os_stopwatch_reset(void);

// Last launched app slot label ("ota_1"), "" if none.
const char *os_last_app(void);
void os_set_last_app(const char *label);
