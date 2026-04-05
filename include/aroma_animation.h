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
    AROMA_ANIM_FADE
} AromaAnimationType;

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
    struct _AromaAnimation* next;
} AromaAnimation;

void aroma_animation_manager_init(void);
AromaAnimation* aroma_animation_start(AromaNode* target, AromaAnimationType type, float start_val, float end_val, uint32_t duration_ms);
void aroma_animation_stop(AromaNode* target);

#ifdef __cplusplus
}
#endif

#endif
