#ifndef AROMA_TEXTBOX_H
#define AROMA_TEXTBOX_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

#define AROMA_TEXTBOX_MAX_LENGTH 256
#define AROMA_TEXTBOX_CURSOR_BLINK_RATE 500  

typedef struct AromaFont AromaFont;

typedef struct AromaTextbox
{
    AromaRect rect;
    char text[AROMA_TEXTBOX_MAX_LENGTH];
    size_t text_length;
    size_t cursor_pos;           

    bool is_focused;
    bool is_hovered;
    bool show_cursor;            

    uint64_t cursor_blink_time;  

    uint32_t bg_color;
    uint32_t hover_bg_color;
    uint32_t focused_bg_color;
    uint32_t text_color;
    uint32_t border_color;
    uint32_t hover_border_color;
    uint32_t focused_border_color;
    uint32_t cursor_color;
    uint32_t placeholder_color;
    char placeholder[AROMA_TEXTBOX_MAX_LENGTH];
    AromaFont* font;             

    size_t last_window_id;       

    bool (*on_text_changed)(AromaNode* node, const char* text, void* user_data);
    bool (*on_focus_changed)(AromaNode* node, bool focused, void* user_data);
    void* user_data;
} AromaTextbox;

AromaNode* aroma_textbox_create(AromaNode* parent, int x, int y, int width, int height);

void aroma_textbox_set_placeholder(AromaNode* node, const char* placeholder);

void aroma_textbox_set_text(AromaNode* node, const char* text);

const char* aroma_textbox_get_text(AromaNode* node);

void aroma_textbox_set_focused(AromaNode* node, bool focused);

bool aroma_textbox_is_focused(AromaNode* node);

void aroma_textbox_on_click(AromaNode* node, int mouse_x, int mouse_y);

void aroma_textbox_on_char(AromaNode* node, char character);

void aroma_textbox_on_backspace(AromaNode* node);

void aroma_textbox_set_on_text_changed(AromaNode* node, 
                                      bool (*callback)(AromaNode*, const char*, void*), 
                                      void* user_data);

void aroma_textbox_set_on_focus_changed(AromaNode* node, 
                                       bool (*callback)(AromaNode*, bool, void*), 
                                       void* user_data);

void aroma_textbox_draw(AromaNode* node, size_t window_id);

void aroma_textbox_set_font(AromaNode* node, AromaFont* font);

void aroma_textbox_destroy(AromaNode* node);

bool aroma_textbox_setup_events(AromaNode* textbox_node, 
                               void (*on_redraw_callback)(void*),
                               bool (*on_text_changed_callback)(AromaNode*, const char*, void*),
                               void* user_data);

#endif

