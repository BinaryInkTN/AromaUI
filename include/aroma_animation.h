#ifndef AROMA_ANIMATION_H
#define AROMA_ANIMATION_H

#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    
    struct _AromaAnimation* next;
} AromaAnimation;

void aroma_animation_manager_init(void);
void aroma_animation_manager_shutdown(void);
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