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
#include <stdlib.h>
#include <math.h>

#define AROMA_LIST_MAX_ITEMS     16
#define AROMA_LIST_ITEM_PADDING  12
#define AROMA_LIST_ICON_PADDING  12
#define AROMA_LIST_MIN_ITEM_HEIGHT 28
typedef struct  __attribute__((packed, aligned(1)))  {
    AromaRect rect;

    AromaFont *font;
    AromaFont *secondary_font;
    AromaFont *icon_font;
    AromaNode *self_node;
    AromaNode *scroll_container;

    void (*callback)(int index, void *user_data);
    void *user_data;
    size_t item_count;

    uint32_t header_bg_color;
    uint32_t header_text_color;

    int selected_index;
    int pressed_index;
    int item_height;
    int active_pointer_id;

    float corner_radius;
    float selected_corner_radius;
    float text_scale;
    float secondary_text_scale;

    bool show_headers;

    // Explicit padding for WASM memory alignment
    uint8_t _padding[3];

    uint8_t item_types[AROMA_LIST_MAX_ITEMS];

    AromaListItem items[AROMA_LIST_MAX_ITEMS];

} AromaListViewInternal;

const uint32_t AROMA_MATERIAL_COLORS[] = {
    0xFFF44336, 0xFFE91E63, 0xFF9C27B0, 0xFF673AB7,
    0xFF3F51B5, 0xFF2196F3, 0xFF03A9F4, 0xFF00BCD4,
    0xFF009688, 0xFF4CAF50, 0xFF8BC34A, 0xFFCDDC39,
    0xFFFFEB3B, 0xFFFFC107, 0xFFFF9800, 0xFFFF5722,
};

static inline AromaListViewInternal *get_listview_internal(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return NULL;
    return (AromaListViewInternal *)node->node_widget_ptr;
}

static bool __is_header_item(const AromaListViewInternal *list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count)
        return false;
    return (list->item_types[index] == AROMA_LIST_ITEM_HEADER);
}

static bool __is_separator_item(const AromaListViewInternal *list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count)
        return false;
    return (list->item_types[index] == AROMA_LIST_ITEM_SEPARATOR);
}

static bool __item_is_selectable(const AromaListViewInternal *list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count)
        return false;
    return (list->item_types[index] == AROMA_LIST_ITEM_NORMAL);
}

static int __get_item_height(const AromaListViewInternal *list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count)
        return list->item_height;
    if (__is_header_item(list, index))
        return list->item_height / 2;
    if (__is_separator_item(list, index))
        return 1;
    if (list->items[index].secondary_text[0] != '\0')
        return (int)(list->item_height * 1.5f);
    return list->item_height;
}

static int lv_total_content_height(const AromaListViewInternal *list)
{
    int total = 0;
    for (size_t i = 0; i < list->item_count; i++)
        total += __get_item_height(list, (int)i);
    return total;
}

static void lv_update_content_height(AromaListViewInternal *list)
{
    if (!list) return;
    int h = lv_total_content_height(list);
    if (h < 1) h = 1;
    list->rect.height = h;
    if (list->scroll_container)
        aroma_container_update_auto_content_size(list->scroll_container);
}

static int lv_hit_test_item(const AromaListViewInternal *list, int screen_y)
{
    int scroll_y = 0;
    if (list->scroll_container)
        aroma_container_get_scroll(list->scroll_container, NULL, &scroll_y);

    int rel_y    = screen_y - list->rect.y + scroll_y;
    int current_y = 0;
    for (size_t i = 0; i < list->item_count; i++)
    {
        int ih = __get_item_height(list, (int)i);
        if (rel_y >= current_y && rel_y < current_y + ih)
            return (int)i;
        current_y += ih;
    }
    return -1;
}

