#ifndef AROMA_LISTVIEW_H
#define AROMA_LISTVIEW_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 List View
typedef struct AromaListItem {
    char text[128];
    char secondary_text[128];
    void* user_data;
} AromaListItem;

typedef struct AromaListView AromaListView;

// Create list view
AromaNode* aroma_listview_create(AromaNode* parent, int x, int y, int width, int height);

// Add item
void aroma_listview_add_item(AromaNode* list_node, const char* text, const char* secondary_text, void* user_data);

// Clear all items
void aroma_listview_clear(AromaNode* list_node);

// Set selection callback
void aroma_listview_set_callback(AromaNode* list_node, void (*callback)(int index, void* user_data), void* user_data);

// Set font
void aroma_listview_set_font(AromaNode* list_node, AromaFont* font);

// Draw
void aroma_listview_draw(AromaNode* list_node, size_t window_id);

// Destroy
void aroma_listview_destroy(AromaNode* list_node);

#endif // AROMA_LISTVIEW_H
