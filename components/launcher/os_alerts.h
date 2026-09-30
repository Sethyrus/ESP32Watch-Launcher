#pragma once

// Timer and alarms. Both keep an absolute wall-clock instant in NVS (namespace
// "launcher"), so they keep "running" while an app is open or the chip sleeps; the
// system task polls them and the watch sleeps only until the next one is due.

#include <stdbool.h>
#include <stdint.h>

#define OS_ALARM_COUNT 4

typedef struct {
    bool enabled;
    bool repeat; // every day; otherwise it disables itself after ringing
    uint8_t hour;
    uint8_t minute;
    int64_t next_ms; // wall-clock instant of the next ring (enabled only)
} os_alarm_t;

typedef enum { OS_TIMER_IDLE, OS_TIMER_RUNNING, OS_TIMER_PAUSED } os_timer_state_t;

typedef enum { OS_ALERT_TIMER, OS_ALERT_ALARM } os_alert_kind_t;

typedef struct {
    os_alert_kind_t kind;
    int index;   // alarm slot
    bool missed; // it was due long ago (an app was open): show, but no sound
} os_alert_t;

void os_alerts_load(void);

// ---- Alarms ----
const os_alarm_t *os_alarm_get(int index);
// Stores the alarm; an enabled one gets its next_ms computed from now.
void os_alarm_set(int index, bool enabled, bool repeat, int hour, int minute);
// After the clock was set: recompute every enabled alarm.
void os_alarms_reschedule(void);
// Earliest enabled alarm; false if none.
bool os_alarm_next(int *hour, int *minute);

// ---- Timer ----
os_timer_state_t os_timer_state(void);
int64_t os_timer_remaining_ms(void);
int os_timer_total_s(void); // last duration set, to preset the roller
void os_timer_start(int seconds);
void os_timer_pause(void);
void os_timer_resume(void);
void os_timer_reset(void);

// ---- Firing ----
// True once per due timer/alarm, after updating their state. LVGL lock not needed.
bool os_alerts_poll(os_alert_t *out);
// Milliseconds until the next timer/alarm is due, -1 if none.
int64_t os_alerts_next_in_ms(void);

// ---- Output (vibration motor pulses and speaker beeps, up to 60 s) ----
void os_alert_output_start(void);
void os_alert_output_stop(void);
bool os_alert_output_active(void);
