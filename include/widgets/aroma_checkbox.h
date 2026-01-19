#ifndef AROMA_CHECKBOX_H
#define AROMA_CHECKBOX_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Checkbox
typedef struct AromaCheckbox AromaCheckbox;

// Create checkbox
AromaNode* aroma_checkbox_create(AromaNode* parent, const char* label, int x, int y, int width, int height);

// Set/get checked state
void aroma_checkbox_set_checked(AromaNode* checkbox_node, bool checked);
bool aroma_checkbox_is_checked(AromaNode* checkbox_node);

// Set callback
void aroma_checkbox_set_callback(AromaNode* checkbox_node, void (*callback)(bool checked, void* user_data), void* user_data);

// Set font
void aroma_checkbox_set_font(AromaNode* checkbox_node, AromaFont* font);

// Draw
void aroma_checkbox_draw(AromaNode* checkbox_node, size_t window_id);

bool aroma_checkbox_setup_events(AromaNode* node,
								 void (*on_redraw_callback)(void*),
								 void* user_data);

// Destroy
void aroma_checkbox_destroy(AromaNode* checkbox_node);

#endif // AROMA_CHECKBOX_H
