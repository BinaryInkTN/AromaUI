#ifndef AROMA_CARD_H
#define AROMA_CARD_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"


typedef enum {
    CARD_TYPE_ELEVATED,  // Elevated card with shadow
    CARD_TYPE_FILLED,    // Filled card with tint
    CARD_TYPE_OUTLINED   // Outlined card with border
} AromaCardType;

typedef struct AromaCard AromaCard;

// Create a new card
AromaNode* aroma_card_create(AromaNode* parent, int x, int y, int width, int height, AromaCardType type);

// Set card colors
void aroma_card_set_colors(AromaNode* card_node, uint32_t bg_color, uint32_t border_color);

// Set card click callback
void aroma_card_set_click_callback(AromaNode* card_node, void (*callback)(void* user_data), void* user_data);

// Draw card
void aroma_card_draw(AromaNode* card_node, size_t window_id);

// Destroy card
void aroma_card_destroy(AromaNode* card_node);

#endif // AROMA_CARD_H
