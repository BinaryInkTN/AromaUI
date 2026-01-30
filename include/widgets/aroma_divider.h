#ifndef AROMA_DIVIDER_H
#define AROMA_DIVIDER_H

#include "aroma_common.h"
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif
// Material Design 3 Divider
typedef enum {
    DIVIDER_ORIENTATION_HORIZONTAL,
    DIVIDER_ORIENTATION_VERTICAL
} AromaDividerOrientation;

typedef struct AromaDivider AromaDivider;

// Create divider
AromaNode* aroma_divider_create(AromaNode* parent, int x, int y, int length, AromaDividerOrientation orientation);

// Set color
void aroma_divider_set_color(AromaNode* divider_node, uint32_t color);

// Set thickness
void aroma_divider_set_thickness(AromaNode* divider_node, int thickness);

// Draw
void aroma_divider_draw(AromaNode* divider_node, size_t window_id);

// Destroy
void aroma_divider_destroy(AromaNode* divider_node);
#ifdef __cplusplus
}
#endif
#endif // AROMA_DIVIDER_H
