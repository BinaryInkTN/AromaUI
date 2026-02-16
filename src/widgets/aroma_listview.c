#include "widgets/aroma_listview.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_LIST_MAX_ITEMS 64

typedef struct AromaListView
{
    AromaRect rect;
    AromaListItem items[AROMA_LIST_MAX_ITEMS];
    size_t item_count;
    int selected_index;
    AromaFont *font;
    AromaFont *icon_font;
    void (*callback)(int index, void *user_data);
    void *user_data;
    int item_height;
    float corner_radius;
    float selected_corner_radius;
    float text_scale;
    int active_pointer_id;
} AromaListView;

static void __get_abs_pos(AromaNode* node, int* x, int* y)
{
    if (!node || !x || !y) return;
    *x = 0;
    *y = 0;
    AromaNode* curr = node;
    while (curr)
    {
        *x += curr->x;
        *y += curr->y;
        curr = curr->parent_node;
    }
}

static AromaRect __get_real_bounds(AromaNode* node)
{
    AromaRect bounds = {0, 0, 0, 0};
    if (!node) return bounds;
    __get_abs_pos(node, &bounds.x, &bounds.y);
    bounds.width = node->width;
    bounds.height = node->height;
    return bounds;
}

static bool __listview_handle_event(AromaEvent *event, void *user_data)
{
    (void)user_data;

    if (!event || !event->target_node)
        return false;

    AromaListView *list =
        (AromaListView *)event->target_node->node_widget_ptr;

    if (!list)
        return false;

    int x = 0, y = 0;
    bool is_click = false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            if (list->active_pointer_id != -1) return false;
            x = event->data.mouse.x;
            y = event->data.mouse.y;
            is_click = true;
            list->active_pointer_id = 0;
            break;
            
        case EVENT_TYPE_MOUSE_RELEASE:
            if (list->active_pointer_id != 0) return false;
            x = event->data.mouse.x;
            y = event->data.mouse.y;
            is_click = false;
            list->active_pointer_id = -1;
            break;
            
        case EVENT_TYPE_TOUCH_DOWN:
            if (list->active_pointer_id == -1) {
                list->active_pointer_id = event->data.touch.id;
                x = event->data.touch.x;
                y = event->data.touch.y;
                is_click = true;
            }
            break;
            
        case EVENT_TYPE_TOUCH_UP:
            if (list->active_pointer_id == event->data.touch.id) {
                list->active_pointer_id = -1;
                x = event->data.touch.x;
                y = event->data.touch.y;
                is_click = false;
            }
            break;
            
        default:
            return false;
    }

    AromaRect bounds = (AromaRect) {
        .x = event->target_node->x,
        .y = event->target_node->y,
        .width = event->target_node->width,
        .height = event->target_node->height
    };

    if (x < bounds.x || x >= bounds.x + bounds.width ||
        y < bounds.y || y >= bounds.y + bounds.height)
    {
        if (event->event_type == EVENT_TYPE_MOUSE_RELEASE || 
            event->event_type == EVENT_TYPE_TOUCH_UP) {
            list->active_pointer_id = -1;
        }
        return false;
    }

    if (!is_click)
        return false;

    int rel_y = y - bounds.y;
    int content_height = list->item_count * list->item_height;

    if (rel_y >= content_height)
        return false;

    int index = rel_y / list->item_height;

    if (index >= 0 && index < (int)list->item_count)
    {
        list->selected_index = index;

        if (list->callback)
            list->callback(index, list->user_data);

        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);

        AromaPlatformInterface *platform =
            aroma_backend_abi.get_platform_interface();

        if (platform && platform->android_vibrate)
            platform->android_vibrate(60);

        return true;
    }

    return false;
}

