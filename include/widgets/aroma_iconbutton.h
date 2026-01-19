#ifndef AROMA_ICON_BUTTON_H
#define AROMA_ICON_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Icon Button variants
typedef enum {
    ICON_BUTTON_STANDARD,    // Standard icon button
    ICON_BUTTON_FILLED,      // Filled background
    ICON_BUTTON_TONAL,       // Tonal background
    ICON_BUTTON_OUTLINED     // With outline
} AromaIconButtonVariant;

typedef struct AromaIconButton AromaIconButton;

// Create icon button
AromaNode* aroma_iconbutton_create(AromaNode* parent, const char* icon_text, int x, int y, int size, AromaIconButtonVariant variant);

// Set click callback
void aroma_iconbutton_set_callback(AromaNode* button_node, void (*callback)(void* user_data), void* user_data);

// Set colors
void aroma_iconbutton_set_colors(AromaNode* button_node, uint32_t bg_color, uint32_t icon_color);

// Set font for icon
void aroma_iconbutton_set_font(AromaNode* button_node, AromaFont* font);

// Draw
void aroma_iconbutton_draw(AromaNode* button_node, size_t window_id);

// Destroy
void aroma_iconbutton_destroy(AromaNode* button_node);

#endif // AROMA_ICON_BUTTON_H