static bool __listview_handle_event(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || !event->target_node) return false;

    AromaListViewInternal *list = get_listview_internal(event->target_node);
    if (!list) return false;

    AromaNode *node   = event->target_node;
    AromaRect  bounds = list->rect;

    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_CLICK:
    {
        if (list->active_pointer_id != -1) return false;
        int x = event->data.mouse.x, y = event->data.mouse.y;
        if (x < bounds.x || x >= bounds.x + bounds.width ||
            y < bounds.y || y >= bounds.y + bounds.height)
            return false;
        list->active_pointer_id = 0;
        int hit = lv_hit_test_item(list, y);
        if (hit >= 0 && __item_is_selectable(list, hit))
        {
            list->pressed_index = hit;
            aroma_node_invalidate(node);
        }
        return true;
    }
    case EVENT_TYPE_TOUCH_DOWN:
    {
        int tx = event->data.touch.x, ty = event->data.touch.y;
        if (tx < bounds.x || tx >= bounds.x + bounds.width ||
            ty < bounds.y || ty >= bounds.y + bounds.height)
            return false;
        list->active_pointer_id = event->data.touch.id;
        int hit = lv_hit_test_item(list, ty);
        if (hit >= 0 && __item_is_selectable(list, hit))
        {
            list->pressed_index = hit;
            aroma_node_invalidate(node);
        }
        return false;
    }
    case EVENT_TYPE_MOUSE_RELEASE:
    {
        if (list->active_pointer_id != 0) return false;
        list->active_pointer_id = -1;
        int x = event->data.mouse.x, y = event->data.mouse.y;
        bool in_bounds = x >= bounds.x && x < bounds.x + bounds.width &&
                         y >= bounds.y && y < bounds.y + bounds.height;
        int  hit       = in_bounds ? lv_hit_test_item(list, y) : -1;
        bool was_pressed = (list->pressed_index == hit && hit >= 0);
        list->pressed_index = -1;
        aroma_node_invalidate(node);
        if (was_pressed && __item_is_selectable(list, hit))
        {
            list->selected_index = hit;
            if (list->callback) list->callback(hit, list->user_data);
            AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
            if (platform && platform->android_vibrate) platform->android_vibrate(60);
            return true;
        }
        return false;
    }
    case EVENT_TYPE_TOUCH_UP:
    {
        if (event->data.touch.id != list->active_pointer_id) return false;
        list->active_pointer_id = -1;
        int  ty        = event->data.touch.y;
        int  hit       = lv_hit_test_item(list, ty);
        bool was_pressed = (list->pressed_index == hit && hit >= 0);
        list->pressed_index = -1;
        aroma_node_invalidate(node);
        if (was_pressed && __item_is_selectable(list, hit))
        {
            list->selected_index = hit;
            if (list->callback) list->callback(hit, list->user_data);
            AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
            if (platform && platform->android_vibrate) platform->android_vibrate(60);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

AromaNode *aroma_listview_create(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) return NULL;

    AromaListViewInternal *list =
        (AromaListViewInternal *)aroma_widget_alloc(sizeof(AromaListViewInternal));
    if (!list) return NULL;

    memset(list, 0, sizeof(AromaListViewInternal));
    list->rect.x      = x;
    list->rect.y      = y;
    list->rect.width  = width;
    list->rect.height = height;
    list->selected_index         = -1;
    list->pressed_index          = -1;
    list->item_height            = AROMA_LIST_MIN_ITEM_HEIGHT;
    list->corner_radius          = 8.0f;
    list->selected_corner_radius = 6.0f;
    list->text_scale             = 1.0f;
    list->secondary_text_scale   = 0.8f;
    list->active_pointer_id      = -1;
    list->show_headers           = true;
    list->scroll_container       = NULL;

    for (int i = 0; i < AROMA_LIST_MAX_ITEMS; i++)
        list->item_types[i] = AROMA_LIST_ITEM_NORMAL;

    AromaTheme theme = aroma_theme_get_global();
    list->header_bg_color   = aroma_color_blend(theme.colors.surface,
                                                 theme.colors.primary, 0.1f);
    list->header_text_color = theme.colors.text_secondary;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, list);
    if (!node)
    {
        aroma_widget_free(list);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_listview_draw);
    list->self_node = node;

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK,   __listview_handle_event, NULL, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __listview_handle_event, NULL, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN,    __listview_handle_event, NULL, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP,      __listview_handle_event, NULL, 90);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif
    return node;
}

