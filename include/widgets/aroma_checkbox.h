#ifndef AROMA_CHECKBOX_H
#define AROMA_CHECKBOX_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct  AromaCheckbox AromaCheckbox;


AromaNode* aroma_checkbox_create(AromaNode* parent, const char* label, int x, int y, int width, int height);


void aroma_checkbox_set_checked(AromaNode* checkbox_node, bool checked);
bool aroma_checkbox_is_checked(AromaNode* checkbox_node);


void aroma_checkbox_set_callback(AromaNode* checkbox_node, void (*callback)(bool checked, void* user_data), void* user_data);


void aroma_checkbox_set_font(AromaNode* checkbox_node, AromaFont* font);


void aroma_checkbox_draw(AromaNode* checkbox_node, size_t window_id);

bool aroma_checkbox_setup_events(AromaNode* node,
								 void (*on_redraw_callback)(void*),
								 void* user_data);


void aroma_checkbox_destroy(AromaNode* checkbox_node);
#ifdef __cplusplus
}
#endif
#endif
