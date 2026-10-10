#ifndef AROMA_STEPPER_H
#define AROMA_STEPPER_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AROMA_STEPPER_STEPS_MAX 8
#define AROMA_STEPPER_LABEL_MAX 32

typedef enum AromaStepperMode {
    AROMA_STEPPER_NUMERIC,
    AROMA_STEPPER_STEPS
} AromaStepperMode;

typedef struct AromaStepper AromaStepper;

typedef void (*AromaStepperChangeCb)(AromaNode *node, int value,
                                     void *user_data);

AromaNode *aroma_stepper_create_numeric(AromaNode *parent, int x, int y,
                                        int width, int height, int min_val,
                                        int max_val, int value, int step);
AromaNode *aroma_stepper_create_steps(AromaNode *parent, int x, int y,
                                      int width, int height,
                                      const char **labels, int count);

void aroma_stepper_set_value(AromaNode *st_node, int value);
int aroma_stepper_get_value(AromaNode *st_node);
void aroma_stepper_set_wrap(AromaNode *st_node, bool wrap);
void aroma_stepper_set_step_index(AromaNode *st_node, int index);
int aroma_stepper_get_step_index(AromaNode *st_node);
void aroma_stepper_set_on_change(AromaNode *st_node, AromaStepperChangeCb cb,
                                 void *user_data);
void aroma_stepper_set_font(AromaNode *st_node, AromaFont *font);





bool aroma_stepper_owns_touch(AromaNode *st_node);
bool aroma_stepper_setup_events(AromaNode *st_node, void (*on_redraw)(void *),
                                void *user_data);
void aroma_stepper_draw(AromaNode *st_node, size_t window_id);
void aroma_stepper_destroy(AromaNode *st_node);

#ifdef __cplusplus
}
#endif
#endif
