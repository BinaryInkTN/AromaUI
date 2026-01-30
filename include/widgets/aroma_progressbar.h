#ifndef AROMA_PROGRESS_BAR_H
#define AROMA_PROGRESS_BAR_H

#include "aroma_common.h"
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif
// Material Design 3 Linear Progress Indicator
typedef enum {
    PROGRESS_TYPE_DETERMINATE,   // Shows specific progress
    PROGRESS_TYPE_INDETERMINATE  // Shows ongoing activity
} AromaProgressType;

typedef struct AromaProgressBar AromaProgressBar;

// Create progress bar
AromaNode* aroma_progressbar_create(AromaNode* parent, int x, int y, int width, int height, AromaProgressType type);

// Set progress (0.0 to 1.0)
void aroma_progressbar_set_progress(AromaNode* progress_node, float progress);

// Get progress
float aroma_progressbar_get_progress(AromaNode* progress_node);

// Set colors
void aroma_progressbar_set_colors(AromaNode* progress_node, uint32_t track_color, uint32_t indicator_color);

// Draw
void aroma_progressbar_draw(AromaNode* progress_node, size_t window_id);

// Destroy
void aroma_progressbar_destroy(AromaNode* progress_node);
#ifdef __cplusplus
}
#endif
#endif // AROMA_PROGRESS_BAR_H
