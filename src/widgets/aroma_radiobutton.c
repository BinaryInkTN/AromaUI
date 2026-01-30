#include "widgets/aroma_radiobutton.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>

#define AROMA_RADIO_LABEL_MAX 64
#define AROMA_RADIO_GROUP_MAX_ITEMS 32

typedef struct AromaRadioButton AromaRadioButton;

typedef struct AromaRadioGroup {
    AromaRadioButton* buttons[AROMA_RADIO_GROUP_MAX_ITEMS];
    size_t count;
    int selected_index;
    bool auto_layout;
} AromaRadioGroup;

struct AromaRadioButton {
    AromaRect rect;
    bool is_selected;
    bool is_hovered;
    bool is_pressed;
    AromaRadioGroup* group;
    AromaFont* font;
    uint32_t ring_color;
    uint32_t dot_color;
    uint32_t hover_color;
    uint32_t text_color;
    uint32_t background_color;
    int circle_diameter;
    int circle_x;
    int circle_y;
    int inner_diameter;
    int inner_x;
    int inner_y;
    float circle_radius;
    float inner_radius;
    char label[AROMA_RADIO_LABEL_MAX];
    void (*on_selected)(void*);
    void* user_data;
    int text_x;
    int text_y;
};

static inline uint32_t __radio_lighten(uint32_t color, float amount)
{
    return aroma_color_blend(color, 0xFFFFFFu, amount);
}

static inline uint32_t __radio_darken(uint32_t color, float amount)
{
    return aroma_color_blend(color, 0x000000u, amount);
}

static void __radio_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

AromaRadioGroup* aroma_radio_group_create(void);
void aroma_radio_button_set_selected(AromaNode* node, bool selected);
bool aroma_radio_button_get_selected(AromaNode* node);
void aroma_radio_button_set_font(AromaNode* node, AromaFont* font);
void aroma_radio_button_draw(AromaNode* node, size_t window_id);
void aroma_radio_button_destroy(AromaNode* node);

#define AROMA_RADIO_GROUP_MAX 32
static AromaRadioGroup* g_radio_groups[AROMA_RADIO_GROUP_MAX];

static AromaRadioGroup* __radio_group_get(int group_id)
{
    if (group_id < 0 || group_id >= AROMA_RADIO_GROUP_MAX) return NULL;
    if (!g_radio_groups[group_id]) {
        g_radio_groups[group_id] = aroma_radio_group_create();
    }
    return g_radio_groups[group_id];
}

AromaRadioGroup* aroma_radio_group_create(void)
{
    AromaRadioGroup* group = (AromaRadioGroup*)calloc(1, sizeof(AromaRadioGroup));
    if (group) {
        group->count = 0;
        group->selected_index = -1;
        group->auto_layout = false;
    }
    return group;
}

void aroma_radio_group_destroy(AromaRadioGroup* group)
{
    if (!group) return;
    free(group);
}

static void __radio_group_add(AromaRadioGroup* group, AromaRadioButton* button)
{
    if (!group || !button) return;
    if (group->count >= AROMA_RADIO_GROUP_MAX_ITEMS) {
        LOG_WARNING("Radio group full; button not registered");
        return;
    }
    group->buttons[group->count] = button;
    if (button->is_selected) {
        group->selected_index = (int)group->count;
    }
    group->count++;
}

static void __radio_group_deselect_others(AromaRadioGroup* group, AromaRadioButton* current)
{
    if (!group) return;
    for (size_t i = 0; i < group->count; ++i) {
        AromaRadioButton* item = group->buttons[i];
        if (!item) continue;
        if (item != current && item->is_selected) {
            item->is_selected = false;
        }
        if (item == current) {
            group->selected_index = (int)i;
        }
    }
}

static void __radiobutton_update_layout(AromaRadioButton* data)
{
    int diameter = data->rect.height - 8;
    if (diameter < 16) diameter = 16;
    data->circle_diameter = diameter;
    data->circle_x = data->rect.x;
    data->circle_y = data->rect.y + (data->rect.height - diameter) / 2;
    data->circle_radius = (float)diameter / 2.0f;
    data->inner_diameter = diameter - 8;
    data->inner_x = data->circle_x + (diameter - data->inner_diameter) / 2;
    data->inner_y = data->circle_y + (diameter - data->inner_diameter) / 2;
    data->inner_radius = (float)data->inner_diameter / 2.0f;
    data->text_x = data->circle_x + diameter + 10;
    int font_h = aroma_font_get_line_height(data->font);

    data->text_y =
        data->circle_y +
        (data->circle_diameter - font_h) / 2;
}

