#include "widgets/aroma_listview.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_LIST_MAX_ITEMS 64

typedef struct AromaListView {
    AromaRect rect;
    AromaListItem items[AROMA_LIST_MAX_ITEMS];
    size_t item_count;
    int selected_index;
    AromaFont* font;
    void (*callback)(int index, void* user_data);
    void* user_data;
} AromaListView;

static bool __listview_handle_event(AromaEvent* event, void* user_data)
{
    (void)user_data;
    if (!event || !event->target_node) return false;
    AromaListView* list = (AromaListView*)event->target_node->node_widget_ptr;
    if (!list) return false;

    if (event->event_type != EVENT_TYPE_MOUSE_CLICK) return false;
    int rel_y = event->data.mouse.y - list->rect.y;
    if (rel_y < 0 || rel_y >= list->rect.height) return false;

    int item_height = 28;
    int index = rel_y / item_height;
    if (index >= 0 && index < (int)list->item_count) {
        list->selected_index = index;
        if (list->callback) list->callback(index, list->user_data);
        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);
        return true;
    }
    return false;
}

AromaNode* aroma_listview_create(AromaNode* parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) return NULL;
    AromaListView* list = (AromaListView*)aroma_widget_alloc(sizeof(AromaListView));
    if (!list) return NULL;

    memset(list, 0, sizeof(AromaListView));
    list->rect.x = x;
    list->rect.y = y;
    list->rect.width = width;
    list->rect.height = height;
    list->item_count = 0;
    list->selected_index = -1;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, list);
    if (!node) {
        aroma_widget_free(list);
        return NULL;
    }

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __listview_handle_event, NULL, 80);
    return node;
}

void aroma_listview_add_item(AromaNode* list_node, const char* text, const char* secondary_text, void* user_data)
{
    if (!list_node || !list_node->node_widget_ptr || !text) return;
    AromaListView* list = (AromaListView*)list_node->node_widget_ptr;
    if (list->item_count >= AROMA_LIST_MAX_ITEMS) return;

    AromaListItem* item = &list->items[list->item_count++];
    strncpy(item->text, text, sizeof(item->text) - 1);
    if (secondary_text) strncpy(item->secondary_text, secondary_text, sizeof(item->secondary_text) - 1);
    item->user_data = user_data;
    aroma_node_invalidate(list_node);
}

void aroma_listview_clear(AromaNode* list_node)
{
    if (!list_node || !list_node->node_widget_ptr) return;
    AromaListView* list = (AromaListView*)list_node->node_widget_ptr;
    list->item_count = 0;
    list->selected_index = -1;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_callback(AromaNode* list_node, void (*callback)(int index, void* user_data), void* user_data)
{
    if (!list_node || !list_node->node_widget_ptr) return;
    AromaListView* list = (AromaListView*)list_node->node_widget_ptr;
    list->callback = callback;
    list->user_data = user_data;
}

void aroma_listview_set_font(AromaNode* list_node, AromaFont* font)
{
    if (!list_node || !list_node->node_widget_ptr) return;
    AromaListView* list = (AromaListView*)list_node->node_widget_ptr;
    list->font = font;
    aroma_node_invalidate(list_node);
}

void aroma_listview_draw(AromaNode* list_node, size_t window_id)
{
    if (!list_node || !list_node->node_widget_ptr) return;
    AromaListView* list = (AromaListView*)list_node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    AromaTheme theme = aroma_theme_get_global();

    gfx->fill_rectangle(window_id, list->rect.x, list->rect.y, list->rect.width, list->rect.height,
                        theme.colors.surface, true, 8.0f);
    if (aroma_node_is_hidden(list_node)) return;

    int item_height = 28;
    for (size_t i = 0; i < list->item_count; ++i) {
        int y = list->rect.y + (int)i * item_height;
        if (y + item_height > list->rect.y + list->rect.height) break;

        if ((int)i == list->selected_index) {
            gfx->fill_rectangle(window_id, list->rect.x + 2, y, list->rect.width - 4, item_height,
                                aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.2f), true, 6.0f);
        }

        if (list->font && gfx->render_text) {
            gfx->render_text(window_id, list->font, list->items[i].text, list->rect.x + 12, y + 18, theme.colors.text_primary);
        }
    }
}

void aroma_listview_destroy(AromaNode* list_node)
{
    if (!list_node) return;
    if (list_node->node_widget_ptr) {
        aroma_widget_free(list_node->node_widget_ptr);
        list_node->node_widget_ptr = NULL;
    }
    __destroy_node(list_node);
}