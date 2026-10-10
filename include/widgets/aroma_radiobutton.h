#ifndef AROMA_RADIO_BUTTON_H
#define AROMA_RADIO_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct  AromaRadioButton AromaRadioButton;
typedef struct  AromaRadioGroup AromaRadioGroup;


AromaNode* aroma_radiobutton_create(AromaNode* parent, const char* label, int x, int y, int width, int height, int group_id);



AromaRadioGroup* aroma_radio_group_create(void);
void aroma_radio_group_destroy(AromaRadioGroup* group);
int aroma_radio_group_get_selected(const AromaRadioGroup* group);


void aroma_radiobutton_set_selected(AromaNode* radio_node, bool selected);
bool aroma_radiobutton_is_selected(AromaNode* radio_node);


void aroma_radiobutton_set_callback(AromaNode* radio_node, void (*callback)(void* user_data), void* user_data);


void aroma_radiobutton_set_font(AromaNode* radio_node, AromaFont* font);


void aroma_radiobutton_draw(AromaNode* radio_node, size_t window_id);

bool aroma_radio_button_setup_events(AromaNode* node,
									 void (*on_redraw_callback)(void*),
									 void* user_data);


void aroma_radiobutton_destroy(AromaNode* radio_node);
#ifdef __cplusplus
}
#endif
#endif
