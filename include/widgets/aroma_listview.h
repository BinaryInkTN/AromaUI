#ifndef AROMA_LISTVIEW_H
#define AROMA_LISTVIEW_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct    __attribute__((packed, aligned(1)))
{ 
    char text[64];
    char secondary_text[64];
    char icon[32];
    uint8_t _pad[4];
    void* user_data;
} AromaListItem;
typedef struct  AromaListView AromaListView;

#define AROMA_LIST_ITEM_NORMAL      0
#define AROMA_LIST_ITEM_HEADER       1
#define AROMA_LIST_ITEM_SEPARATOR    2

AromaNode* aroma_listview_create(AromaNode* parent, int x, int y, int width, int height);
void aroma_listview_add_item(AromaNode* list_node, const char* text, const char* secondary_text, void* user_data);
void aroma_listview_add_item_with_icon(AromaNode* list_node, const char* text, const char* secondary_text, const char* icon_code, void* user_data);
void aroma_listview_add_header(AromaNode* list_node, const char* text);
void aroma_listview_add_separator(AromaNode* list_node);
void aroma_listview_remove_item(AromaNode* list_node, int index);
void aroma_listview_clear(AromaNode* list_node);
int aroma_listview_get_selected(AromaNode* list_node);
size_t aroma_listview_get_count(AromaNode* list_node);
void* aroma_listview_get_item_data(AromaNode* list_node, int index);
void aroma_listview_set_callback(AromaNode* list_node, void (*callback)(int index, void* user_data), void* user_data);
void aroma_listview_set_font(AromaNode* list_node, AromaFont* font);
void aroma_listview_set_secondary_font(AromaNode* list_node, AromaFont* font);
void aroma_listview_set_icon_font(AromaNode* list_node, AromaFont* font);
void aroma_listview_set_item_height(AromaNode* list_node, int height);
void aroma_listview_set_text_scale(AromaNode* list_node, float scale);
void aroma_listview_set_secondary_text_scale(AromaNode* list_node, float scale);
void aroma_listview_set_corner_radius(AromaNode* list_node, float radius);
void aroma_listview_set_selected_corner_radius(AromaNode* list_node, float radius);
void aroma_listview_show_headers(AromaNode* list_node, bool show);
void aroma_listview_set_header_colors(AromaNode* list_node, uint32_t bg_color, uint32_t text_color);
void aroma_listview_draw(AromaNode* list_node, size_t window_id);
void aroma_listview_update_title_text(AromaNode* list_node, int index, const char* new_text);
void aroma_listview_update_secondary_text(AromaNode* list_node, int index, const char* new_text);

void aroma_listview_destroy(AromaNode* list_node);

void aroma_listview_set_scroll_container(AromaNode* list_node, AromaNode* container);
AromaNode* aroma_listview_get_scroll_container(AromaNode* list_node);

#ifdef __cplusplus
}
#endif

#endif