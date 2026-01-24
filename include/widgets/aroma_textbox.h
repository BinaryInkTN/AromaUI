#ifndef AROMA_TEXTBOX_H
#define AROMA_TEXTBOX_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define AROMA_TEXTBOX_MAX_LENGTH 256
#define AROMA_TEXTBOX_CURSOR_BLINK_RATE 500

typedef struct AromaFont AromaFont;
typedef struct AromaWindow AromaWindow;

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
    float text_scale;
    int cursor_x;
    int cursor_y;
    int text_x;
    int cursor_height;
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

void aroma_ui_request_redraw(void* user_data);

static inline AromaTextbox* aroma_ui_create_textbox(AromaWindow* parent, const char* placeholder,
                                       int x, int y, int width, int height) {
    if (!parent) return NULL;

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* textbox = aroma_textbox_create(parent_node, x, y, width, height);
    if (!textbox) {
        LOG_ERROR("Failed to create textbox");
        return NULL;
    }

    if (placeholder) {
        aroma_textbox_set_placeholder(textbox, placeholder);
    }

    aroma_textbox_setup_events(textbox, aroma_ui_request_redraw, NULL, NULL);
    aroma_node_invalidate(textbox);

    LOG_INFO("Textbox created with placeholder: %s", placeholder ? placeholder : "");
    return (AromaTextbox*)textbox;
}

static inline void aroma_ui_textbox_set_text(AromaTextbox* textbox, const char* text) {
    if (!textbox || !text) return;

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    if (tb) {
        strncpy(tb->text, text, AROMA_TEXTBOX_MAX_LENGTH - 1);
        tb->text[AROMA_TEXTBOX_MAX_LENGTH - 1] = '\0';
        tb->text_length = strlen(tb->text);
        aroma_node_invalidate(textbox_node);
    }
}

static inline const char* aroma_ui_textbox_get_text(AromaTextbox* textbox) {
    if (!textbox) return "";

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    return tb ? tb->text : "";
}

static inline void aroma_ui_on_textbox_change(AromaTextbox* textbox,
                                 bool (*handler)(AromaTextbox*, const char*, void*),
                                 void* user_data) {
    if (!textbox) return;

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    if (tb) {
        tb->on_text_changed = (bool (*)(AromaNode*, const char*, void*))handler;
        tb->user_data = user_data;
    }
}

static inline void aroma_ui_destroy_textbox(AromaTextbox* textbox) {
    if (!textbox) return;
    aroma_textbox_destroy((AromaNode*)textbox);
}

#endif