void aroma_listview_add_item(AromaNode *list_node, const char *text,
                             const char *secondary_text, void *user_data)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list || !text || list->item_count >= AROMA_LIST_MAX_ITEMS) return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    if (secondary_text)
    {
        strncpy(item->secondary_text, secondary_text,
                sizeof(item->secondary_text) - 1);
        item->secondary_text[sizeof(item->secondary_text) - 1] = '\0';
    }
    item->user_data = user_data;
    list->item_types[list->item_count] = AROMA_LIST_ITEM_NORMAL;
    list->item_count++;
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_add_item_with_icon(AromaNode *list_node, const char *text,
                                       const char *secondary_text,
                                       const char *icon_code, void *user_data)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list || !text || list->item_count >= AROMA_LIST_MAX_ITEMS) return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    if (secondary_text)
    {
        strncpy(item->secondary_text, secondary_text,
                sizeof(item->secondary_text) - 1);
        item->secondary_text[sizeof(item->secondary_text) - 1] = '\0';
    }
 if (icon_code)
{
    strncpy(item->icon, icon_code, sizeof(item->icon) - 1);
    item->icon[sizeof(item->icon) - 1] = '\0';
}
    item->user_data = user_data;
    list->item_types[list->item_count] = AROMA_LIST_ITEM_NORMAL;
    list->item_count++;
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_add_header(AromaNode *list_node, const char *text)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list || !text || list->item_count >= AROMA_LIST_MAX_ITEMS) return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    list->item_types[list->item_count] = AROMA_LIST_ITEM_HEADER;
    list->item_count++;
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_add_separator(AromaNode *list_node)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list || list->item_count >= AROMA_LIST_MAX_ITEMS) return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));
    list->item_types[list->item_count] = AROMA_LIST_ITEM_SEPARATOR;
    list->item_count++;
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_remove_item(AromaNode *list_node, int index)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list || index < 0 || index >= (int)list->item_count) return;

    for (int i = index; i < (int)list->item_count - 1; i++)
    {
        memcpy(&list->items[i], &list->items[i + 1], sizeof(AromaListItem));
        list->item_types[i] = list->item_types[i + 1];
    }
    list->item_count--;

    if (list->selected_index == index)       list->selected_index = -1;
    else if (list->selected_index > index)   list->selected_index--;
    if (list->pressed_index == index)        list->pressed_index = -1;
    else if (list->pressed_index > index)    list->pressed_index--;

    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_clear(AromaNode *list_node)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list) return;
    list->item_count     = 0;
    list->selected_index = -1;
    list->pressed_index  = -1;
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

int aroma_listview_get_selected(AromaNode *list_node)
{
    if (!list_node) return -1;
    AromaListViewInternal *list = get_listview_internal(list_node);
    return list ? list->selected_index : -1;
}

size_t aroma_listview_get_count(AromaNode *list_node)
{
    if (!list_node) return 0;
    AromaListViewInternal *list = get_listview_internal(list_node);
    return list ? list->item_count : 0;
}

void *aroma_listview_get_item_data(AromaNode *list_node, int index)
{
    if (!list_node) return NULL;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list || index < 0 || index >= (int)list->item_count) return NULL;
    return list->items[index].user_data;
}

void aroma_listview_set_callback(AromaNode *list_node,
                                 void (*callback)(int, void *), void *user_data)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list) return;
    list->callback  = callback;
    list->user_data = user_data;
}

void aroma_listview_set_font(AromaNode *list_node, AromaFont *font)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list) return;
    list->font = font;
    if (font)
    {
        int lh = aroma_font_get_line_height(font);
        list->item_height = (int)(lh * 1.5f);
        if (list->item_height < AROMA_LIST_MIN_ITEM_HEIGHT)
            list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    }
    else
    {
        list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    }
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_secondary_font(AromaNode *list_node, AromaFont *font)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->secondary_font = font; aroma_node_invalidate(list_node); }
}

