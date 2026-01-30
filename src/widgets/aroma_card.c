#include "widgets/aroma_card.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>

typedef struct AromaCard {
    AromaRect rect;
    AromaCardType type;
    uint32_t bg_color;
    uint32_t border_color;
    float border_radius;
    uint32_t shadow_color;
    bool is_hovered;
    bool is_pressed;
    void (*click_callback)(void* user_data);
    void* user_data;
} AromaCard;

static void __card_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static bool __card_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaCard* card = (AromaCard*)event->target_node->node_widget_ptr;
    if (!card) return false;

    AromaRect* r = &card->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_ENTER:
            card->is_hovered = true;
            aroma_node_invalidate(event->target_node);
            __card_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            card->is_hovered = false;
            card->is_pressed = false;
            aroma_node_invalidate(event->target_node);
            __card_request_redraw(user_data);
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                card->is_pressed = true;
                aroma_node_invalidate(event->target_node);
                __card_request_redraw(user_data);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (card->is_pressed) {
                card->is_pressed = false;
                if (in_bounds && card->click_callback) {
                    card->click_callback(card->user_data);
                }
                aroma_node_invalidate(event->target_node);
                __card_request_redraw(user_data);
                return in_bounds;
            }
            break;
        default:
            break;
    }

    return false;
}

AromaNode* aroma_card_create(AromaNode* parent, int x, int y, int width, int height, AromaCardType type) {
    if (!parent) return NULL;
    AromaCard* card = (AromaCard*)aroma_widget_alloc(sizeof(AromaCard));
    if (!card) return NULL;

    card->rect.x = x;
    card->rect.y = y;
    card->rect.width = width;
    card->rect.height = height;
    card->type = type;
    AromaTheme theme = aroma_theme_get_global();
    card->bg_color = type == CARD_TYPE_FILLED
        ? aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f)
        : theme.colors.surface;
    card->border_color = theme.colors.border;
    card->border_radius = 12.0f;
    card->shadow_color = 0xE0E0E0;
    card->is_hovered = false;
    card->is_pressed = false;
    card->click_callback = NULL;
    card->user_data = NULL;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, card);
    if (!node) {
        aroma_widget_free(card);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_card_draw);

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __card_handle_event, aroma_ui_request_redraw, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __card_handle_event, aroma_ui_request_redraw, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __card_handle_event, aroma_ui_request_redraw, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __card_handle_event, aroma_ui_request_redraw, 70);
    
    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

void aroma_card_set_colors(AromaNode* card_node, uint32_t bg_color, uint32_t border_color) {
    if (!card_node) return;
    AromaCard* card = (AromaCard*)card_node->node_widget_ptr;
    if (!card) return;
    card->bg_color = bg_color;
    card->border_color = border_color;
}

void aroma_card_set_click_callback(AromaNode* card_node, void (*callback)(void* user_data), void* user_data) {
    if (!card_node) return;
    AromaCard* card = (AromaCard*)card_node->node_widget_ptr;
    if (!card) return;
    card->click_callback = callback;
    card->user_data = user_data;
}

void aroma_card_draw(AromaNode* card_node, size_t window_id) {
    if (!card_node) return;
    AromaCard* card = (AromaCard*)card_node->node_widget_ptr;
    if (aroma_node_is_hidden(card_node)) return;
    if (!card) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    if (card->type == CARD_TYPE_ELEVATED) {
        gfx->fill_rectangle(window_id, card->rect.x + 2, card->rect.y + 4,
                          card->rect.width, card->rect.height,
                          card->shadow_color, true, card->border_radius);
    }

    gfx->fill_rectangle(window_id, card->rect.x, card->rect.y,
                        card->rect.width, card->rect.height,
                        card->bg_color, true, card->border_radius);

    if (card->type == CARD_TYPE_OUTLINED) {
        gfx->draw_hollow_rectangle(window_id, card->rect.x, card->rect.y,
                                   card->rect.width, card->rect.height,
                                   card->border_color, 1, true, card->border_radius);
    }
}

void aroma_card_destroy(AromaNode* card_node) {
    if (!card_node) return;
    AromaCard* card = (AromaCard*)card_node->node_widget_ptr;
    if (card) aroma_widget_free(card);
}