AromaNode* aroma_radio_button_create(AromaNode* parent, AromaRadioGroup* group,
                                     const char* label, int x, int y,
                                     int width, int height, bool selected)
{
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid radio button parameters");
        return NULL;
    }

    AromaRadioButton* data = (AromaRadioButton*)aroma_widget_alloc(sizeof(AromaRadioButton));
    if (!data) {
        LOG_ERROR("Failed to allocate radio button");
        return NULL;
    }

    data->rect.x = x;
    data->rect.y = y;
    data->rect.width = width;
    data->rect.height = height;
    data->is_selected = selected;
    data->is_hovered = false;
    data->is_pressed = false;
    data->group = group;
    data->font = NULL;
    AromaTheme theme = aroma_theme_get_global();
    data->ring_color = theme.colors.border;
    data->dot_color = theme.colors.primary;
    data->hover_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.12f);
    data->text_color = theme.colors.text_primary;
    data->background_color = theme.colors.surface;
    data->on_selected = NULL;
    data->user_data = NULL;

    if (label) {
        strncpy(data->label, label, AROMA_RADIO_LABEL_MAX - 1);
        data->label[AROMA_RADIO_LABEL_MAX - 1] = '\0';
    } else {
        data->label[0] = '\0';
    }

    data->circle_diameter = 0;
    data->circle_x = 0;
    data->circle_y = 0;
    data->inner_diameter = 0;
    data->inner_x = 0;
    data->inner_y = 0;
    data->circle_radius = 0.0f;
    data->inner_radius = 0.0f;
    data->text_x = 0;
    data->text_y = 0;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node) {
        aroma_widget_free(data);
        LOG_ERROR("Failed to create radio button node");
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_radiobutton_draw);

    if (group) {
        __radio_group_add(group, data);
        if (group->selected_index == -1 && selected) {
            group->selected_index = (int)(group->count - 1);
        } else if (selected) {
            __radio_group_deselect_others(group, data);
        }
    }

    LOG_INFO("Radio button created: label='%s'", data->label);
    __radiobutton_update_layout(data);

    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

AromaNode* aroma_radiobutton_create(AromaNode* parent, const char* label,
                                    int x, int y, int width, int height, int group_id)
{
    AromaRadioGroup* group = __radio_group_get(group_id);
    return aroma_radio_button_create(parent, group, label, x, y, width, height, false);
}

void aroma_radiobutton_set_selected(AromaNode* radio_node, bool selected)
{
    aroma_radio_button_set_selected(radio_node, selected);
}

bool aroma_radiobutton_is_selected(AromaNode* radio_node)
{
    return aroma_radio_button_get_selected(radio_node);
}

void aroma_radiobutton_set_callback(AromaNode* radio_node,
                                    void (*callback)(void* user_data),
                                    void* user_data)
{
    if (!radio_node || !radio_node->node_widget_ptr) return;
    AromaRadioButton* data = (AromaRadioButton*)radio_node->node_widget_ptr;
    data->on_selected = callback;
    data->user_data = user_data;
}

void aroma_radiobutton_set_font(AromaNode* radio_node, AromaFont* font)
{
    aroma_radio_button_set_font(radio_node, font);
}

void aroma_radiobutton_draw(AromaNode* radio_node, size_t window_id)
{
    aroma_radio_button_draw(radio_node, window_id);
}

void aroma_radiobutton_destroy(AromaNode* radio_node)
{
    aroma_radio_button_destroy(radio_node);
}

void aroma_radio_button_set_selected(AromaNode* node, bool selected)
{
    if (!node || !node->node_widget_ptr) return;
    if (aroma_node_is_hidden(node)) return;
    AromaRadioButton* data = (AromaRadioButton*)node->node_widget_ptr;
    if (data->is_selected == selected) return;
    data->is_selected = selected;
    if (selected && data->group) {
        __radio_group_deselect_others(data->group, data);
    } else if (data->group && !selected && data->group->selected_index >= 0) {
        AromaRadioButton* current = data->group->buttons[data->group->selected_index];
        if (current == data) {
            data->group->selected_index = -1;
        }
    }
    aroma_node_invalidate(node);
    if (selected && data->on_selected) {
        data->on_selected(data->user_data);
    }
}

