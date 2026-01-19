#ifndef AROMA_SNACKBAR_H
#define AROMA_SNACKBAR_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Snackbar for temporary messages
typedef struct AromaSnackbar AromaSnackbar;

// Create a new snackbar
AromaNode* aroma_snackbar_create(AromaNode* parent, const char* message, int duration_ms);

// Set snackbar action button
void aroma_snackbar_set_action(AromaNode* snackbar_node, const char* action_text, 
                                void (*callback)(void* user_data), void* user_data);

// Set snackbar font
void aroma_snackbar_set_font(AromaNode* snackbar_node, AromaFont* font);

// Show snackbar (starts animation)
void aroma_snackbar_show(AromaNode* snackbar_node);

// Draw snackbar
void aroma_snackbar_draw(AromaNode* snackbar_node, size_t window_id);

// Destroy snackbar
void aroma_snackbar_destroy(AromaNode* snackbar_node);

#endif // AROMA_SNACKBAR_H
