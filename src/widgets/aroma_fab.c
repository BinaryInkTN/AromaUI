#include "widgets/aroma_fab.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>

typedef struct AromaFAB {
    AromaRect rect;
    AromaFABSize size;
    char icon_text[32];
    char text[64];
    uint32_t bg_color;
    uint32_t icon_color;
    bool is_hovered;
    bool is_pressed;
    void (*click_callback)(void* user_data);
    void* user_data;
    AromaFont* font;
} AromaFAB;

static void __fab_request_redraw(void* user_data)
{
    if (!user_data) {
        return;
    }
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static bool __fab_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaFAB* fab = (AromaFAB*)event->target_node->node_widget_ptr;
    if (!fab) return false;

    AromaRect* r = &fab->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_ENTER:
            fab->is_hovered = true;
            aroma_node_invalidate(event->target_node);
            __fab_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            fab->is_hovered = false;
            fab->is_pressed = false;
            aroma_node_invalidate(event->target_node);
            __fab_request_redraw(user_data);
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                fab->is_pressed = true;
                aroma_node_invalidate(event->target_node);
                __fab_request_redraw(user_data);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (fab->is_pressed) {
                fab->is_pressed = false;
                if (in_bounds && fab->click_callback) {
                    fab->click_callback(fab->user_data);
                }
                aroma_node_invalidate(event->target_node);
                __fab_request_redraw(user_data);
                return in_bounds;
            }
            break;
        default:
            break;
    }

    return false;
}

static int get_fab_size(AromaFABSize size) {
    switch (size) {
        case FAB_SIZE_SMALL: return 40;
        case FAB_SIZE_NORMAL: return 56;
        case FAB_SIZE_LARGE: return 96;
        case FAB_SIZE_EXTENDED: return 56; // Height only
        default: return 56;
    }
}

AromaNode* aroma_fab_create(AromaNode* parent, int x, int y, AromaFABSize size, const char* icon_text) {
    if (!parent) return NULL;
    AromaFAB* fab = (AromaFAB*)aroma_widget_alloc(sizeof(AromaFAB));
    if (!fab) return NULL;

    int fab_size = get_fab_size(size);
    fab->rect.x = x;
    fab->rect.y = y;
    fab->rect.width = size == FAB_SIZE_EXTENDED ? 120 : fab_size;
    fab->rect.height = fab_size;
    fab->size = size;
    strncpy(fab->icon_text, icon_text ? icon_text : "+", sizeof(fab->icon_text) - 1);
    fab->text[0] = '\0';
    AromaTheme theme = aroma_theme_get_global();
    fab->bg_color = theme.colors.primary;
    fab->icon_color = 0xFFFFFF;
    fab->is_hovered = false;
    fab->is_pressed = false;
    fab->click_callback = NULL;
    fab->user_data = NULL;
    fab->font = NULL;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, fab);
    if (!node) {
        aroma_widget_free(fab);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_fab_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __fab_handle_event, aroma_ui_request_redraw, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __fab_handle_event, aroma_ui_request_redraw, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __fab_handle_event, aroma_ui_request_redraw, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __fab_handle_event, aroma_ui_request_redraw, 70);
    return node;
}

void aroma_fab_set_click_callback(AromaNode* fab_node, void (*callback)(void* user_data), void* user_data) {
    if (!fab_node) return;
    AromaFAB* fab = (AromaFAB*)fab_node->node_widget_ptr;
    if (!fab) return;
    fab->click_callback = callback;
    fab->user_data = user_data;
}

void aroma_fab_set_colors(AromaNode* fab_node, uint32_t bg_color, uint32_t icon_color) {
    if (!fab_node) return;
    AromaFAB* fab = (AromaFAB*)fab_node->node_widget_ptr;
    if (!fab) return;
    fab->bg_color = bg_color;
    fab->icon_color = icon_color;
}

void aroma_fab_set_font(AromaNode* fab_node, AromaFont* font) {
    if (!fab_node) return;
    AromaFAB* fab = (AromaFAB*)fab_node->node_widget_ptr;
    if (!fab) return;
    fab->font = font;
}

void aroma_fab_set_text(AromaNode* fab_node, const char* text) {
    if (!fab_node || !text) return;
    AromaFAB* fab = (AromaFAB*)fab_node->node_widget_ptr;
    if (!fab) return;
    strncpy(fab->text, text, sizeof(fab->text) - 1);
    fab->size = FAB_SIZE_EXTENDED;
    fab->rect.width = 120 + strlen(text) * 8;
}

void aroma_fab_draw(AromaNode* fab_node, size_t window_id) {
    if (!fab_node) return;
    AromaFAB* fab = (AromaFAB*)fab_node->node_widget_ptr;
    if (aroma_node_is_hidden(fab_node)) return;
    if (!fab) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    uint32_t color = fab->bg_color;
    if (fab->is_pressed) {
        color = 0x4F378B;  // Darker
    } else if (fab->is_hovered) {
        color = 0x7965B3;  // Lighter
    }

    // MD3 Elevation 3 shadow for FAB
    gfx->fill_rectangle(window_id, fab->rect.x + 2, fab->rect.y + 4,
                        fab->rect.width, fab->rect.height,
                        0xC0C0C0, true, fab->rect.height / 2.0f);

    // Draw FAB
    gfx->fill_rectangle(window_id, fab->rect.x, fab->rect.y,
                        fab->rect.width, fab->rect.height,
                        color, true, fab->rect.height / 2.0f);

    // Draw icon/text
    if (gfx->render_text && fab->font && fab->icon_text[0]) {
        int text_x = fab->rect.x + fab->rect.width / 2 - 8;
        int text_y = fab->rect.y + fab->rect.height / 2 + 8;
        gfx->render_text(window_id, fab->font, fab->icon_text, text_x, text_y, fab->icon_color);
    }
}

void aroma_fab_destroy(AromaNode* fab_node) {
    if (!fab_node) return;
    AromaFAB* fab = (AromaFAB*)fab_node->node_widget_ptr;
    if (fab) {
        aroma_widget_free(fab);
    }
}
