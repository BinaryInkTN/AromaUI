#include "widgets/aroma_slider.h"
#include "aroma_common.h"
#include "aroma_event.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>

static void __slider_request_redraw(void* user_data)
{
    if (!user_data) {
        return;
    }
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

AromaNode* aroma_slider_create(AromaNode* parent, int x, int y, int width, int height, 
                               int min_value, int max_value, int initial_value)
{
    if (!parent || width <= 0 || height <= 0 || min_value >= max_value) {
        LOG_ERROR("Invalid slider parameters\n");
        return NULL;
    }

    AromaSlider* data = (AromaSlider*)aroma_widget_alloc(sizeof(AromaSlider));
    if (!data) {
        LOG_ERROR("Failed to allocate slider data\n");
        return NULL;
    }

    data->rect.x = x;
    data->rect.y = y;
    data->rect.width = width;
    data->rect.height = height;
    data->min_value = min_value;
    data->max_value = max_value;
    data->current_value = (initial_value < min_value) ? min_value : 
                          (initial_value > max_value) ? max_value : initial_value;
    data->track_color = 0xCCCCCC;
    data->thumb_color = 0x0078D4;
    data->thumb_hover_color = 0x005A9C;
    data->is_hovered = false;
    data->is_dragging = false;
    data->on_change = NULL;
    data->user_data = NULL;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node) {
        LOG_ERROR("Failed to create slider node\n");
        aroma_widget_free(data);
        return NULL;
    }

    LOG_INFO("Slider created: x=%d, y=%d, w=%d, h=%d, range=%d-%d, value=%d\n", 
             x, y, width, height, min_value, max_value, data->current_value);

    return node;
}

void aroma_slider_set_value(AromaNode* node, int value)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;

    if (value < data->min_value) value = data->min_value;
    if (value > data->max_value) value = data->max_value;

    if (data->current_value != value) {
        data->current_value = value;
        if (data->on_change) {
            data->on_change(node, data->user_data);
        }
        aroma_node_invalidate(node);
    }
}

int aroma_slider_get_value(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) {
        return 0;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;
    return data->current_value;
}

void aroma_slider_set_on_change(AromaNode* node, bool (*callback)(AromaNode*, void*), void* user_data)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;
    data->on_change = callback;
    data->user_data = user_data;

    LOG_INFO("Slider on_change callback registered\n");
}

void aroma_slider_on_click(AromaNode* node, int mouse_x, int mouse_y)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;

    if (mouse_x >= data->rect.x && mouse_x <= data->rect.x + data->rect.width &&
        mouse_y >= data->rect.y && mouse_y <= data->rect.y + data->rect.height) {

        bool was_dragging = data->is_dragging;
        data->is_dragging = true;

        bool state_changed = false;
        if (!was_dragging) {
            state_changed = true;
        }
        if (!data->is_hovered) {
            data->is_hovered = true;
            state_changed = true;
        }
        if (state_changed) {
            aroma_node_invalidate(node);
        }
        int click_offset = mouse_x - data->rect.x;
        int range = data->max_value - data->min_value;
        int new_value = data->min_value + (click_offset * range) / data->rect.width;

        aroma_slider_set_value(node, new_value);
    }
}

void aroma_slider_on_mouse_move(AromaNode* node, int mouse_x, int mouse_y, bool is_pressed)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;

    bool in_bounds = (mouse_x >= data->rect.x && mouse_x <= data->rect.x + data->rect.width &&
                      mouse_y >= data->rect.y && mouse_y <= data->rect.y + data->rect.height);

    bool pressed = is_pressed || data->is_dragging;

    if (!pressed) {
        bool previous_hover = data->is_hovered;
        bool was_dragging = data->is_dragging;
        data->is_hovered = in_bounds;
        data->is_dragging = false;
        if (previous_hover != data->is_hovered || was_dragging) {
            aroma_node_invalidate(node);
        }
        return;
    }

    if (data->is_dragging) {

        int clamped_x = mouse_x;
        if (clamped_x < data->rect.x) clamped_x = data->rect.x;
        if (clamped_x > data->rect.x + data->rect.width) clamped_x = data->rect.x + data->rect.width;

        int click_offset = clamped_x - data->rect.x;
        int range = data->max_value - data->min_value;
        int new_value = data->min_value + (click_offset * range) / data->rect.width;

        aroma_slider_set_value(node, new_value);
    }
}

void aroma_slider_on_mouse_release(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;
    if (data->is_dragging) {
        data->is_dragging = false;
        aroma_node_invalidate(node);
    }
}

void aroma_slider_draw(AromaNode* node, size_t window_id)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaSlider* data = (AromaSlider*)node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        return;
    }

    int track_height = 4;
    int track_y = data->rect.y + (data->rect.height - track_height) / 2;
    gfx->fill_rectangle(window_id, data->rect.x, track_y, data->rect.width, track_height, 
                       data->track_color, false, 0.0f);

    int range = data->max_value - data->min_value;
    int thumb_x = data->rect.x + (data->current_value - data->min_value) * data->rect.width / range;
    int thumb_size = 16;

    uint32_t thumb_color = (data->is_dragging || data->is_hovered) ? data->thumb_hover_color : data->thumb_color;

    gfx->fill_rectangle(window_id, thumb_x - thumb_size/2, data->rect.y, thumb_size, data->rect.height,
                       thumb_color, true, 2.0f);

    gfx->draw_hollow_rectangle(window_id, thumb_x - thumb_size/2, data->rect.y, thumb_size, data->rect.height,
                              0x333333, 1, true, 2.0f);
}

void aroma_slider_destroy(AromaNode* node)
{
    if (!node) {
        return;
    }

    if (node->node_widget_ptr) {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}

static bool __slider_default_mouse_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;

    AromaSlider* slider = (AromaSlider*)event->target_node->node_widget_ptr;
    if (!slider) return false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            aroma_slider_on_click(event->target_node, event->data.mouse.x, event->data.mouse.y);
            __slider_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_MOVE:
            aroma_slider_on_mouse_move(event->target_node, event->data.mouse.x, event->data.mouse.y, false);
            __slider_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_RELEASE:
            aroma_slider_on_mouse_release(event->target_node);
            __slider_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_ENTER:
            if (!slider->is_hovered) {
                slider->is_hovered = true;
                aroma_node_invalidate(event->target_node);
            }
            __slider_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            if (slider->is_hovered || slider->is_dragging) {
                slider->is_hovered = false;
                slider->is_dragging = false;
                aroma_node_invalidate(event->target_node);
            }
            __slider_request_redraw(user_data);
            return true;
        default:
            break;
    }
    return false;
}

bool aroma_slider_setup_events(AromaNode* slider_node, void (*on_redraw_callback)(void*), void* user_data)
{
    if (!slider_node) return false;

    aroma_event_subscribe(slider_node->node_id, EVENT_TYPE_MOUSE_CLICK, __slider_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(slider_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __slider_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(slider_node->node_id, EVENT_TYPE_MOUSE_MOVE, __slider_default_mouse_handler, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(slider_node->node_id, EVENT_TYPE_MOUSE_ENTER, __slider_default_mouse_handler, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(slider_node->node_id, EVENT_TYPE_MOUSE_EXIT, __slider_default_mouse_handler, (void*)on_redraw_callback, 80);

    return true;
}

