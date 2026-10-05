#include "widgets/aroma_switch.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "widgets/aroma_container.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>
#include "aroma_dp.h"
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_SWITCH_INSET_DP 2
#define AROMA_SWITCH_BORDER_WIDTH_DP 2

#ifdef __ANDROID__
static inline int sw_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float sw_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int sw_dp(int dp) { return dp; }
static inline float sw_dp_f(float dp) { return dp; }
#endif

AromaNode *aroma_switch_create(AromaNode *parent, int x, int y, int width, int height, bool initial_state)
{
    if (!parent)
    {
        LOG_ERROR("Cannot create switch without parent\n");
        return NULL;
    }

#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif

    AromaSwitch *data = (AromaSwitch *)aroma_widget_alloc(sizeof(AromaSwitch));
    if (!data)
    {
        LOG_ERROR("Failed to allocate switch data\n");
        return NULL;
    }

    data->rect.x = x;
    data->rect.y = y;
    data->rect.width = width;
    data->rect.height = height;
    data->state = initial_state;
    AromaTheme theme = aroma_theme_get_global();
    data->color_on = theme.colors.primary;
    data->color_off = theme.colors.border;
    data->is_hovered = false;

    data->track_radius = (float)data->rect.height / 2.0f;
    data->toggle_size = data->rect.height - sw_dp(AROMA_SWITCH_INSET_DP * 2);
    data->toggle_radius = (float)data->toggle_size / 2.0f;
    data->border_color = 0x333333;
    data->toggle_x = data->state ? (data->rect.x + data->rect.width - data->toggle_size - sw_dp(AROMA_SWITCH_INSET_DP)) : (data->rect.x + sw_dp(AROMA_SWITCH_INSET_DP));

    data->use_theme_colors = true;
    data->on_change = NULL;
    data->user_data = NULL;
    data->active_pointer_id = -1;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node)
    {
        LOG_ERROR("Failed to create switch node\n");
        aroma_widget_free(data);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_switch_draw);

    LOG_INFO("Switch created: x=%d, y=%d, w=%d, h=%d, state=%s\n",
             x, y, width, height, initial_state ? "ON" : "OFF");

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_switch_set_state(AromaNode *node, bool state)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaSwitch *data = (AromaSwitch *)node->node_widget_ptr;
    if (data->state != state)
    {
        data->state = state;
        data->toggle_x = data->state ? (data->rect.x + data->rect.width - data->toggle_size - sw_dp(AROMA_SWITCH_INSET_DP)) : (data->rect.x + sw_dp(AROMA_SWITCH_INSET_DP));
        if (data->on_change)
        {
            data->on_change(node, data->user_data);
        }
    }
}

bool aroma_switch_get_state(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return false;
    AromaSwitch *data = (AromaSwitch *)node->node_widget_ptr;
    return data->state;
}

void aroma_switch_set_on_change(AromaNode *node, bool (*callback)(AromaNode *, void *), void *user_data)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaSwitch *data = (AromaSwitch *)node->node_widget_ptr;
    data->on_change = callback;
    data->user_data = user_data;
    LOG_INFO("Switch on_change callback registered\n");
}

void aroma_switch_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(node))
        return;
    AromaSwitch *data = (AromaSwitch *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    if (data->use_theme_colors)
    {
        AromaTheme theme = aroma_theme_get_global();
        data->color_on = theme.colors.primary;
        data->color_off = theme.colors.border;
    }

    data->toggle_x = data->state
                         ? (data->rect.x + data->rect.width - data->toggle_size - sw_dp(AROMA_SWITCH_INSET_DP))
                         : (data->rect.x + sw_dp(AROMA_SWITCH_INSET_DP));

    uint32_t bg_color = data->state ? data->color_on : data->color_off;
    if (data->is_hovered)
    {
        bg_color = data->state ? aroma_color_adjust(data->color_on, 0.08f)
                               : aroma_color_adjust(data->color_off, 0.08f);
    }
    gfx->fill_rectangle(window_id, data->rect.x, data->rect.y, data->rect.width, data->rect.height, bg_color, true, data->track_radius);

    gfx->draw_hollow_rectangle(window_id, data->rect.x, data->rect.y, data->rect.width, data->rect.height,
                               data->border_color, sw_dp(AROMA_SWITCH_BORDER_WIDTH_DP), true, data->track_radius);

    int toggle_y = data->rect.y + sw_dp(AROMA_SWITCH_INSET_DP);
    uint32_t toggle_color = 0xFFFFFF;
    gfx->fill_rectangle(window_id, data->toggle_x, toggle_y, data->toggle_size, data->toggle_size, toggle_color, true, data->toggle_radius);
}

void aroma_switch_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr)
    {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}

