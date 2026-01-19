#ifndef AROMA_RADIO_BUTTON_H
#define AROMA_RADIO_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Radio Button
typedef struct AromaRadioButton AromaRadioButton;

// Create radio button
AromaNode* aroma_radiobutton_create(AromaNode* parent, const char* label, int x, int y, int width, int height, int group_id);

// Set/get selected state
void aroma_radiobutton_set_selected(AromaNode* radio_node, bool selected);
bool aroma_radiobutton_is_selected(AromaNode* radio_node);

// Set callback
void aroma_radiobutton_set_callback(AromaNode* radio_node, void (*callback)(void* user_data), void* user_data);

// Set font
void aroma_radiobutton_set_font(AromaNode* radio_node, AromaFont* font);

// Draw
void aroma_radiobutton_draw(AromaNode* radio_node, size_t window_id);

bool aroma_radio_button_setup_events(AromaNode* node,
									 void (*on_redraw_callback)(void*),
									 void* user_data);

// Destroy
void aroma_radiobutton_destroy(AromaNode* radio_node);

#endif // AROMA_RADIO_BUTTON_H
