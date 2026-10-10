#ifndef AROMA_MENU_H
#define AROMA_MENU_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct  AromaMenuItem {
    char text[64];
    bool enabled;
    bool separator;
    char icon[8];
    void (*callback)(void* user_data);
    void* user_data;
} AromaMenuItem;

typedef struct  AromaMenu AromaMenu;


AromaNode* aroma_menu_create(AromaNode* parent, int x, int y);


void aroma_menu_add_item(AromaNode* menu_node, const char* text, void (*callback)(void* user_data), void* user_data);


void aroma_menu_add_item_with_icon(AromaNode* menu_node, const char* text, const char* icon_code, void (*callback)(void* user_data), void* user_data);


void aroma_menu_add_separator(AromaNode* menu_node);


void aroma_menu_show(AromaNode* menu_node);
void aroma_menu_hide(AromaNode* menu_node);


void aroma_menu_set_font(AromaNode* menu_node, AromaFont* font);


void aroma_menu_set_icon_font(AromaNode* menu_node, AromaFont* font);


void aroma_menu_draw(AromaNode* menu_node, size_t window_id);


void aroma_menu_destroy(AromaNode* menu_node);
#ifdef __cplusplus
}
#endif
#endif
