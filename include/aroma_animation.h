#ifndef AROMA_ANIMATION_H
#define AROMA_ANIMATION_H

#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif








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


typedef enum {
    AROMA_LOOP_OFF,
    AROMA_LOOP_RESTART,
    AROMA_LOOP_PINGPONG
} AromaLoopMode;


typedef void (*AromaAnimationCallback)(AromaNode* target, float current_val, void* user_data);


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


    AromaAnimationCompleteCallback on_complete;




    AromaLoopMode loop_mode;



    float last_progress;







    bool parent_relative;

    struct _AromaAnimation* next;
} AromaAnimation;

void aroma_animation_manager_init(void);
bool aroma_animation_is_running_on(AromaNode *target);void aroma_animation_manager_shutdown(void);

















void aroma_animation_tick(uint64_t now_ms);







void aroma_animation_pause_all(void);
void aroma_animation_resume_all(void);
bool aroma_animation_is_paused(void);
AromaAnimation* aroma_animation_start(AromaNode* target, AromaAnimationType type, float start_val, float end_val, uint32_t duration_ms);
void aroma_animation_stop(AromaNode* target);
AromaAnimation* aroma_animation_start_custom(AromaNode* target, float start_val, float end_val, uint32_t duration_ms, AromaAnimationCallback cb, void* user_data);
void aroma_animation_set_easing(AromaAnimation* anim, AromaEasingType easing);

void aroma_animation_set_on_complete(AromaAnimation* anim, AromaAnimationCompleteCallback cb);




void aroma_animation_set_loop(AromaAnimation* anim, bool loop);



void aroma_animation_set_loop_mode(AromaAnimation* anim, AromaLoopMode mode);

void aroma_animation_cleanup_node(AromaNode* target);
void aroma_animation_cleanup_all(void);

#ifdef __cplusplus
}
#endif

#endif