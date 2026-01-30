#include "widgets/aroma_checkbox.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>

#define AROMA_CHECKBOX_LABEL_MAX 64

typedef struct AromaCheckbox {
    AromaRect rect;
    bool checked;
    bool is_hovered;
    bool is_pressed;
    uint32_t box_color;
    uint32_t border_color;
    uint32_t check_color;
    uint32_t hover_color;
    uint32_t text_color;
    float border_radius;
    AromaFont* font;
    char label[AROMA_CHECKBOX_LABEL_MAX];
    void (*on_change)(bool, void*);
    void* user_data;
    int box_size;
    int box_x;
    int box_y;
    int text_x;
    int text_y;
} AromaCheckbox;

static inline uint32_t __checkbox_lighten(uint32_t color, float amount)
{
    return aroma_color_blend(color, 0xFFFFFFu, amount);
}

static inline uint32_t __checkbox_darken(uint32_t color, float amount)
{
    return aroma_color_blend(color, 0x000000u, amount);
}

static void __checkbox_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

void aroma_checkbox_set_state(AromaNode* node, bool state);
bool aroma_checkbox_get_state(AromaNode* node);

static void __checkbox_update_layout(AromaCheckbox* checkbox)
{
    const int padding = 8;
    checkbox->box_size = checkbox->rect.height - padding;
    if (checkbox->box_size < 16) checkbox->box_size = 16;
    if (checkbox->box_size > checkbox->rect.height) checkbox->box_size = checkbox->rect.height;
    checkbox->box_x = checkbox->rect.x;
    checkbox->box_y = checkbox->rect.y + (checkbox->rect.height - checkbox->box_size) / 2;
    checkbox->text_x = checkbox->box_x + checkbox->box_size + padding;
    checkbox->text_y = checkbox->rect.y + (checkbox->rect.height - aroma_font_get_line_height(checkbox->font) - padding) / 2;
}

AromaNode* aroma_checkbox_create(AromaNode* parent, const char* label,
                                 int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid checkbox parameters");
        return NULL;
    }

    AromaCheckbox* data = (AromaCheckbox*)aroma_widget_alloc(sizeof(AromaCheckbox));
    if (!data) {
        LOG_ERROR("Failed to allocate checkbox");
        return NULL;
    }

    data->rect.x = x;
    data->rect.y = y;
    data->rect.width = width;
    data->rect.height = height;
    data->checked = false;
    data->is_hovered = false;
    data->is_pressed = false;
    AromaTheme theme = aroma_theme_get_global();
    data->box_color = theme.colors.surface;
    data->border_color = theme.colors.border;
    data->check_color = theme.colors.primary;
    data->hover_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.12f);
    data->text_color = theme.colors.text_primary;
    data->border_radius = 4.0f;
    data->font = NULL;
    data->on_change = NULL;
    data->user_data = NULL;

    if (label) {
        strncpy(data->label, label, AROMA_CHECKBOX_LABEL_MAX - 1);
        data->label[AROMA_CHECKBOX_LABEL_MAX - 1] = '\0';
    } else {
        data->label[0] = '\0';
    }

    data->box_size = 0;
    data->box_x = 0;
    data->box_y = 0;
    data->text_x = 0;
    data->text_y = 0;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node) {
        aroma_widget_free(data);
        LOG_ERROR("Failed to create checkbox node");
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_checkbox_draw);

    LOG_INFO("Checkbox created: label='%s'", data->label);

    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

void aroma_checkbox_set_checked(AromaNode* checkbox_node, bool checked)
{
    aroma_checkbox_set_state(checkbox_node, checked);
}

bool aroma_checkbox_is_checked(AromaNode* checkbox_node)
{
    return aroma_checkbox_get_state(checkbox_node);
}

void aroma_checkbox_set_callback(AromaNode* checkbox_node,
                                 void (*callback)(bool checked, void* user_data),
                                 void* user_data)
{
    if (!checkbox_node || !checkbox_node->node_widget_ptr) return;
    AromaCheckbox* data = (AromaCheckbox*)checkbox_node->node_widget_ptr;
    data->on_change = callback;
    data->user_data = user_data;
}

void aroma_checkbox_set_state(AromaNode* node, bool state)
{
    if (!node || !node->node_widget_ptr) return;
    if (aroma_node_is_hidden(node)) return;
    AromaCheckbox* data = (AromaCheckbox*)node->node_widget_ptr;
    if (data->checked != state) {
        data->checked = state;
        aroma_node_invalidate(node);
        if (data->on_change) {
            data->on_change(data->checked, data->user_data);
        }
    }
}

bool aroma_checkbox_get_state(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) return false;
    AromaCheckbox* data = (AromaCheckbox*)node->node_widget_ptr;
    return data->checked;
}

