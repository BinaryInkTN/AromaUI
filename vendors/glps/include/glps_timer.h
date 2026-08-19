#ifndef GLPS_TIMER_H
#define GLPS_TIMER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Callback function type for timers.
 * 
 * @param arg Pointer to user-defined argument passed when timer expires.
 */
typedef void (*timer_callback)(void *arg);

/**
 * @brief Represents a high-resolution timer in GLPS.
 */
typedef struct glps_timer {
  uint64_t start_time_ms;
  uint64_t duration_ms;
  uint64_t end_time_ms;
  timer_callback callback;
  void *callback_arg;
  bool is_valid;
} glps_timer;
/**
 * @brief Initializes a new timer.
 * 
 * @return Pointer to a newly allocated glps_timer structure, or NULL on failure.
 */
glps_timer *glps_timer_init(void);

/**
 * @brief Starts a timer with a specified duration and callback.
 * 
 * @param timer Pointer to the glps_timer instance.
 * @param duration_ms Duration of the timer in milliseconds.
 * @param callback Function to call when the timer expires.
 * @param arg Argument to pass to the callback function.
 */
void glps_timer_start(glps_timer *timer, uint64_t duration_ms, timer_callback callback, void *arg);

/**
 * @brief Stops a running timer.
 * 
 * @param timer Pointer to the glps_timer instance.
 */
void glps_timer_stop(glps_timer *timer);

/**
 * @brief Returns the elapsed time since the timer was started in milliseconds.
 * 
 * @param timer Pointer to the glps_timer instance.
 * @return Elapsed time in milliseconds.
 */
double glps_timer_elapsed_ms(glps_timer *timer);

/**
 * @brief Returns the elapsed time since the timer was started in microseconds.
 * 
 * @param timer Pointer to the glps_timer instance.
 * @return Elapsed time in microseconds.
 */
double glps_timer_elapsed_us(glps_timer *timer);

/**
 * @brief Destroys a timer and frees associated resources.
 * 
 * @param timer Pointer to the glps_timer instance.
 */
void glps_timer_destroy(glps_timer *timer);

/**
 * @brief Checks if the timer has expired and calls the callback if it has.
 * 
 * @param timer Pointer to the glps_timer instance.
 */
void glps_timer_check_and_call(glps_timer *timer);

#endif // GLPS_TIMER_H
