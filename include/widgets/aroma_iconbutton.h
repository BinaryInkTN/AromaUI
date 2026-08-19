#ifndef AROMA_ICON_BUTTON_H
#define AROMA_ICON_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ICON_BUTTON_STANDARD,    
    ICON_BUTTON_FILLED,      
    ICON_BUTTON_TONAL,       
    ICON_BUTTON_OUTLINED     
} AromaIconButtonVariant;

typedef struct  AromaIconButton AromaIconButton;


AromaNode* aroma_iconbutton_create(AromaNode* parent, const char* icon_text, int x, int y, int size, AromaIconButtonVariant variant);


void aroma_iconbutton_set_callback(AromaNode* button_node, void (*callback)(void* user_data), void* user_data);


void aroma_iconbutton_set_colors(AromaNode* button_node, uint32_t bg_color, uint32_t icon_color);

void aroma_iconbutton_set_icon(AromaNode* button_node, const char* icon_text);

void aroma_iconbutton_set_font(AromaNode* button_node, AromaFont* font);


void aroma_iconbutton_draw(AromaNode* button_node, size_t window_id);


void aroma_iconbutton_destroy(AromaNode* button_node);
#ifdef __cplusplus
}
#endif
#endif 
