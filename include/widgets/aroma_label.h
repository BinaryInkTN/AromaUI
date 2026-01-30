#ifndef AROMA_LABEL_H
#define AROMA_LABEL_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    LABEL_STYLE_LABEL_LARGE,
    LABEL_STYLE_LABEL_MEDIUM,
    LABEL_STYLE_LABEL_SMALL
} AromaLabelStyle;

typedef struct AromaLabel AromaLabel;

// Create label
AromaNode* aroma_label_create(AromaNode* parent, const char* text, int x, int y, AromaLabelStyle style);

// Set text
void aroma_label_set_text(AromaNode* label_node, const char* text);

// Set color
void aroma_label_set_color(AromaNode* label_node, uint32_t color);

// Set font
void aroma_label_set_font(AromaNode* label_node, AromaFont* font);

// Set style
void aroma_label_set_style(AromaNode* label_node, AromaLabelStyle style);

// Draw
void aroma_label_draw(AromaNode* label_node, size_t window_id);

// Destroy
void aroma_label_destroy(AromaNode* label_node);
#ifdef __cplusplus
}
#endif
#endif // AROMA_LABEL_H
