#ifndef AROMA_SLIDER_H
#define AROMA_SLIDER_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaSlider
{
    AromaRect rect;
    int min_value, max_value;
    int current_value;
    uint32_t track_color;
    uint32_t thumb_color;
    uint32_t thumb_hover_color;
    bool is_hovered;
    bool is_dragging;
    bool (*on_change)(AromaNode*, void*);
    void* user_data;
} AromaSlider;

AromaNode* aroma_slider_create(AromaNode* parent, int x, int y, int width, int height, 
                               int min_value, int max_value, int initial_value);

void aroma_slider_set_value(AromaNode* node, int value);

int aroma_slider_get_value(AromaNode* node);

void aroma_slider_set_on_change(AromaNode* node, bool (*callback)(AromaNode*, void*), void* user_data);

void aroma_slider_draw(AromaNode* node, size_t window_id);

void aroma_slider_on_click(AromaNode* node, int mouse_x, int mouse_y);

void aroma_slider_on_mouse_move(AromaNode* node, int mouse_x, int mouse_y, bool is_pressed);

void aroma_slider_on_mouse_release(AromaNode* node);

void aroma_slider_destroy(AromaNode* node);

bool aroma_slider_setup_events(AromaNode* slider_node, void (*on_redraw_callback)(void*), void* user_data);

#endif