void aroma_listview_set_icon_font(AromaNode *list_node, AromaFont *font)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->icon_font = font; aroma_node_invalidate(list_node); }
}

void aroma_listview_set_item_height(AromaNode *list_node, int height)
{
    if (!list_node || height <= 0) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list) return;
    list->item_height = height;
    lv_update_content_height(list);
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_text_scale(AromaNode *list_node, float scale)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->text_scale = scale; aroma_node_invalidate(list_node); }
}

void aroma_listview_set_secondary_text_scale(AromaNode *list_node, float scale)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->secondary_text_scale = scale; aroma_node_invalidate(list_node); }
}

void aroma_listview_set_corner_radius(AromaNode *list_node, float radius)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->corner_radius = radius; aroma_node_invalidate(list_node); }
}

void aroma_listview_set_selected_corner_radius(AromaNode *list_node, float radius)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->selected_corner_radius = radius; aroma_node_invalidate(list_node); }
}

void aroma_listview_show_headers(AromaNode *list_node, bool show)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) { list->show_headers = show; aroma_node_invalidate(list_node); }
}

void aroma_listview_set_header_colors(AromaNode *list_node,
                                      uint32_t bg_color, uint32_t text_color)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list)
    {
        list->header_bg_color   = bg_color;
        list->header_text_color = text_color;
        aroma_node_invalidate(list_node);
    }
}

void aroma_listview_set_scroll_container(AromaNode *list_node, AromaNode *container)
{
    if (!list_node) return;
    AromaListViewInternal *list = get_listview_internal(list_node);
    if (list) list->scroll_container = container;
}

AromaNode *aroma_listview_get_scroll_container(AromaNode *list_node)
{
    if (!list_node) return NULL;
    AromaListViewInternal *list = get_listview_internal(list_node);
    return list ? list->scroll_container : NULL;
}

