#include "aroma_animation.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "aroma_node.h"
#include "aroma_common.h"
#include "aroma_ui.h"
#include <stdlib.h>
#include <math.h>

static AromaAnimation* animation_list = NULL;
static AromaTimer* anim_timer = NULL;

static void update_animations(void* arg) {
    if (!animation_list) return;

    uint64_t now = aroma_time_now_ms();
    AromaAnimation* curr = animation_list;
    AromaAnimation* prev = NULL;

    bool needs_redraw = false;

    while (curr) {
        if (!curr->is_running) {
            // Remove
            if (prev) prev->next = curr->next;
            else animation_list = curr->next;
            AromaAnimation* temp = curr;
            curr = curr->next;
            aroma_node_mark_clean(temp->target);
            free(temp);
            continue;
        }

        float progress = (float)(now - curr->start_time) / (float)curr->duration_ms;
        if (progress >= 1.0f) {
            progress = 1.0f;
            curr->is_running = false;
        }

        // Simple ease out
        float ease = 1.0f - powf(1.0f - progress, 3.0f);
        curr->current_val = curr->start_val + (curr->end_val - curr->start_val) * ease;

        AromaRect* rect = (AromaRect*)curr->target->node_widget_ptr;
        if (rect) {
            if (curr->type == AROMA_ANIM_SLIDE_X) {
                rect->x = (int)curr->current_val;
            } else if (curr->type == AROMA_ANIM_SLIDE_Y) {
                rect->y = (int)curr->current_val;
            }
        }
        
        aroma_node_invalidate(curr->target);

        needs_redraw = true;

        prev = curr;
        curr = curr->next;
    }

    if (needs_redraw) {
        aroma_ui_render_all();
    }
}

void aroma_animation_manager_init(void) {
    if (!anim_timer) {
        anim_timer = aroma_timer_create(16, true, update_animations, NULL);
    }
}

AromaAnimation* aroma_animation_start(AromaNode* target, AromaAnimationType type, float start_val, float end_val, uint32_t duration_ms) {
    aroma_animation_stop(target); // Stop existing on target
    
    AromaAnimation* anim = (AromaAnimation*)calloc(1, sizeof(AromaAnimation));
    anim->target = target;
    anim->type = type;
    anim->start_val = start_val;
    anim->end_val = end_val;
    anim->current_val = start_val;
    anim->duration_ms = duration_ms;
    anim->start_time = aroma_time_now_ms();
    anim->is_running = true;

    anim->next = animation_list;
    animation_list = anim;
    
    if (!anim_timer) {
        aroma_animation_manager_init();
    }
    return anim;
}

void aroma_animation_stop(AromaNode* target) {
    AromaAnimation* curr = animation_list;
    while (curr) {
        if (curr->target == target) {
            curr->is_running = false; // Will be cleaned up on next tick
        }
        curr = curr->next;
    }
}
