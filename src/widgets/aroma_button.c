#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "widgets/aroma_button.h"
#include "aroma_slab_alloc.h"
#include "aroma_logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool aroma_button_point_in_bounds(AromaButton* button, int x, int y)
{
    if (!button) return false;
    return (x >= button->rect.x && x <= (button->rect.x + button->rect.width) &&
            y >= button->rect.y && y <= (button->rect.y + button->rect.height));
}

static uint32_t aroma_button_get_color(AromaButton* button)
{
    if (!button) return 0xCCCCCC;

    switch (button->state)
    {
        case BUTTON_STATE_HOVER:
            return button->hover_color;
        case BUTTON_STATE_PRESSED:
            return button->pressed_color;
        case BUTTON_STATE_IDLE:
        case BUTTON_STATE_RELEASED:
        default:
            return button->idle_color;
    }
}

AromaNode* aroma_button_create(AromaNode* parent, const char* label, int x, int y, int width, int height)
{
    if (!parent || !label || width <= 0 || height <= 0)
    {
        LOG_ERROR("Invalid button parameters");
        return NULL;
    }

    AromaButton* button = (AromaButton*)aroma_widget_alloc(sizeof(AromaButton));
    if (!button)
    {
        LOG_ERROR("Failed to allocate memory for button");
        return NULL;
    }

    AromaNode* button_node = (AromaNode*)__add_child_node(NODE_TYPE_WIDGET, parent, button);
    if (!button_node)
    {
        aroma_widget_free(button);
        LOG_ERROR("Failed to create button node");
        return NULL;
    }

    button->rect.x = x;
    button->rect.y = y;
    button->rect.width = width;
    button->rect.height = height;
    strncpy(button->label, label, AROMA_BUTTON_LABEL_MAX - 1);
    button->label[AROMA_BUTTON_LABEL_MAX - 1] = '\0';

    button->state = BUTTON_STATE_IDLE;
    button->idle_color = 0x0078D4;      

    button->hover_color = 0x107C10;     

    button->pressed_color = 0x004B50;   

    button->text_color = 0xFFFFFF;      

    button->on_click = NULL;
    button->on_hover = NULL;
    button->user_data = NULL;

    LOG_INFO("Button created: label='%s', x=%d, y=%d, w=%d, h=%d", 
             label, x, y, width, height);

    return button_node;
}

void aroma_button_set_on_click(AromaNode* button_node, bool (*on_click)(AromaNode*, void*), void* user_data)
{
    if (!button_node)
    {
        LOG_ERROR("Invalid button node");
        return;
    }

    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button)
    {
        LOG_ERROR("Button widget pointer is NULL");
        return;
    }

    button->on_click = on_click;
    button->user_data = user_data;
    LOG_INFO("Button click callback registered");
}

void aroma_button_set_on_hover(AromaNode* button_node, bool (*on_hover)(AromaNode*, void*), void* user_data)
{
    if (!button_node)
    {
        LOG_ERROR("Invalid button node");
        return;
    }

    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button)
    {
        LOG_ERROR("Button widget pointer is NULL");
        return;
    }

    button->on_hover = on_hover;
    button->user_data = user_data;
    LOG_INFO("Button hover callback registered");
}

void aroma_button_set_colors(AromaNode* button_node, uint32_t idle_color, uint32_t hover_color, 
                             uint32_t pressed_color, uint32_t text_color)
{
    if (!button_node)
    {
        LOG_ERROR("Invalid button node");
        return;
    }

    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button)
    {
        LOG_ERROR("Button widget pointer is NULL");
        return;
    }

    button->idle_color = idle_color;
    button->hover_color = hover_color;
    button->pressed_color = pressed_color;
    button->text_color = text_color;
    LOG_INFO("Button colors updated");
}

bool aroma_button_handle_mouse_event(AromaNode* button_node, int mouse_x, int mouse_y, bool is_clicked)
{
    if (!button_node)
    {
        return false;
    }

    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button)
    {
        return false;
    }

    bool is_in_bounds = aroma_button_point_in_bounds(button, mouse_x, mouse_y);
    AromaButtonState prev_state = button->state;

    if (is_in_bounds)
    {
        if (is_clicked)
        {
            button->state = BUTTON_STATE_PRESSED;
        }
        else if (button->state == BUTTON_STATE_PRESSED)
        {
            button->state = BUTTON_STATE_RELEASED;

            if (button->on_click)
            {
                LOG_INFO("Button clicked: %s", button->label);
                button->on_click(button_node, button->user_data);
            }
        }
        else
        {
            button->state = BUTTON_STATE_HOVER;

            if (prev_state != BUTTON_STATE_HOVER && button->on_hover)
            {
                LOG_INFO("Button hovered: %s", button->label);
                button->on_hover(button_node, button->user_data);
            }
        }
    }
    else
    {
        button->state = BUTTON_STATE_IDLE;
    }

    return is_in_bounds;
}

void aroma_button_draw(AromaNode* button_node, size_t window_id)
{
    if (!button_node)
    {
        LOG_ERROR("Invalid button node for drawing");
        return;
    }

    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button)
    {
        LOG_ERROR("Button widget pointer is NULL");
        return;
    }

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
    {
        LOG_ERROR("Graphics interface not available");
        return;
    }

    uint32_t button_color = aroma_button_get_color(button);
    gfx->fill_rectangle(
        window_id,
        button->rect.x,
        button->rect.y,
        button->rect.width,
        button->rect.height,
        button_color,
        true,  

        4.0f   

    );

    uint32_t border_color = 0x000000;
    gfx->draw_hollow_rectangle(
        window_id,
        button->rect.x,
        button->rect.y,
        button->rect.width,
        button->rect.height,
        border_color,
        1.0f,      
        true,   
        4.0f    
    );
    LOG_INFO("Button drawn: %s at (%d, %d)", button->label, button->rect.x, button->rect.y);
}

void aroma_button_destroy(AromaNode* button_node)
{
    if (!button_node)
    {
        return;
    }

    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (button)
    {
        aroma_widget_free(button);
    }

    __destroy_node(button_node);
    LOG_INFO("Button destroyed");
}

static bool __button_default_mouse_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;

    AromaButton* btn = (AromaButton*)event->target_node->node_widget_ptr;
    if (!btn) return false;

    AromaButtonState prev_state = btn->state;
    bool in_bounds = false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            in_bounds = aroma_button_handle_mouse_event(event->target_node,
                event->data.mouse.x, event->data.mouse.y, true);
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
        case EVENT_TYPE_MOUSE_MOVE:
            in_bounds = aroma_button_handle_mouse_event(event->target_node,
                event->data.mouse.x, event->data.mouse.y, false);
            break;
        default:
            break;
    }

    if (btn->state != prev_state && user_data) {
        void (*on_redraw)(void*) = (void (*)(void*))user_data;
        on_redraw(NULL);
    }

    return in_bounds;
}

bool aroma_button_setup_events(AromaNode* button_node, void (*on_redraw_callback)(void*), void* user_data)
{
    if (!button_node) return false;

    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_CLICK, __button_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __button_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_MOVE, __button_default_mouse_handler, (void*)on_redraw_callback, 80);

    return true;
}