void aroma_listview_draw(AromaNode *list_node, size_t window_id)
{
    if (!list_node) return;
    if (aroma_node_is_hidden(list_node)) return;

    AromaListViewInternal *list = get_listview_internal(list_node);
    if (!list) return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text) return;

    if (!list->font)
    {
        LOG_ERROR("aroma_listview_draw: font is NULL, skipping listview");
        return;
    }

    AromaTheme theme = aroma_theme_get_global();
    int width = list->rect.width;

    int scroll_y = 0;
    if (list->scroll_container)
        aroma_container_get_scroll(list->scroll_container, NULL, &scroll_y);

    int current_y = list->rect.y - scroll_y;

    for (size_t i = 0; i < list->item_count; ++i)
    {
        int item_height  = __get_item_height(list, (int)i);
        bool is_header   = __is_header_item(list, (int)i);
        bool is_separator = __is_separator_item(list, (int)i);
        bool is_selected = ((int)i == list->selected_index);
        bool is_pressed  = ((int)i == list->pressed_index);

        if (is_separator)
        {
            int separator_y = current_y + item_height / 2;
            gfx->fill_rectangle(window_id,
                                 list->rect.x + AROMA_LIST_ITEM_PADDING,
                                 separator_y,
                                 width - (AROMA_LIST_ITEM_PADDING * 2), 1,
                                 aroma_color_blend(theme.colors.text_secondary,
                                                   theme.colors.surface, 0.35f),
                                 false, 0);
            current_y += item_height;
            continue;
        }

        if (is_header && list->show_headers)
        {
            gfx->fill_rectangle(window_id,
                                 list->rect.x, current_y,
                                 width, item_height,
                                 list->header_bg_color, true, 0);
        }

        if ((is_selected || is_pressed) && !is_header)
        {
            uint32_t highlight_color = is_pressed
                ? aroma_color_blend(theme.colors.primary,
                                    theme.colors.surface, 0.3f)
                : aroma_color_blend(theme.colors.surface,
                                    theme.colors.primary_light, 0.2f);
            gfx->fill_rectangle(window_id,
                                 list->rect.x + 2, current_y,
                                 width - 4, item_height,
                                 highlight_color, true,
                                 list->selected_corner_radius);
        }

      if (gfx->render_text)
{
    int text_x = list->rect.x + AROMA_LIST_ITEM_PADDING;

    if (list->items[i].icon[0] != '\0' && list->icon_font)
    {
        int icon_lh = (int)(aroma_font_get_px_size(list->icon_font) * 0.6f);
        int icon_y  = current_y + (item_height - icon_lh) / 2;
        const size_t cidx = i % (sizeof(AROMA_MATERIAL_COLORS) /
                                 sizeof(AROMA_MATERIAL_COLORS[0]));
        uint32_t icon_color = AROMA_MATERIAL_COLORS[cidx];

        gfx->fill_rectangle(window_id,
                             text_x - 4,
                             current_y + (item_height - icon_lh) / 2 - 4,
                             icon_lh + 8, icon_lh + 8,
                             aroma_color_blend(icon_color,
                                               theme.colors.surface, 0.4f),
                             true, icon_lh / 2 + 4);

        gfx->render_text(window_id, list->icon_font,
                         list->items[i].icon, text_x, icon_y,
                         is_header ? list->header_text_color
                                   : theme.colors.text_primary,
                         0.6f);

        text_x += icon_lh + AROMA_LIST_ICON_PADDING;
    }

    bool has_secondary = (list->items[i].secondary_text[0] != '\0' && !is_header);

    // DEBUG START
    static int debug_count = 0;
    if (debug_count < 5 && list->items[i].secondary_text[0] != '\0') {
        LOG_INFO("DRAW[%zu]: sec='%s' has_sec=%d ih=%d curr_y=%d rect_h=%d scroll_y=%d",
                 i, list->items[i].secondary_text, has_secondary,
                 item_height, current_y, list->rect.height, scroll_y);
        debug_count++;
    }
    // DEBUG END

    if (list->items[i].text[0] != '\0')
    {
        int line_height = aroma_font_get_line_height(list->font);
        int text_y;

        if (has_secondary)
        {
            int total_text_height = line_height + 
                                    (int)(line_height * list->secondary_text_scale) + 4;
            text_y = current_y + (item_height - total_text_height) / 2;
        }
        else
        {
            text_y = current_y + (item_height - line_height) / 2;
        }

        uint32_t text_color = is_header
            ? list->header_text_color
            : theme.colors.text_primary;

        gfx->render_text(window_id, list->font,
                         list->items[i].text, text_x, text_y,
                         text_color,
                         is_header ? list->secondary_text_scale
                                   : list->text_scale);
    }

    if (has_secondary)
    {
        AromaFont *sec_font = list->secondary_font
                                  ? list->secondary_font
                                  : list->font;
        if (sec_font)
        {
            int primary_line_height = aroma_font_get_line_height(list->font);
            int sec_line_height = (int)(primary_line_height * list->secondary_text_scale);
            int total_text_height = primary_line_height + sec_line_height + 4;
            int text_y = current_y + (item_height - total_text_height) / 2;
            int sec_text_y = text_y + primary_line_height + 2;

            // DEBUG
            static int sec_debug = 0;
            if (sec_debug < 5) {
                LOG_INFO("DRAW_SEC: y=%d font=%p text='%s' color=0x%X scale=%.2f",
                         sec_text_y, (void*)sec_font, list->items[i].secondary_text,
                         theme.colors.text_secondary, list->secondary_text_scale);
                sec_debug++;
            }

            gfx->render_text(window_id, sec_font,
                             list->items[i].secondary_text,
                             text_x, sec_text_y,
                             theme.colors.text_secondary,
                             list->secondary_text_scale);
        }
        else
        {
            LOG_ERROR("DRAW_SEC: sec_font is NULL!");
        }
    }
}

        current_y += item_height;
    }
}

void aroma_listview_destroy(AromaNode *list_node)
{
    if (!list_node) return;
    if (list_node->node_widget_ptr)
    {
        aroma_widget_free(list_node->node_widget_ptr);
        list_node->node_widget_ptr = NULL;
    }
}