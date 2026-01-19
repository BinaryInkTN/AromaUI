#ifndef AROMA_MENU_H
#define AROMA_MENU_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

// Material Design 3 Menu
typedef struct AromaMenuItem {
    char text[64];
    bool enabled;
    bool separator;
    void (*callback)(void* user_data);
    void* user_data;
} AromaMenuItem;

typedef struct AromaMenu AromaMenu;

// Create menu
AromaNode* aroma_menu_create(AromaNode* parent, int x, int y);

// Add menu item
void aroma_menu_add_item(AromaNode* menu_node, const char* text, void (*callback)(void* user_data), void* user_data);

// Add separator
void aroma_menu_add_separator(AromaNode* menu_node);

// Show/hide menu
void aroma_menu_show(AromaNode* menu_node);
void aroma_menu_hide(AromaNode* menu_node);

// Set font
void aroma_menu_set_font(AromaNode* menu_node, AromaFont* font);

// Draw
void aroma_menu_draw(AromaNode* menu_node, size_t window_id);

// Destroy
void aroma_menu_destroy(AromaNode* menu_node);

#endif // AROMA_MENU_H