bool aroma_radio_button_get_selected(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) return false;
    AromaRadioButton* data = (AromaRadioButton*)node->node_widget_ptr;
    return data->is_selected;
}

void aroma_radio_button_set_on_selected(AromaNode* node,
                                        void (*callback)(void*),
                                        void* user_data)
{
    if (!node || !node->node_widget_ptr) return;
    AromaRadioButton* data = (AromaRadioButton*)node->node_widget_ptr;
    data->on_selected = callback;
    data->user_data = user_data;
}

void aroma_radio_button_set_font(AromaNode* node, AromaFont* font)
{
    if (!node || !node->node_widget_ptr) return;
    AromaRadioButton* data = (AromaRadioButton*)node->node_widget_ptr;
    data->font = font;
    __radiobutton_update_layout(data);
    aroma_node_invalidate(node);
}

void aroma_radio_button_draw(AromaNode* node, size_t window_id)
{
    if (!node || !node->node_widget_ptr) return;
    AromaRadioButton* data = (AromaRadioButton*)node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (aroma_node_is_hidden(node)) return;
    if (!gfx) return;

    uint32_t ring_color = data->ring_color;
    if (data->is_hovered) {
        ring_color = __radio_lighten(ring_color, 0.12f);
    }

    gfx->fill_rectangle(window_id, data->circle_x, data->circle_y, data->circle_diameter, data->circle_diameter,
                        data->background_color, true, data->circle_radius);
    gfx->draw_hollow_rectangle(window_id, data->circle_x, data->circle_y, data->circle_diameter, data->circle_diameter,
                               ring_color, 1.0f, true, data->circle_radius);

    if (data->is_selected) {
        gfx->fill_rectangle(window_id, data->inner_x, data->inner_y, data->inner_diameter, data->inner_diameter,
                            data->dot_color, true, data->inner_radius);
    }

    if (data->font && data->label[0] != '\0' && gfx->render_text) {
        gfx->render_text(window_id, data->font, data->label, data->text_x, data->text_y, data->text_color, 1.0f);
    }
}

static bool __radio_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaRadioButton* data = (AromaRadioButton*)event->target_node->node_widget_ptr;
    if (!data) return false;

    AromaRect* r = &data->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_ENTER:
            data->is_hovered = true;
            aroma_node_invalidate(event->target_node);
            __radio_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            data->is_hovered = false;
            data->is_pressed = false;
            aroma_node_invalidate(event->target_node);
            __radio_request_redraw(user_data);
            return false;
        case EVENT_TYPE_MOUSE_MOVE:
            if (data->is_hovered != in_bounds) {
                data->is_hovered = in_bounds;
                aroma_node_invalidate(event->target_node);
            }
            __radio_request_redraw(user_data);
            return in_bounds;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                data->is_pressed = true;
                aroma_node_invalidate(event->target_node);
                __radio_request_redraw(user_data);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (data->is_pressed) {
                data->is_pressed = false;
                if (in_bounds && !data->is_selected) {
                    aroma_radio_button_set_selected(event->target_node, true);
                }
                aroma_node_invalidate(event->target_node);
                __radio_request_redraw(user_data);
                return in_bounds;
            }
            break;
        default:
            break;
    }

    return false;
}

bool aroma_radio_button_setup_events(AromaNode* node,
                                     void (*on_redraw_callback)(void*),
                                     void* user_data)
{
    if (!node) return false;

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __radio_handle_event, (void*)on_redraw_callback, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __radio_handle_event, (void*)on_redraw_callback, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, __radio_handle_event, (void*)on_redraw_callback, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __radio_handle_event, (void*)on_redraw_callback, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __radio_handle_event, (void*)on_redraw_callback, 70);

    return true;
}

void aroma_radio_button_destroy(AromaNode* node)
{
    if (!node) return;

    AromaRadioButton* data = (AromaRadioButton*)node->node_widget_ptr;
    if (data && data->group) {
        for (size_t i = 0; i < data->group->count; ++i) {
            if (data->group->buttons[i] == data) {
                for (size_t j = i; j + 1 < data->group->count; ++j) {
                    data->group->buttons[j] = data->group->buttons[j + 1];
                }
                data->group->count--;
                data->group->buttons[data->group->count] = NULL;
                if (data->group->selected_index == (int)i) {
                    data->group->selected_index = -1;
                }
                break;
            }
        }
    }

    if (node->node_widget_ptr) {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }

    __destroy_node(node);
}
