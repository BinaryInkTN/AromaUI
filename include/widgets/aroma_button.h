#ifndef AROMA_BUTTON_H
#define AROMA_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include <stdbool.h>

#define AROMA_BUTTON_LABEL_MAX 64

struct AromaStyle;

typedef enum AromaButtonState {
    BUTTON_STATE_IDLE,
    BUTTON_STATE_HOVER,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_RELEASED
} AromaButtonState;

typedef struct AromaButton
{
    AromaRect rect;
    char label[AROMA_BUTTON_LABEL_MAX];
    AromaButtonState state;
    uint32_t idle_color;
    uint32_t hover_color;
    uint32_t pressed_color;
    uint32_t text_color;
    AromaFont* font;
    bool (*on_click)(AromaNode* button_node, void* user_data);
    bool (*on_hover)(AromaNode* button_node, void* user_data);
    void* user_data;
} AromaButton;

AromaNode* aroma_button_create(AromaNode* parent, const char* label, int x, int y, int width, int height);

void aroma_button_set_on_click(AromaNode* button_node, bool (*on_click)(AromaNode*, void*), void* user_data);

void aroma_button_set_on_hover(AromaNode* button_node, bool (*on_hover)(AromaNode*, void*), void* user_data);

void aroma_button_set_colors(AromaNode* button_node, uint32_t idle_color, uint32_t hover_color, uint32_t pressed_color, uint32_t text_color);

void aroma_button_set_font(AromaNode* button_node, AromaFont* font);

void aroma_button_apply_style(AromaNode* button_node, const struct AromaStyle* style);

bool aroma_button_handle_mouse_event(AromaNode* button_node, int mouse_x, int mouse_y, bool is_clicked);

void aroma_button_draw(AromaNode* button_node, size_t window_id);

void aroma_button_destroy(AromaNode* button_node);

bool aroma_button_setup_events(AromaNode* button_node, void (*on_redraw_callback)(void*), void* user_data);

#endif