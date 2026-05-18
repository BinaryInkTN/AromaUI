#ifndef AROMA_GAUGE_H
#define AROMA_GAUGE_H

#include "aroma_common.h"
#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct  AromaGauge AromaGauge;

// Create gauge
AromaNode* aroma_gauge_create(AromaNode* parent, int x, int y, int radius, int thickness);

// Set value and range
void aroma_gauge_set_value(AromaNode* gauge_node, float value, float min, float max);

// Get value
float aroma_gauge_get_value(AromaNode* gauge_node);

// Set colors
void aroma_gauge_set_colors(AromaNode* gauge_node, uint32_t track_color, uint32_t indicator_color);

// Draw
void aroma_gauge_draw(AromaNode* gauge_node, size_t window_id);

// Destroy
void aroma_gauge_destroy(AromaNode* gauge_node);

#ifdef __cplusplus
}
#endif

#endif // AROMA_GAUGE_H