static bool __switch_default_mouse_handler(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node)
        return false;
    AromaSwitch *sw = (AromaSwitch *)event->target_node->node_widget_ptr;
    if (!sw)
        return false;
 int adjusted_x = event->data.mouse.x;
    int adjusted_y = event->data.mouse.y;
    if (event->event_type == EVENT_TYPE_TOUCH_DOWN ||
        event->event_type == EVENT_TYPE_TOUCH_UP ||
        event->event_type == EVENT_TYPE_TOUCH_MOVE) {
        adjusted_x = event->data.touch.x;
        adjusted_y = event->data.touch.y;
    }
                          AromaNode *cur = event->target_node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int scroll_x, scroll_y;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            adjusted_x += scroll_x;
            adjusted_y += scroll_y;
        }
        cur = cur->parent_node;
    }
    

    if (event->event_type == EVENT_TYPE_MOUSE_MOVE || event->event_type == EVENT_TYPE_MOUSE_ENTER)
    {
        bool hover = (adjusted_x >= sw->rect.x && adjusted_x <= sw->rect.x + sw->rect.width &&
                      adjusted_y >= sw->rect.y && adjusted_y <= sw->rect.y + sw->rect.height);
        if (sw->is_hovered != hover)
        {
            sw->is_hovered = false;
            if (user_data)
            {
                void (*on_redraw)(void *) = (void (*)(void *))user_data;
                on_redraw(NULL);
            }
        }
        return hover;
    }

    if (event->event_type == EVENT_TYPE_MOUSE_EXIT)
    {
        if (sw->is_hovered)
        {
            sw->is_hovered = false;
            if (user_data)
            {
                void (*on_redraw)(void *) = (void (*)(void *))user_data;
                on_redraw(NULL);
            }
        }
        return false;
    }

    switch (event->event_type)
    {
    case EVENT_TYPE_TOUCH_DOWN: {
        bool in_b = adjusted_x >= sw->rect.x &&
            adjusted_x <= sw->rect.x + sw->rect.width &&
            adjusted_y >= sw->rect.y &&
            adjusted_y <= sw->rect.y + sw->rect.height;
        if (in_b && sw->active_pointer_id == -1) {
            sw->active_pointer_id = event->data.touch.id;
            aroma_node_invalidate(event->target_node);
            return true;
        }
        return false;
    }
    case EVENT_TYPE_TOUCH_UP: {
        if (sw->active_pointer_id != event->data.touch.id)
            return false;
        sw->active_pointer_id = -1;
        bool in_b = adjusted_x >= sw->rect.x &&
            adjusted_x <= sw->rect.x + sw->rect.width &&
            adjusted_y >= sw->rect.y &&
            adjusted_y <= sw->rect.y + sw->rect.height;
        if (in_b) {
            aroma_switch_set_state(event->target_node, !sw->state);
            if (user_data) {
                void (*on_redraw)(void *) = (void (*)(void *))user_data;
                on_redraw(NULL);
            }
            return true;
        }
        aroma_node_invalidate(event->target_node);
        return true;
    }
    case EVENT_TYPE_TOUCH_MOVE:
        return sw->active_pointer_id != -1;
    case EVENT_TYPE_MOUSE_CLICK:
        if (sw->active_pointer_id != -1)
            return false;
        if (adjusted_x >= sw->rect.x &&
            adjusted_x <= sw->rect.x + sw->rect.width &&
            adjusted_y >= sw->rect.y &&
            adjusted_y <= sw->rect.y + sw->rect.height)
        {
            aroma_switch_set_state(event->target_node, !sw->state);
            if (user_data)
            {
                void (*on_redraw)(void *) = (void (*)(void *))user_data;
                on_redraw(NULL);
            }
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

bool aroma_switch_setup_events(AromaNode *switch_node, void (*on_redraw_callback)(void *), void *user_data)
{
    if (!switch_node)
        return false;
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_MOUSE_CLICK, __switch_default_mouse_handler, (void *)on_redraw_callback, 100);
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_MOUSE_MOVE, __switch_default_mouse_handler, (void *)on_redraw_callback, 80);
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_MOUSE_ENTER, __switch_default_mouse_handler, (void *)on_redraw_callback, 80);
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_MOUSE_EXIT, __switch_default_mouse_handler, (void *)on_redraw_callback, 80);
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_TOUCH_DOWN, __switch_default_mouse_handler, (void *)on_redraw_callback, 100);
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_TOUCH_UP, __switch_default_mouse_handler, (void *)on_redraw_callback, 100);
    aroma_event_subscribe(switch_node->node_id, EVENT_TYPE_TOUCH_MOVE, __switch_default_mouse_handler, (void *)on_redraw_callback, 80);
    return true;
}
