#ifndef AROMA_TOOLTIP_H
#define AROMA_TOOLTIP_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOOLTIP_POSITION_TOP,
    TOOLTIP_POSITION_BOTTOM,
    TOOLTIP_POSITION_LEFT,
    TOOLTIP_POSITION_RIGHT
} AromaTooltipPosition;

typedef struct  AromaTooltip AromaTooltip;


AromaNode* aroma_tooltip_create(AromaNode* parent, const char* text, int x, int y, AromaTooltipPosition position);


void aroma_tooltip_set_text(AromaNode* tooltip_node, const char* text);


void aroma_tooltip_show(AromaNode* tooltip_node, int delay_ms);
void aroma_tooltip_hide(AromaNode* tooltip_node);


void aroma_tooltip_set_font(AromaNode* tooltip_node, AromaFont* font);


void aroma_tooltip_draw(AromaNode* tooltip_node, size_t window_id);


void aroma_tooltip_destroy(AromaNode* tooltip_node);
#ifdef __cplusplus
}
#endif
#endif
