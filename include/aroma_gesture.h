#ifndef AROMA_GESTURE_H
#define AROMA_GESTURE_H

#include "aroma_common.h"
#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AROMA_GESTURE_TAP        = 1u << 0,
    AROMA_GESTURE_DOUBLE_TAP = 1u << 1,
    AROMA_GESTURE_LONG_PRESS = 1u << 2,
    AROMA_GESTURE_SWIPE_LEFT  = 1u << 3,
    AROMA_GESTURE_SWIPE_RIGHT = 1u << 4,
    AROMA_GESTURE_SWIPE_UP    = 1u << 5,
    AROMA_GESTURE_SWIPE_DOWN  = 1u << 6,
    AROMA_GESTURE_PAN         = 1u << 7,
    AROMA_GESTURE_PINCH       = 1u << 8
} AromaGestureType;

typedef struct {
    AromaGestureType type;
    AromaNode *target;
    int start_x;
    int start_y;
    int x;
    int y;
    int dx;
    int dy;
    float scale;
    float velocity_x;
    float velocity_y;
} AromaGestureEvent;

typedef void (*AromaGestureCallback)(const AromaGestureEvent *event,
                                     void *user_data);

typedef struct AromaGestureObserver AromaGestureObserver;

AromaGestureObserver *aroma_gesture_observe(AromaNode *node, uint32_t mask,
                                            AromaGestureCallback cb,
                                            void *user_data);
void aroma_gesture_unobserve(AromaGestureObserver *observer);
void aroma_gesture_set_enabled(AromaGestureObserver *observer, bool enabled);

void aroma_gesture_handle_touch(int id, int x, int y, int state);

#ifdef __cplusplus
}
#endif

#endif
