#ifndef AROMA_FAB_H
#define AROMA_FAB_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Floating Action Button (FAB)
typedef enum {
    FAB_SIZE_SMALL,   // 40dp
    FAB_SIZE_NORMAL,  // 56dp
    FAB_SIZE_LARGE,   // 96dp
    FAB_SIZE_EXTENDED // Variable width with text
} AromaFABSize;

typedef struct AromaFAB AromaFAB;

// Create a new FAB
AromaNode* aroma_fab_create(AromaNode* parent, int x, int y, AromaFABSize size, const char* icon_text);

// Set FAB click callback
void aroma_fab_set_click_callback(AromaNode* fab_node, void (*callback)(void* user_data), void* user_data);

// Set FAB colors
void aroma_fab_set_colors(AromaNode* fab_node, uint32_t bg_color, uint32_t icon_color);

// Set FAB font
void aroma_fab_set_font(AromaNode* fab_node, AromaFont* font);

// Extended FAB with text
void aroma_fab_set_text(AromaNode* fab_node, const char* text);

// Draw FAB
void aroma_fab_draw(AromaNode* fab_node, size_t window_id);

// Destroy FAB
void aroma_fab_destroy(AromaNode* fab_node);

#endif // AROMA_FAB_H
