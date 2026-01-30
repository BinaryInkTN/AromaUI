#include "widgets/aroma_chip.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>

typedef struct AromaChip {
    AromaRect rect;
    char label[64];
    AromaChipType type;
    bool selected;
    bool is_hovered;
    bool is_pressed;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t selected_color;
    float border_radius;
    void (*callback)(void* user_data);
    void* user_data;
    AromaFont* font;
    int text_x;
    int text_y;
} AromaChip;

static void __chip_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static bool __chip_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaChip* chip = (AromaChip*)event->target_node->node_widget_ptr;
    if (!chip) return false;

    AromaRect* r = &chip->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_ENTER:
            chip->is_hovered = true;
            aroma_node_invalidate(event->target_node);
            __chip_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            chip->is_hovered = false;
            chip->is_pressed = false;
            aroma_node_invalidate(event->target_node);
            __chip_request_redraw(user_data);
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                chip->is_pressed = true;
                aroma_node_invalidate(event->target_node);
                __chip_request_redraw(user_data);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (chip->is_pressed) {
                chip->is_pressed = false;
                if (in_bounds) {
                    if (chip->type == CHIP_TYPE_FILTER) {
                        chip->selected = !chip->selected;
                    }
                    if (chip->callback) {
                        chip->callback(chip->user_data);
                    }
                }
                aroma_node_invalidate(event->target_node);
                __chip_request_redraw(user_data);
                return in_bounds;
            }
            break;
        default:
            break;
    }

    return false;
}

static void __chip_update_layout(AromaChip* chip)
{
    chip->rect.width = 80 + (chip->label ? strlen(chip->label) * 7 : 0);
    chip->rect.height = 32;
    chip->border_radius = 8.0f;
    chip->text_x = chip->rect.x + 12;
    chip->text_y = chip->rect.y + chip->rect.height / 2 + 8;
}

AromaNode* aroma_chip_create(AromaNode* parent, int x, int y, const char* label, AromaChipType type) {
    if (!parent) return NULL;
    AromaChip* chip = (AromaChip*)aroma_widget_alloc(sizeof(AromaChip));
    if (!chip) return NULL;

    chip->rect.x = x;
    chip->rect.y = y;
    strncpy(chip->label, label ? label : "Chip", sizeof(chip->label) - 1);
    chip->type = type;
    chip->selected = false;
    chip->is_hovered = false;
    chip->is_pressed = false;
    AromaTheme theme = aroma_theme_get_global();
    chip->bg_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.12f);
    chip->text_color = theme.colors.text_primary;
    chip->selected_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.35f);
    chip->callback = NULL;
    chip->user_data = NULL;
    chip->font = NULL;
    chip->text_x = 0;
    chip->text_y = 0;

    __chip_update_layout(chip);

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, chip);
    if (!node) {
        aroma_widget_free(chip);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_chip_draw);

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __chip_handle_event, aroma_ui_request_redraw, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __chip_handle_event, aroma_ui_request_redraw, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __chip_handle_event, aroma_ui_request_redraw, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __chip_handle_event, aroma_ui_request_redraw, 70);

    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

void aroma_chip_set_callback(AromaNode* chip_node, void (*callback)(void* user_data), void* user_data) {
    if (!chip_node) return;
    AromaChip* chip = (AromaChip*)chip_node->node_widget_ptr;
    if (!chip) return;
    chip->callback = callback;
    chip->user_data = user_data;
}

void aroma_chip_set_selected(AromaNode* chip_node, bool selected) {
    if (!chip_node) return;
    AromaChip* chip = (AromaChip*)chip_node->node_widget_ptr;
    if (!chip) return;
    chip->selected = selected;
    aroma_node_invalidate(chip_node);
}

void aroma_chip_set_font(AromaNode* chip_node, AromaFont* font) {
    if (!chip_node) return;
    AromaChip* chip = (AromaChip*)chip_node->node_widget_ptr;
    if (!chip) return;
    chip->font = font;
}

void aroma_chip_draw(AromaNode* chip_node, size_t window_id) {
    if (!chip_node) return;
    AromaChip* chip = (AromaChip*)chip_node->node_widget_ptr;
    if (aroma_node_is_hidden(chip_node)) return;
    if (!chip) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    uint32_t bg_color = chip->selected ? chip->selected_color : chip->bg_color;
    if (chip->is_pressed) {
        bg_color = 0xD0C8DC;
    } else if (chip->is_hovered) {
        bg_color = chip->selected ? 0xDDD5E9 : 0xDDD5E9;
    }

    gfx->fill_rectangle(window_id, chip->rect.x, chip->rect.y,
                        chip->rect.width, chip->rect.height,
                        bg_color, true, chip->border_radius);

    if (chip->type == CHIP_TYPE_FILTER || chip->type == CHIP_TYPE_INPUT) {
        gfx->draw_hollow_rectangle(window_id, chip->rect.x, chip->rect.y,
                                   chip->rect.width, chip->rect.height,
                                   0x79747E, 1, true, chip->border_radius);
    }

    if (gfx->render_text && chip->font && chip->label[0]) {
        gfx->render_text(window_id, chip->font, chip->label, chip->text_x, chip->text_y, chip->text_color, 1.0f);
    }
}

void aroma_chip_destroy(AromaNode* chip_node) {
    if (!chip_node) return;
    AromaChip* chip = (AromaChip*)chip_node->node_widget_ptr;
    if (chip) aroma_widget_free(chip);
}