void aroma_checkbox_set_on_change(AromaNode* node,
                                  void (*callback)(bool, void*),
                                  void* user_data)
{
    if (!node || !node->node_widget_ptr) return;
    AromaCheckbox* data = (AromaCheckbox*)node->node_widget_ptr;
    data->on_change = callback;
    data->user_data = user_data;
}

void aroma_checkbox_set_font(AromaNode* node, AromaFont* font)
{
    if (!node || !node->node_widget_ptr) return;
    AromaCheckbox* data = (AromaCheckbox*)node->node_widget_ptr;
    data->font = font;
    __checkbox_update_layout(data);
    aroma_node_invalidate(node);
}

static void __checkbox_draw_checkmark(AromaGraphicsInterface* gfx, size_t window_id,
                                      int x, int y, int size, uint32_t color)
{
    int thickness = size / 6;
    if (thickness < 2) thickness = 2;

    int start_x = x + size / 4;
    int start_y = y + size / 2;
    int mid_x = x + size / 2;
    int mid_y = y + size - size / 4;
    int end_x = x + size - size / 6;
    int end_y = y + size / 4;

    int length1 = mid_x - start_x;
    for (int i = 0; i < thickness; ++i) {
        gfx->fill_rectangle(window_id,
                            start_x,
                            start_y + i,
                            length1,
                            thickness,
                            color,
                            false,
                            0.0f);
    }

    int length2 = end_x - mid_x;
    for (int i = 0; i < thickness; ++i) {
        gfx->fill_rectangle(window_id,
                            mid_x,
                            mid_y - i,
                            length2,
                            thickness,
                            color,
                            false,
                            0.0f);
    }
}

void aroma_checkbox_draw(AromaNode* node, size_t window_id)
{
    if (!node || !node->node_widget_ptr) return;
    AromaCheckbox* data = (AromaCheckbox*)node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (aroma_node_is_hidden(node)) return;
    if (!gfx) return;

    if (data->box_size == 0) __checkbox_update_layout(data);

    uint32_t base_color = data->box_color;
    if (data->is_hovered) base_color = __checkbox_lighten(base_color, 0.06f);
    uint32_t border_color = data->border_color;
    if (data->is_pressed) base_color = __checkbox_darken(base_color, 0.06f);

    gfx->fill_rectangle(window_id, data->box_x, data->box_y, data->box_size, data->box_size,
                        base_color, true, data->border_radius);
    gfx->draw_hollow_rectangle(window_id, data->box_x, data->box_y, data->box_size, data->box_size,
                               border_color, 1.0f, true, data->border_radius);

    if (data->checked) {
        uint32_t fill = data->check_color;
        gfx->fill_rectangle(window_id, data->box_x + 3, data->box_y + 3, data->box_size - 6, data->box_size - 6,
                            __checkbox_lighten(fill, 0.12f), true, 3.0f);
        __checkbox_draw_checkmark(gfx, window_id, data->box_x + 4, data->box_y + 4, data->box_size - 8, fill);
    }

    if (data->font && data->label[0] != '\0' && gfx->render_text) {
        gfx->render_text(window_id, data->font, data->label, data->text_x, data->text_y, data->text_color, 1.0f);
    }
}

static bool __checkbox_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaCheckbox* data = (AromaCheckbox*)event->target_node->node_widget_ptr;
    if (!data) return false;

    AromaRect* r = &data->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_ENTER:
            data->is_hovered = true;
            aroma_node_invalidate(event->target_node);
            __checkbox_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            data->is_hovered = false;
            data->is_pressed = false;
            aroma_node_invalidate(event->target_node);
            __checkbox_request_redraw(user_data);
            return false;
        case EVENT_TYPE_MOUSE_MOVE:
            if (data->is_hovered != in_bounds) {
                data->is_hovered = in_bounds;
                aroma_node_invalidate(event->target_node);
            }
            __checkbox_request_redraw(user_data);
            return in_bounds;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                data->is_pressed = true;
                aroma_node_invalidate(event->target_node);
                __checkbox_request_redraw(user_data);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (data->is_pressed) {
                data->is_pressed = false;
                if (in_bounds) {
                    aroma_checkbox_set_state(event->target_node, !data->checked);
                }
                aroma_node_invalidate(event->target_node);
                __checkbox_request_redraw(user_data);
                return in_bounds;
            }
            break;
        default:
            break;
    }

    return false;
}

bool aroma_checkbox_setup_events(AromaNode* node,
                                 void (*on_redraw_callback)(void*),
                                 void* user_data)
{
    if (!node) return false;
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __checkbox_handle_event, (void*)on_redraw_callback, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __checkbox_handle_event, (void*)on_redraw_callback, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, __checkbox_handle_event, (void*)on_redraw_callback, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __checkbox_handle_event, (void*)on_redraw_callback, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __checkbox_handle_event, (void*)on_redraw_callback, 70);
    return true;
}

void aroma_checkbox_destroy(AromaNode* node)
{
    if (!node) return;
    if (node->node_widget_ptr) {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
    __destroy_node(node);
}
