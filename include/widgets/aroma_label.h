#ifndef AROMA_LABEL_H
#define AROMA_LABEL_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"

// Material Design 3 Label (Text Display)
typedef enum {
    LABEL_STYLE_DISPLAY_LARGE,    // 57sp
    LABEL_STYLE_DISPLAY_MEDIUM,   // 45sp
    LABEL_STYLE_DISPLAY_SMALL,    // 36sp
    LABEL_STYLE_HEADLINE_LARGE,   // 32sp
    LABEL_STYLE_HEADLINE_MEDIUM,  // 28sp
    LABEL_STYLE_HEADLINE_SMALL,   // 24sp
    LABEL_STYLE_TITLE_LARGE,      // 22sp
    LABEL_STYLE_TITLE_MEDIUM,     // 16sp
    LABEL_STYLE_TITLE_SMALL,      // 14sp
    LABEL_STYLE_BODY_LARGE,       // 16sp
    LABEL_STYLE_BODY_MEDIUM,      // 14sp
    LABEL_STYLE_BODY_SMALL,       // 12sp
    LABEL_STYLE_LABEL_LARGE,      // 14sp
    LABEL_STYLE_LABEL_MEDIUM,     // 12sp
    LABEL_STYLE_LABEL_SMALL       // 11sp
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

#endif // AROMA_LABEL_H
