#ifndef AROMA_ANIMATION_H
#define AROMA_ANIMATION_H

#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upper bound (ms of animation time) a single engine tick may advance a
 * mid-flight animation. Larger wall-clock gaps (frame hitch, backgrounding
 * on Android where Choreographer callbacks stop, debugger pause) ease
 * toward completion over successive ticks instead of teleporting.
 * 64ms ~= 4 frames at 60Hz: hitches stay invisible, background gaps visibly
 * catch up instead of jumping. Completion still fires exactly once when
 * wall-clock progress reaches 1. */
#define AROMA_ANIM_MAX_TICK_STEP_MS 64

typedef enum {
    AROMA_ANIM_NONE,
    AROMA_ANIM_SLIDE_X,
    AROMA_ANIM_SLIDE_Y,
    AROMA_ANIM_SCALE_X,
    AROMA_ANIM_SCALE_Y,
    AROMA_ANIM_FADE,
    AROMA_ANIM_CUSTOM
} AromaAnimationType;

typedef enum {
    AROMA_EASE_LINEAR,
    AROMA_EASE_IN_QUAD,
    AROMA_EASE_OUT_QUAD,
    AROMA_EASE_IN_OUT_QUAD,
    AROMA_EASE_OUT_CUBIC,
    AROMA_EASE_OUT_BACK,
    AROMA_EASE_OUT_ELASTIC
} AromaEasingType;

// Loop behavior for aroma_animation_set_loop_mode.
typedef enum {
    AROMA_LOOP_OFF,       // Play once, then complete.
    AROMA_LOOP_RESTART,   // Jump back to start_val each cycle.
    AROMA_LOOP_PINGPONG   // Alternate direction each cycle (smooth, no jump).
} AromaLoopMode;

// Standard frame-by-frame custom update callback
typedef void (*AromaAnimationCallback)(AromaNode* target, float current_val, void* user_data);

// New completion callback triggered once the animation completes naturally
typedef void (*AromaAnimationCompleteCallback)(AromaNode* target, void* user_data);

typedef struct _AromaAnimation {
    AromaNode* target;
    AromaAnimationType type;
    float start_val;
    float end_val;
    float current_val;
    uint32_t duration_ms;
    uint64_t start_time;
    bool is_running;
    void* timer;
    AromaEasingType easing;
    AromaAnimationCallback custom_cb;
    void* user_data;
    
    // Pointer to completion callback
    AromaAnimationCompleteCallback on_complete;

    // Loop mode (AROMA_LOOP_OFF by default). Restart jumps back to
    // start_val each cycle; ping-pong alternates direction for a smooth,
    // seamless loop. Completion callbacks do not fire while looping.
    AromaLoopMode loop_mode;

    // Last rendered progress (0..1). Used to clamp per-tick advances so a
    // stalled frame cannot teleport the animation past intermediate states.
    float last_progress;

    // When true, start_val/end_val are parent-relative (Incense-authored
    // dp positions, e.g. x: 24 with animation_end_val: 276). The engine
    // adds the parent's current position each tick so the final rect
    // lands on parent_abs + end_val (absolute content coordinates).
    // Programmatic animations that pass absolute rect values (e.g.
    // rect->x as start) leave this false and are written as-is.
    bool parent_relative;
    
    struct _AromaAnimation* next;
} AromaAnimation;

void aroma_animation_manager_init(void);
bool aroma_animation_is_running_on(AromaNode *target);void aroma_animation_manager_shutdown(void);

/* Advance all running animations to the given wall-clock timestamp (ms,
 * same clock domain as aroma_time_now_ms()).
 *
 * Platform contract:
 * - Linux (GLPS/GLFW): driven by the 16ms engine timer, which is pumped
 *   by aroma_timer_tick() from aroma_ui_process_events() each main-loop
 *   iteration. No extra integration needed.
 * - Android: driven by the same engine timer, pumped from the
 *   Choreographer vsync callback. aroma_animation_start*() requests a
 *   vsync frame so an animation started while idle still ticks.
 *
 * Large timestamp jumps (hitch, debugger, backgrounding) never teleport
 * an animation: mid-flight progress advances at most
 * AROMA_ANIM_MAX_TICK_STEP_MS worth per tick and eases toward the end.
 * Use pause/resume across known gaps (Android window loss) to freeze
 * animations instead of consuming their duration while hidden. */
void aroma_animation_tick(uint64_t now_ms);

/* Freeze all running animations (e.g. Android APP_CMD_TERM_WINDOW /
 * APP_CMD_LOST_FOCUS). Ticks become no-ops until resume. Resume shifts
 * every running animation's start_time forward by the paused duration,
 * so wall-clock progress continues exactly where it visually stopped
 * instead of jumping to the end. Safe to call redundantly; NULL-safe
 * by design (no arguments). */
void aroma_animation_pause_all(void);
void aroma_animation_resume_all(void);
bool aroma_animation_is_paused(void);
AromaAnimation* aroma_animation_start(AromaNode* target, AromaAnimationType type, float start_val, float end_val, uint32_t duration_ms);
void aroma_animation_stop(AromaNode* target);
AromaAnimation* aroma_animation_start_custom(AromaNode* target, float start_val, float end_val, uint32_t duration_ms, AromaAnimationCallback cb, void* user_data);
void aroma_animation_set_easing(AromaAnimation* anim, AromaEasingType easing);

void aroma_animation_set_on_complete(AromaAnimation* anim, AromaAnimationCompleteCallback cb);

// Enable or disable looping. A looping animation restarts from start_val
// every time it reaches end_val (zero-duration animations never loop).
// Completion callbacks do not fire while looping is enabled.
void aroma_animation_set_loop(AromaAnimation* anim, bool loop);

// Select the loop behavior explicitly: restart jumps back to start_val,
// ping-pong reverses direction each cycle for a seamless loop.
void aroma_animation_set_loop_mode(AromaAnimation* anim, AromaLoopMode mode);

void aroma_animation_cleanup_node(AromaNode* target);
void aroma_animation_cleanup_all(void);

#ifdef __cplusplus
}
#endif

#endif