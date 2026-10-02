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
// The wall clock was stepped by delta_ms (time set by hand): a running stopwatch keeps
// its elapsed time.
void os_stopwatch_clock_changed(int64_t delta_ms);

// Last launched app slot label ("ota_1"), "" if none. Setting it also marks "inside an
// app" until the next launcher boot, so a crash there is blamed on that app.
const char *os_last_app(void);
void os_set_last_app(const char *label);

// Unexpected resets (panic, watchdog, brownout), seen from the launcher's next boot:
// on battery the console is gone, so this is the only trace. Call once per boot, after
// the RTC has set the clock.
typedef struct {
    uint32_t time;  // Unix time of the boot that saw it (0 = unknown)
    uint8_t reason; // esp_reset_reason_t
    char where[17]; // app slot label, "" = the launcher itself
} os_reset_t;

void os_resets_check(void);
uint32_t os_resets_count(void);        // since the log started
const os_reset_t *os_reset_last(void); // NULL if none
