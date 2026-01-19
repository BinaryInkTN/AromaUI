#ifndef AROMA_CHIP_H
#define AROMA_CHIP_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Chip variants
typedef enum {
    CHIP_TYPE_ASSIST,    // Assist chip
    CHIP_TYPE_FILTER,    // Filter chip (toggleable)
    CHIP_TYPE_INPUT,     // Input chip (with close button)
    CHIP_TYPE_SUGGESTION // Suggestion chip
} AromaChipType;

typedef struct AromaChip AromaChip;

// Create a new chip
AromaNode* aroma_chip_create(AromaNode* parent, int x, int y, const char* label, AromaChipType type);

// Set chip callback
void aroma_chip_set_callback(AromaNode* chip_node, void (*callback)(void* user_data), void* user_data);

// Set chip selected state (for filter chips)
void aroma_chip_set_selected(AromaNode* chip_node, bool selected);

// Set chip font
void aroma_chip_set_font(AromaNode* chip_node, AromaFont* font);

// Draw chip
void aroma_chip_draw(AromaNode* chip_node, size_t window_id);

// Destroy chip
void aroma_chip_destroy(AromaNode* chip_node);

#endif // AROMA_CHIP_H
