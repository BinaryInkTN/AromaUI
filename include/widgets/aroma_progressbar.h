#ifndef AROMA_PROGRESS_BAR_H
#define AROMA_PROGRESS_BAR_H

#include "aroma_common.h"
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROGRESS_TYPE_DETERMINATE,
    PROGRESS_TYPE_INDETERMINATE
} AromaProgressType;

typedef struct  AromaProgressBar AromaProgressBar;


AromaNode* aroma_progressbar_create(AromaNode* parent, int x, int y, int width, int height, AromaProgressType type);


void aroma_progressbar_set_progress(AromaNode* progress_node, float progress);


float aroma_progressbar_get_progress(AromaNode* progress_node);


void aroma_progressbar_set_colors(AromaNode* progress_node, uint32_t track_color, uint32_t indicator_color);


void aroma_progressbar_draw(AromaNode* progress_node, size_t window_id);


void aroma_progressbar_destroy(AromaNode* progress_node);
#ifdef __cplusplus
}
#endif
#endif
