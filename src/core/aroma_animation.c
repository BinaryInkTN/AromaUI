#include "aroma_animation.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "aroma_node.h"
#include "aroma_common.h"
#include "aroma_ui.h"
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static AromaAnimation* animation_list = NULL;
static AromaTimer*     anim_timer     = NULL;

/* While >0 we are inside aroma_animation_tick(). User callbacks
 * (custom_cb, on_complete) may stop/clean up animations or start new ones;
 * freeing list nodes synchronously would pull the node the tick loop is
 * standing on (or its saved `next`) out from under it. Cleanup entry
 * points therefore only mark nodes dead while a tick is in flight and the
 * tick loop sweeps them at a safe point. */
static int  s_tick_depth     = 0;
static bool s_deferred_sweep = false;

/* Pause state: while paused, ticks are no-ops and resume shifts every
 * running animation's start_time forward by the paused wall-clock span. */
static bool     s_paused      = false;
static uint64_t s_pause_begin = 0;


static float apply_easing(AromaEasingType easing, float t)
{
    switch (easing) {
        case AROMA_EASE_LINEAR:       return t;
        case AROMA_EASE_IN_QUAD:      return t * t;
        case AROMA_EASE_OUT_QUAD:     return t * (2.0f - t);
        case AROMA_EASE_IN_OUT_QUAD:  return t < 0.5f
                                           ? 2.0f * t * t
                                           : -1.0f + (4.0f - 2.0f * t) * t;
        case AROMA_EASE_OUT_CUBIC:    return 1.0f - powf(1.0f - t, 3.0f);
        case AROMA_EASE_OUT_BACK: {
            const float c1 = 1.70158f, c3 = c1 + 1.0f, p = t - 1.0f;
            return 1.0f + c3 * powf(p, 3.0f) + c1 * powf(p, 2.0f);
        }
        case AROMA_EASE_OUT_ELASTIC: {
            const float c4 = (2.0f * (float)M_PI) / 3.0f;
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return powf(2.0f, -10.0f * t)
                   * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        default: return 1.0f - powf(1.0f - t, 3.0f);
    }
}


static void update_animations(void* arg)
{
    (void)arg;
    aroma_animation_tick(aroma_time_now_ms());
}

void aroma_animation_tick(uint64_t now)
{
    /* Frozen while paused (Android background / lost surface): timers are
     * still pumped by the platform, but animation time must not advance. */
    if (s_paused)
        return;

    if (!animation_list) return;

    s_tick_depth++;
    bool needs_redraw  = false;
    bool still_running = false;

    AromaAnimation* curr = animation_list;
    AromaAnimation* prev = NULL;

    while (curr) {

        if (!curr->is_running) {
            if (prev) prev->next = curr->next;
            else      animation_list = curr->next;
            AromaAnimation* dead = curr;
            curr = curr->next;
            free(dead);
            continue;
        }

        float raw_progress = (curr->duration_ms > 0)
            ? (float)((int64_t)now - (int64_t)curr->start_time) / (float)curr->duration_ms
            : 1.0f;

        bool finished = (raw_progress >= 1.0f);
        float progress = finished ? 1.0f : raw_progress;

        /* Clamp per-tick advance so a stalled frame cannot teleport the
         * animation past intermediate states. After a hitch the animation
         * eases through the missed interval over successive ticks instead
         * of jumping, so transitions keep their visual pacing on 60/90/120Hz
         * panels and after Android background gaps. Completion still fires
         * exactly once when wall-clock progress reaches 1. */
        if (!finished && curr->duration_ms > 0) {
            float max_step = (float)AROMA_ANIM_MAX_TICK_STEP_MS / (float)curr->duration_ms;
            float allowed  = curr->last_progress + max_step;
            if (progress > allowed)
                progress = allowed;
        }

        /* Keep animation time locked to the wall clock: after a hitch the
           next tick jumps to the correct position instead of replaying
           missed time in slow motion, so transitions always take
           duration_ms and never linger half-finished. */
        curr->last_progress = progress;

        float ease        = apply_easing(curr->easing, progress);
        curr->current_val = curr->start_val
                          + (curr->end_val - curr->start_val) * ease;

        AromaRect* rect = aroma_node_get_rect(curr->target);
        if (rect) {
            /* Round to nearest px so the final tick lands exactly on the
             * rounded dp->px end position. Truncation ((int)val) sits up
             * to 1px low whenever density scaling yields a fractional
             * pixel, which is most visible on low-dpi (<1.0) screens.
             * Incense slide values are parent-relative: add the parent's
             * current position each tick so layout shifts (viewer at
             * y=76, responsive moves, scroll containers) don't leave the
             * widget parent_abs too high (e.g. theming button 276
             * landing on dropdown 196). Programmatic absolute
             * animations (parent_relative=false) are written as-is. */
            int parent_x = 0, parent_y = 0;
            if (curr->parent_relative && curr->target && curr->target->parent_node) {
                AromaRect* pr = aroma_node_get_rect(curr->target->parent_node);
                if (pr) {
                    parent_x = pr->x;
                    parent_y = pr->y;
                }
            }
            switch (curr->type) {
                case AROMA_ANIM_SLIDE_X:  rect->x      = parent_x + (int)roundf(curr->current_val); break;
                case AROMA_ANIM_SLIDE_Y:  rect->y      = parent_y + (int)roundf(curr->current_val); break;
                case AROMA_ANIM_SCALE_X:  rect->width  = (int)roundf(curr->current_val); break;
                case AROMA_ANIM_SCALE_Y:  rect->height = (int)roundf(curr->current_val); break;
                default: break;
            }
            if ((curr->type == AROMA_ANIM_SLIDE_X || curr->type == AROMA_ANIM_SCALE_X) &&
                curr->target)
                aroma_layout_note_placed(curr->target);
        }

        if (curr->type == AROMA_ANIM_FADE && curr->target) {
            curr->target->opacity = curr->current_val;
        }

        if (curr->type == AROMA_ANIM_CUSTOM && curr->custom_cb) {
            curr->custom_cb(curr->target, curr->current_val, curr->user_data);
            if (!curr->is_running) {
                /* The callback stopped, cleaned up, or destroyed the
                 * target node (__destroy_node from inside the callback is
                 * legal): curr->target may dangle now. Skip this tick's
                 * remainder; the sweep frees the animation without ever
                 * touching the target again. */
                prev = curr;
                curr = curr->next;
                continue;
            }
        }

        aroma_node_invalidate(curr->target);
        needs_redraw = true;

        if (finished) {
            if (curr->loop_mode == AROMA_LOOP_RESTART && curr->duration_ms > 0) {
                /* Loop: restart the cycle from start_val. The end value
                   was already written above, so the next tick continues
                   seamlessly from the beginning. */
                curr->start_time = now;
                curr->last_progress = 0.0f;
                still_running = true;
            } else if (curr->loop_mode == AROMA_LOOP_PINGPONG && curr->duration_ms > 0) {
                /* Ping-pong: swap direction so the next cycle eases back
                   toward the start value with no visible jump. */
                float tmp = curr->start_val;
                curr->start_val = curr->end_val;
                curr->end_val = tmp;
                curr->start_time = now;
                curr->last_progress = 0.0f;
                still_running = true;
            } else {
                curr->is_running = false;

                // Invoke completion callback if registered
                if (curr->on_complete) {
                    curr->on_complete(curr->target, curr->user_data);
                }
            }
        } else {
            still_running = true;
        }

        prev = curr;
        curr = curr->next;
    }

    /* Sweep animations that user callbacks (custom_cb / on_complete above)
     * marked dead mid-tick. They could only be flagged, never freed, while
     * s_tick_depth > 0, so the node the loop stands on is always valid. */
    if (s_deferred_sweep) {
        s_deferred_sweep = false;
        AromaAnimation* c = animation_list;
        AromaAnimation* p = NULL;
        while (c) {
            if (!c->is_running) {
                if (p) p->next = c->next;
                else   animation_list = c->next;
                AromaAnimation* dead = c;
                c = c->next;
                free(dead);
                continue;
            }
            p = c;
            c = c->next;
        }
    }
    s_tick_depth--;

    if (needs_redraw) {
        aroma_ui_request_redraw(NULL);
        if (still_running) {
            /* Keep the frame chain alive. On Android frames are
             * vsync-gated: without an explicit request the Choreographer
             * posts nothing while the app is otherwise idle and the
             * animation would stall. On Linux/GLPS this resolves to an
             * immediate update callback and is harmless. */
            aroma_ui_request_frame();
        }
    }
}

void aroma_animation_pause_all(void)
{
    if (s_paused)
        return;
    s_paused = true;
    s_pause_begin = aroma_time_now_ms();
}

void aroma_animation_resume_all(void)
{
    if (!s_paused)
        return;
    s_paused = false;
    uint64_t now = aroma_time_now_ms();
    uint64_t gap = (now >= s_pause_begin) ? (now - s_pause_begin) : 0;
    if (gap > 0) {
        for (AromaAnimation* c = animation_list; c; c = c->next) {
            if (c->is_running)
                c->start_time += gap;
        }
    }
    if (animation_list)
        aroma_ui_request_frame();
}

bool aroma_animation_is_paused(void)
{
    return s_paused;
}


void aroma_animation_manager_init(void)
{
    if (!anim_timer) {
        anim_timer = aroma_timer_create(16, true, update_animations, NULL);
    }
}

bool aroma_animation_is_running_on(AromaNode *target)
{
    if (!target)
        return false;
    for (AromaAnimation *c = animation_list; c; c = c->next)
    {
        if (c->target == target && c->is_running)
            return true;
    }
    return false;
}

void aroma_animation_manager_shutdown(void)
{
    if (anim_timer) {
        aroma_timer_cancel(anim_timer);
        anim_timer = NULL;
    }
    /* Previously the live list leaked here; free it so shutdown is clean
     * on both Linux (process exit hygiene, LSAN builds) and Android
     * (activity re-creation without process death). */
    aroma_animation_cleanup_all();
    s_paused = false;
    s_pause_begin = 0;
}

AromaAnimation* aroma_animation_start(AromaNode*         target,
                                       AromaAnimationType type,
                                       float              start_val,
                                       float              end_val,
                                       uint32_t           duration_ms)
{
    if (!target) return NULL;

    aroma_animation_stop(target);

    AromaAnimation* anim = (AromaAnimation*)calloc(1, sizeof(AromaAnimation));
    if (!anim) return NULL;

    anim->target      = target;
    anim->type        = type;
    anim->start_val   = start_val;
    anim->end_val     = end_val;
    anim->current_val = start_val;
    anim->duration_ms = duration_ms;
    anim->start_time  = aroma_time_now_ms();
    anim->is_running  = true;
    anim->easing      = AROMA_EASE_OUT_CUBIC;
    anim->on_complete = NULL; // Initialize safe default

    anim->next     = animation_list;
    animation_list = anim;

    if (!anim_timer) aroma_animation_manager_init();

    /* Guarantee the frame chain runs while this animation is alive. On
     * Android frames only happen after an explicit request; without this,
     * an animation started while the app is otherwise idle would never
     * tick. No-op before UI init (tests) and cheap on Linux. */
    aroma_ui_request_frame();
    return anim;
}

static void cleanup_animation_list(void) {
    AromaAnimation* curr = animation_list;
    while (curr) {
        AromaAnimation* next = curr->next;
        free(curr);
        curr = next;
    }
    animation_list = NULL;
}

void aroma_animation_cleanup_all(void) {
    if (s_tick_depth > 0) {
        /* Inside a tick callback: only flag, the tick loop sweeps. */
        for (AromaAnimation* c = animation_list; c; c = c->next)
            c->is_running = false;
        s_deferred_sweep = true;
        return;
    }
    cleanup_animation_list();
}

void aroma_animation_cleanup_node(AromaNode* target) {
    if (!target) return;

    if (s_tick_depth > 0) {
        /* Inside a tick callback (e.g. on_complete destroying its own
         * target): only flag, the tick loop sweeps. Freeing here would
         * invalidate the node the loop is standing on. */
        for (AromaAnimation* c = animation_list; c; c = c->next) {
            if (c->target == target)
                c->is_running = false;
        }
        s_deferred_sweep = true;
        return;
    }
    AromaAnimation* curr = animation_list;
    AromaAnimation* prev = NULL;
    
    while (curr) {
        AromaAnimation* next = curr->next;
        if (curr->target == target) {
            if (prev) {
                prev->next = next;
            } else {
                animation_list = next;
            }
            free(curr);
        } else {
            prev = curr;
        }
        curr = next;
    }
}

void aroma_animation_stop(AromaNode* target)
{
    AromaAnimation* curr = animation_list;
    while (curr) {
        if (curr->target == target) curr->is_running = false;
        curr = curr->next;
    }
}

AromaAnimation* aroma_animation_start_custom(AromaNode*              target,
                                              float                   start_val,
                                              float                   end_val,
                                              uint32_t                duration_ms,
                                              AromaAnimationCallback  cb,
                                              void*                   user_data)
{
    AromaAnimation* anim = aroma_animation_start(
        target, AROMA_ANIM_CUSTOM, start_val, end_val, duration_ms);
    if (anim) {
        anim->custom_cb = cb;
        anim->user_data = user_data;
    }
    return anim;
}

void aroma_animation_set_easing(AromaAnimation* anim, AromaEasingType easing)
{
    if (anim) anim->easing = easing;
}

void aroma_animation_set_on_complete(AromaAnimation* anim, AromaAnimationCompleteCallback cb)
{
    if (anim) anim->on_complete = cb;
}

void aroma_animation_set_loop(AromaAnimation* anim, bool loop)
{
    if (anim) anim->loop_mode = loop ? AROMA_LOOP_RESTART : AROMA_LOOP_OFF;
}

void aroma_animation_set_loop_mode(AromaAnimation* anim, AromaLoopMode mode)
{
    if (anim) anim->loop_mode = mode;
}