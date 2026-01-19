#include "core/aroma_timer.h"
#include <string.h>

#define AROMA_MAX_TIMERS 128

struct AromaTimer {
    uint32_t period_ms;
    uint64_t next_fire;
    bool repeat;
    bool active;
    AromaTimerCallback cb;
    void* user_data;
};

static struct AromaTimer g_timers[AROMA_MAX_TIMERS];

void aroma_timer_init(void) {
    memset(g_timers, 0, sizeof(g_timers));
}

void aroma_timer_shutdown(void) {
    memset(g_timers, 0, sizeof(g_timers));
}

AromaTimer* aroma_timer_create(uint32_t period_ms, bool repeat, AromaTimerCallback cb, void* user_data) {
    if (!cb || period_ms == 0) return NULL;
    for (size_t i = 0; i < AROMA_MAX_TIMERS; i++) {
        if (!g_timers[i].active) {
            g_timers[i].period_ms = period_ms;
            g_timers[i].next_fire = 0;
            g_timers[i].repeat = repeat;
            g_timers[i].active = true;
            g_timers[i].cb = cb;
            g_timers[i].user_data = user_data;
            return &g_timers[i];
        }
    }
    return NULL;
}

void aroma_timer_cancel(AromaTimer* timer) {
    if (!timer) return;
    timer->active = false;
}

void aroma_timer_tick(uint64_t now_ms) {
    for (size_t i = 0; i < AROMA_MAX_TIMERS; i++) {
        if (!g_timers[i].active) continue;
        if (g_timers[i].next_fire == 0) {
            g_timers[i].next_fire = now_ms + g_timers[i].period_ms;
        }
        if (now_ms >= g_timers[i].next_fire) {
            g_timers[i].cb(g_timers[i].user_data);
            if (g_timers[i].repeat) {
                g_timers[i].next_fire = now_ms + g_timers[i].period_ms;
            } else {
                g_timers[i].active = false;
            }
        }
    }
}
