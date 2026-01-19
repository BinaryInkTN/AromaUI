#ifndef AROMA_TIMER_H
#define AROMA_TIMER_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*AromaTimerCallback)(void* user_data);

typedef struct AromaTimer AromaTimer;

void aroma_timer_init(void);
void aroma_timer_shutdown(void);
AromaTimer* aroma_timer_create(uint32_t period_ms, bool repeat, AromaTimerCallback cb, void* user_data);
void aroma_timer_cancel(AromaTimer* timer);
void aroma_timer_tick(uint64_t now_ms);

#endif
