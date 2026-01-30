#ifndef AROMA_DIALOG_H
#define AROMA_DIALOG_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif
// Material Design 3 Dialog
typedef enum {
    DIALOG_TYPE_BASIC,      // Basic dialog
    DIALOG_TYPE_FULL_SCREEN // Full-screen dialog
} AromaDialogType;

typedef struct AromaDialog AromaDialog;

// Create dialog
AromaNode* aroma_dialog_create(AromaNode* parent, const char* title, const char* message, int width, int height, AromaDialogType type);

// Add action button
void aroma_dialog_add_action(AromaNode* dialog_node, const char* label, void (*callback)(void* user_data), void* user_data);

// Show/hide dialog
void aroma_dialog_show(AromaNode* dialog_node);
void aroma_dialog_hide(AromaNode* dialog_node);

// Set font
void aroma_dialog_set_font(AromaNode* dialog_node, AromaFont* font);

// Draw
void aroma_dialog_draw(AromaNode* dialog_node, size_t window_id);

// Destroy
void aroma_dialog_destroy(AromaNode* dialog_node);
#ifdef __cplusplus
}
#endif
#endif // AROMA_DIALOG_H
