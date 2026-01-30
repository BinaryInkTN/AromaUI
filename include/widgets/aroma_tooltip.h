#ifndef AROMA_TOOLTIP_H
#define AROMA_TOOLTIP_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif
// Material Design 3 Tooltip
typedef enum {
    TOOLTIP_POSITION_TOP,
    TOOLTIP_POSITION_BOTTOM,
    TOOLTIP_POSITION_LEFT,
    TOOLTIP_POSITION_RIGHT
} AromaTooltipPosition;

typedef struct AromaTooltip AromaTooltip;

// Create tooltip
AromaNode* aroma_tooltip_create(AromaNode* parent, const char* text, int x, int y, AromaTooltipPosition position);

// Set text
void aroma_tooltip_set_text(AromaNode* tooltip_node, const char* text);

// Show/hide with delay
void aroma_tooltip_show(AromaNode* tooltip_node, int delay_ms);
void aroma_tooltip_hide(AromaNode* tooltip_node);

// Set font
void aroma_tooltip_set_font(AromaNode* tooltip_node, AromaFont* font);

// Draw
void aroma_tooltip_draw(AromaNode* tooltip_node, size_t window_id);

// Destroy
void aroma_tooltip_destroy(AromaNode* tooltip_node);
#ifdef __cplusplus
}
#endif
#endif // AROMA_TOOLTIP_H
