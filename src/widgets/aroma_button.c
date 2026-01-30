#include "core/aroma_common.h"
#include "core/aroma_node.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "widgets/aroma_button.h"
#include "core/aroma_style.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void __button_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

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

static void aroma_button_update_text_position(AromaButton* button)
{
    if (!button || button->label[0] == '\0') return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->measure_text) return;
int line_height;
    #ifdef ESP32
    if (!button->font)
    {

        button->text_width = strlen(button->label) * 6 * (int)button->text_scale;
     line_height = 8 * (int)button->text_scale;
    }
    else
    #endif
    {
        if (!button->font) return;
        float measured_width = gfx->measure_text(0, button->font, button->label, button->text_scale);
        button->text_width = (int)(measured_width + 0.5f);
        line_height = aroma_font_get_line_height(button->font) * (int)button->text_scale;
    }

    const int padding = 6;
    button->text_x = button->rect.x + (button->rect.width - button->text_width) / 2;
    if (button->text_x < button->rect.x + padding) button->text_x = button->rect.x + padding;
    button->text_y = button->rect.y + (button->rect.height - line_height) / 2;
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

    aroma_node_set_draw_cb(button_node, aroma_button_draw);

    button->rect.x = x;
    button->rect.y = y;
    button->rect.width = width;
    button->rect.height = height;
    strncpy(button->label, label, AROMA_BUTTON_LABEL_MAX - 1);
    button->label[AROMA_BUTTON_LABEL_MAX - 1] = '\0';

    button->state = BUTTON_STATE_IDLE;
    AromaTheme theme = aroma_theme_get_global();
    button->idle_color = theme.colors.primary;
    button->hover_color = theme.colors.primary_light;
    button->pressed_color = theme.colors.primary_dark;
    button->text_color = theme.colors.surface;
    button->use_theme_colors = true;
    button->font = NULL;

    button->corner_radius = 12.0f;
    button->shadow_color = 0x22222222;
    button->text_scale = 1.0f;

    button->on_click = NULL;
    button->on_hover = NULL;
    button->user_data = NULL;

    button->text_width = 0;
    button->text_x = 0;
    button->text_y = 0;

    aroma_button_update_text_position(button);

    LOG_INFO("Button created: label='%s', x=%d, y=%d, w=%d, h=%d", label, x, y, width, height);

    #ifdef ESP32
    aroma_node_invalidate(button_node); 
    #endif

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
    if (aroma_node_is_hidden(button_node)) return;

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
    button->use_theme_colors = false;
    LOG_INFO("Button colors updated");
    aroma_node_invalidate(button_node);
}

void aroma_button_set_font(AromaNode* button_node, AromaFont* font)
{
    if (!button_node) return;
    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button) return;

    button->font = font;
    aroma_button_update_text_position(button);
    aroma_node_invalidate(button_node);
}

void aroma_button_set_text_scale(AromaNode* button_node, float scale)
{
    if (!button_node || scale <= 0) return;
    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button) return;

    button->text_scale = scale;
    aroma_button_update_text_position(button);
    aroma_node_invalidate(button_node);
}

void aroma_button_apply_style(AromaNode* button_node, const struct AromaStyle* style)
{
    if (!button_node || !style) return;
    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button) return;

    button->idle_color = style->idle_color;
    button->hover_color = style->hover_color;
    button->pressed_color = style->active_color;
    button->text_color = style->text_color;
    button->use_theme_colors = false;
    aroma_node_invalidate(button_node);
}

bool aroma_button_handle_mouse_event(AromaNode* button_node, int mouse_x, int mouse_y, bool is_clicked)
{
    if (!button_node) return false;
    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button) return false;

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

    if (button->state != prev_state) aroma_node_invalidate(button_node);
    return is_in_bounds;
}

void aroma_button_update_label(AromaNode* button_node, const char* label)
{
    if (!button_node || !label) return;
    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (!button) return;

    strncpy(button->label, label, AROMA_BUTTON_LABEL_MAX - 1);
    button->label[AROMA_BUTTON_LABEL_MAX - 1] = '\0';
    aroma_button_update_text_position(button);
    aroma_node_invalidate(button_node);
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

    uint32_t button_color;
    switch (button->state) {
        case BUTTON_STATE_PRESSED:
            button_color = button->pressed_color;
            break;
        case BUTTON_STATE_HOVER:
            button_color = button->hover_color;
            break;
        default:
            button_color = button->idle_color;
            break;
    }

    if (button->shadow_color != 0)
    {
        gfx->fill_rectangle(
            window_id,
            button->rect.x + 1,
            button->rect.y + 2,
            button->rect.width,
            button->rect.height,
            button->shadow_color,
            true,
            button->corner_radius
        );
    }

    gfx->fill_rectangle(
        window_id,
        button->rect.x,
        button->rect.y,
        button->rect.width,
        button->rect.height,
        button_color,
        true,
        button->corner_radius
    );

    if (button->label[0] != '\0')
    {
        if (gfx->render_text)
        {
            gfx->render_text(window_id, button->font, button->label, 
                           button->text_x, button->text_y, 
                           button->text_color, button->text_scale);
        }
    }
}

void aroma_button_destroy(AromaNode* button_node)
{
    if (!button_node) return;
    AromaButton* button = (AromaButton*)button_node->node_widget_ptr;
    if (button) aroma_widget_free(button);
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
        case EVENT_TYPE_MOUSE_ENTER:
            in_bounds = aroma_button_handle_mouse_event(event->target_node,
                event->data.mouse.x, event->data.mouse.y, false);
            break;
        case EVENT_TYPE_MOUSE_EXIT:
            if (btn->state != BUTTON_STATE_IDLE) {
                btn->state = BUTTON_STATE_IDLE;
                aroma_node_invalidate(event->target_node);
            }
            __button_request_redraw(user_data);
            return false;
        default:
            break;
    }

    if (btn->state != prev_state && user_data) __button_request_redraw(user_data);
    return in_bounds;
}

bool aroma_button_setup_events(AromaNode* button_node, void (*on_redraw_callback)(void*), void* user_data)
{
    if (!button_node) return false;
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_CLICK, __button_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __button_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_MOVE, __button_default_mouse_handler, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_ENTER, __button_default_mouse_handler, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(button_node->node_id, EVENT_TYPE_MOUSE_EXIT, __button_default_mouse_handler, (void*)on_redraw_callback, 80);
    return true;
}