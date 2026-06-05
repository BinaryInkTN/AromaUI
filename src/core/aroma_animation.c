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

/* -------------------------------------------------------------------------
 * Easing
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Timer callback — runs every 16 ms unconditionally (no start/stop API).
 * Must be a true no-op when idle so it never causes spurious redraws.
 * ---------------------------------------------------------------------- */

static void update_animations(void* arg)
{
    (void)arg;

    /* True idle fast-path: no allocations, no invalidations, no redraws. */
    if (!animation_list) return;

    uint64_t        now          = aroma_time_now_ms();
    AromaAnimation* curr         = animation_list;
    AromaAnimation* prev         = NULL;
    bool            needs_redraw = false;

    while (curr) {

        /* --- Remove finished entries ---
         * We save target BEFORE freeing so mark_clean is never called on
         * freed memory.  We do NOT call mark_clean at all: the final
         * invalidate from the last active tick already put the node in the
         * dirty list; dirty_list_clear() will handle it normally.
         * Calling mark_clean here would clear is_dirty without removing the
         * node from the dirty list, leaving a dangling-flag inconsistency. */
        if (!curr->is_running) {
            if (prev) prev->next = curr->next;
            else      animation_list = curr->next;
            AromaAnimation* dead = curr;
            curr = curr->next;
            free(dead);
            continue;
        }

        /* --- Compute progress (guard against zero duration) --- */
        float progress = (curr->duration_ms > 0)
            ? (float)(now - curr->start_time) / (float)curr->duration_ms
            : 1.0f;

        bool finished = (progress >= 1.0f);
        if (finished) progress = 1.0f;

        /* --- Apply eased value --- */
        float ease        = apply_easing(curr->easing, progress);
        curr->current_val = curr->start_val
                          + (curr->end_val - curr->start_val) * ease;

        AromaRect* rect = aroma_node_get_rect(curr->target);
        if (rect) {
            switch (curr->type) {
                case AROMA_ANIM_SLIDE_X:  rect->x      = (int)curr->current_val; break;
                case AROMA_ANIM_SLIDE_Y:  rect->y      = (int)curr->current_val; break;
                case AROMA_ANIM_SCALE_X:  rect->width  = (int)curr->current_val; break;
                case AROMA_ANIM_SCALE_Y:  rect->height = (int)curr->current_val; break;
                default: break;
            }
        }

        if (curr->type == AROMA_ANIM_FADE && curr->target) {
            curr->target->opacity = curr->current_val;
        }

        if (curr->type == AROMA_ANIM_CUSTOM && curr->custom_cb) {
            curr->custom_cb(curr->target, curr->current_val, curr->user_data);
        }

        /* Invalidate and request redraw only while the animation is live.
         * On the final tick (finished == true) we invalidate once more so
         * the renderer sees the exact end value, then mark it done.
         * The entry is removed on the NEXT tick — by that point the dirty
         * list has been cleared by the renderer and no extra redraws occur. */
        aroma_node_invalidate(curr->target);
        needs_redraw = true;

        if (finished) {
            curr->is_running = false;
            /* Don't advance prev — this entry will be removed next tick. */
        }

        prev = curr;
        curr = curr->next;
    }

    if (needs_redraw) {
        aroma_ui_request_redraw(NULL);
    }
}

/* =========================================================================
 * Public API
 * ====================================================================== */

void aroma_animation_manager_init(void)
{
    if (!anim_timer) {
        anim_timer = aroma_timer_create(16, true, update_animations, NULL);
    }
}

AromaAnimation* aroma_animation_start(AromaNode*         target,
                                       AromaAnimationType type,
                                       float              start_val,
                                       float              end_val,
                                       uint32_t           duration_ms)
{
    if (!target) return NULL;

    aroma_animation_stop(target);   /* cancel any existing animation on node */

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

    anim->next     = animation_list;
    animation_list = anim;

    if (!anim_timer) aroma_animation_manager_init();
    return anim;
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