AromaNode *
aroma_listview_create(AromaNode *parent,
                      int x, int y,
                      int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
        return NULL;

    AromaListView *list =
        (AromaListView *)aroma_widget_alloc(sizeof(AromaListView));

    if (!list)
        return NULL;

    memset(list, 0, sizeof(AromaListView));

    list->rect.x = x;
    list->rect.y = y;
    list->rect.width = width;
    list->rect.height = height;

    list->selected_index = -1;
    list->item_height = 28;
    list->corner_radius = 8.0f;
    list->selected_corner_radius = 6.0f;
    list->text_scale = 1.0f;
    list->active_pointer_id = -1;

    AromaNode *node =
        __add_child_node(NODE_TYPE_WIDGET, parent, list);

    if (!node)
    {
        aroma_widget_free(list);
        return NULL;
    }

    node->x = x;
    node->y = y;
    node->width = width;
    node->height = height;

    aroma_node_set_draw_cb(node, aroma_listview_draw);

    aroma_event_subscribe(node->node_id,
                          EVENT_TYPE_MOUSE_CLICK,
                          __listview_handle_event,
                          NULL,
                          90);
    aroma_event_subscribe(node->node_id,
                          EVENT_TYPE_MOUSE_RELEASE,
                          __listview_handle_event,
                          NULL,
                          90);
    aroma_event_subscribe(node->node_id,
                          EVENT_TYPE_TOUCH_DOWN,
                          __listview_handle_event,
                          NULL,
                          90);
    aroma_event_subscribe(node->node_id,
                          EVENT_TYPE_TOUCH_UP,
                          __listview_handle_event,
                          NULL,
                          90);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_listview_add_item(AromaNode *list_node,
                             const char *text,
                             const char *secondary_text,
                             void *user_data)
{
    if (!list_node || !list_node->node_widget_ptr || !text)
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    if(!list)
        return;

    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return;

    AromaListItem *item =
        &list->items[list->item_count++];

    memset(item, 0, sizeof(AromaListItem));

    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';

    if (secondary_text)
    {
        strncpy(item->secondary_text,
                secondary_text,
                sizeof(item->secondary_text) - 1);
        item->secondary_text[
            sizeof(item->secondary_text) - 1] = '\0';
    }

    item->user_data = user_data;

    aroma_node_invalidate(list_node);
}

void aroma_listview_add_item_with_icon(AromaNode *list_node,
                                       const char *text,
                                       const char *secondary_text,
                                       const char *icon_code,
                                       void *user_data)
{
    if (!list_node || !list_node->node_widget_ptr || !text)
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    if(!list)
        return;
        
    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return;

    AromaListItem *item =
        &list->items[list->item_count++];

    memset(item, 0, sizeof(AromaListItem));

    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';

    if (secondary_text)
    {
        strncpy(item->secondary_text,
                secondary_text,
                sizeof(item->secondary_text) - 1);
        item->secondary_text[
            sizeof(item->secondary_text) - 1] = '\0';
    }

    if (icon_code)
    {
        strncpy(item->icon,
                icon_code,
                sizeof(item->icon) - 1);
        item->icon[sizeof(item->icon) - 1] = '\0';
    }

    item->user_data = user_data;

    aroma_node_invalidate(list_node);
}

void aroma_listview_clear(AromaNode *list_node)
{
    if (!list_node || !list_node->node_widget_ptr)
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    list->item_count = 0;
    list->selected_index = -1;

    aroma_node_invalidate(list_node);
}

void aroma_listview_set_callback(AromaNode *list_node,
                                 void (*callback)(int, void *),
                                 void *user_data)
{
    if (!list_node || !list_node->node_widget_ptr)
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    list->callback = callback;
    list->user_data = user_data;
}

void aroma_listview_set_font(AromaNode *list_node,
                             AromaFont *font)
{
    if (!list_node || !list_node->node_widget_ptr)
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    list->font = font;

    if (font)
    {
        int lh = aroma_font_get_line_height(font);
        list->item_height = (int)(lh * 1.5f);
        if (list->item_height < 28)
            list->item_height = 28;
    }
    else
    {
        list->item_height = 28;
    }

    aroma_node_invalidate(list_node);
}

void aroma_listview_set_icon_font(AromaNode *list_node,
                                  AromaFont *font)
{
    if (!list_node || !list_node->node_widget_ptr)
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    list->icon_font = font;

    aroma_node_invalidate(list_node);
}

void aroma_listview_draw(AromaNode *list_node,
                         size_t window_id)
{
    if (!list_node || !list_node->node_widget_ptr)
        return;

    if (aroma_node_is_hidden(list_node))
        return;

    AromaListView *list =
        (AromaListView *)list_node->node_widget_ptr;

    AromaGraphicsInterface *gfx =
        aroma_backend_abi.get_graphics_interface();

    if (!gfx)
        return;

    AromaTheme theme = aroma_theme_get_global();


    int width  = list_node->width;
    int height = list_node->height;
   
    gfx->fill_rectangle(window_id,
                        list_node->x, list_node->y,
                        width, height,
                        theme.colors.surface,
                        true,
                        list->corner_radius);

    for (size_t i = 0; i < list->item_count; ++i)
    {
        int y = list_node->y + (int)i * list->item_height;

        if (y + list->item_height > list_node->y + height)
            break;

        if ((int)i == list->selected_index)
        {
            gfx->fill_rectangle(
                window_id,
                list_node->x + 2,
                y,
                width - 4,
                list->item_height,
                aroma_color_blend(
                    theme.colors.surface,
                    theme.colors.primary_light,
                    0.2f),
                true,
                list->selected_corner_radius);
        }

        if (gfx->render_text && list->font)
        {
            int line_height =
                aroma_font_get_line_height(list->font);

            int text_y =
                y + (list->item_height - line_height) / 2;

            int text_x = list_node->x + 12;

            if (list->items[i].icon[0] != '\0' &&
                list->icon_font)
            {
                int icon_lh =
                    aroma_font_get_line_height(
                        list->icon_font);

                int icon_y =
                    y + (list->item_height - icon_lh) / 2;

                gfx->render_text(window_id,
                                 list->icon_font,
                                 list->items[i].icon,
                                 text_x,
                                 icon_y,
                                 theme.colors.text_primary,
                                 1.0f);

                text_x += icon_lh + 12;
            }

            gfx->render_text(window_id,
                             list->font,
                             list->items[i].text,
                             text_x,
                             text_y,
                             theme.colors.text_primary,
                             list->text_scale);
        }
    }
}

void aroma_listview_destroy(AromaNode *list_node)
{
    if (!list_node)
        return;

    if (list_node->node_widget_ptr)
    {
        aroma_widget_free(
            list_node->node_widget_ptr);
        list_node->node_widget_ptr = NULL;
    }

    __destroy_node(list_node);
}