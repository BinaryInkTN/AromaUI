#ifndef AROMA_SLIDER_H
#define AROMA_SLIDER_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct AromaWindow AromaWindow;

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

void aroma_ui_request_redraw(void* user_data);

typedef struct {
    bool (*handler)(AromaSlider*, int, void*);
    void* user_data;
} UISliderChangeBridge;

static inline bool __ui_slider_on_change_bridge(AromaNode* node, void* data) {
    UISliderChangeBridge* bridge = (UISliderChangeBridge*)data;
    if (!bridge || !bridge->handler || !node) return false;
    struct AromaSlider* sl = (struct AromaSlider*)node->node_widget_ptr;
    int value = sl ? sl->current_value : 0;
    return bridge->handler((AromaSlider*)node, value, bridge->user_data);
}

static inline AromaSlider* aroma_ui_create_slider(AromaWindow* parent,
                                     int x, int y, int width, int height) {
    if (!parent) return NULL;

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* slider = aroma_slider_create(parent_node, x, y, width, height, 0, 100, 50);
    if (!slider) {
        LOG_ERROR("Failed to create slider");
        return NULL;
    }
    aroma_slider_setup_events(slider, aroma_ui_request_redraw, NULL);
    aroma_node_invalidate(slider);

    LOG_INFO("Slider created");
    return (AromaSlider*)slider;
}

static inline void aroma_ui_slider_set_value(AromaSlider* slider, float value) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    int int_value = (int)value;
    if (int_value < 0) int_value = 0;
    if (int_value > 100) int_value = 100;
    aroma_slider_set_value(slider_node, int_value);
    aroma_node_invalidate(slider_node);
}

static inline float aroma_ui_slider_get_value(AromaSlider* slider) {
    if (!slider) return 0.0f;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    return sl ? (float)sl->current_value : 0.0f;
}

static inline void aroma_ui_slider_set_range(AromaSlider* slider, int min, int max) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    if (sl) {
        sl->min_value = min;
        sl->max_value = max;
        if (sl->current_value < min) sl->current_value = min;
        if (sl->current_value > max) sl->current_value = max;
        aroma_node_invalidate(slider_node);
    }
}

static inline void aroma_ui_on_slider_change(AromaSlider* slider,
                                bool (*handler)(AromaSlider*, int, void*),
                                void* user_data) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    if (sl) {
        UISliderChangeBridge* bridge = (UISliderChangeBridge*)sl->user_data;
        if (!bridge) {
            bridge = (UISliderChangeBridge*)malloc(sizeof(UISliderChangeBridge));
            if (!bridge) return;
            sl->user_data = bridge;
        }
        bridge->handler = handler;
        bridge->user_data = user_data;
        sl->on_change = __ui_slider_on_change_bridge;
    }
}

static inline void aroma_ui_destroy_slider(AromaSlider* slider) {
    if (!slider) return;
    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = slider_node ? (struct AromaSlider*)slider_node->node_widget_ptr : NULL;
    if (sl && sl->user_data) {
        free(sl->user_data);
        sl->user_data = NULL;
    }
    aroma_slider_destroy(slider_node);
}

#endif

