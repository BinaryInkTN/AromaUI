#ifndef AROMA_SNACKBAR_H
#define AROMA_SNACKBAR_H

#include "aroma_node.h"
#include "aroma_font.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

AromaNode* aroma_snackbar_create(AromaNode* parent, const char* message, int duration_ms);
void aroma_snackbar_set_action(AromaNode* snackbar_node, const char* action_text, void (*callback)(void* user_data), void* user_data);
void aroma_snackbar_set_font(AromaNode* snackbar_node, AromaFont* font);
void aroma_snackbar_show(AromaNode* snackbar_node);
void aroma_snackbar_dismiss(AromaNode* snackbar_node);
void aroma_snackbar_draw(AromaNode* snackbar_node, size_t window_id);
void aroma_snackbar_destroy(AromaNode* snackbar_node);
bool aroma_snackbar_setup_events(AromaNode* snackbar_node, void (*on_redraw_callback)(void*), void* user_data);

#ifdef __cplusplus
}
#endif

#endif