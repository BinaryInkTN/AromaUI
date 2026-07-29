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
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_LIST_MAX_ITEMS 64
#define AROMA_LIST_ITEM_PADDING 12
#define AROMA_LIST_ICON_PADDING 12
#define AROMA_LIST_MIN_ITEM_HEIGHT 28
#define AROMA_MATERIAL_COLOR_COUNT 16

typedef struct
{
    AromaRect rect;
    AromaFont *font;
    AromaFont *secondary_font;
    AromaFont *icon_font;
    AromaNode *self_node;

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
    uint8_t _padding[3];

    uint8_t item_types[AROMA_LIST_MAX_ITEMS];
    AromaListItem items[AROMA_LIST_MAX_ITEMS];
} AromaListViewInternal;

static const uint32_t AROMA_MATERIAL_COLORS[AROMA_MATERIAL_COLOR_COUNT] = {
    0xFFF44336,
    0xFFE91E63,
    0xFF9C27B0,
    0xFF673AB7,
    0xFF3F51B5,
    0xFF2196F3,
    0xFF03A9F4,
    0xFF00BCD4,
    0xFF009688,
    0xFF4CAF50,
    0xFF8BC34A,
    0xFFCDDC39,
    0xFFFFEB3B,
    0xFFFFC107,
    0xFFFF9800,
    0xFFFF5722,
};

static inline AromaListViewInternal *get_internal(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return NULL;
    return (AromaListViewInternal *)node->node_widget_ptr;
}

static inline bool item_in_range(const AromaListViewInternal *list, int i)
{
    return list && i >= 0 && i < (int)list->item_count;
}

static bool is_header(const AromaListViewInternal *list, int i)
{
    return item_in_range(list, i) && list->item_types[i] == AROMA_LIST_ITEM_HEADER;
}

static bool is_separator(const AromaListViewInternal *list, int i)
{
    return item_in_range(list, i) && list->item_types[i] == AROMA_LIST_ITEM_SEPARATOR;
}

static bool is_selectable(const AromaListViewInternal *list, int i)
{
    return item_in_range(list, i) && list->item_types[i] == AROMA_LIST_ITEM_NORMAL;
}

static int item_height_at(const AromaListViewInternal *list, int i)
{
    if (!list)
        return AROMA_LIST_MIN_ITEM_HEIGHT;
    if (!item_in_range(list, i))
        return list->item_height;
    if (is_header(list, i))
        return list->item_height / 2;
    if (is_separator(list, i))
        return 1;
    if (list->items[i].secondary_text[0] != '\0')
        return (int)(list->item_height * 1.5f);
    return list->item_height;
}

static int total_content_height(const AromaListViewInternal *list)
{
    int h = 0;
    for (size_t i = 0; i < list->item_count; i++)
        h += item_height_at(list, (int)i);
    return h;
}

static void update_content_height(AromaListViewInternal *list)
{
    if (!list)
        return;
    int h = total_content_height(list);
    list->rect.height = h > 0 ? h : 1;
}

static int selectable_index(const AromaListViewInternal *list, int raw_index)
{
    if (!list || raw_index < 0)
        return -1;
    int count = 0;
    for (int i = 0; i <= raw_index; i++)
    {
        if (is_selectable(list, i))
            count++;
    }
    return count - 1;
}

static int hit_test(AromaNode *node, const AromaListViewInternal *list, int screen_y)
{
    if (!list)
        return -1;

    int y = list->rect.y;
    if (node && node->parent_node &&
        aroma_container_is_scrollable(node->parent_node))
    {
        int scroll_x = 0, scroll_y = 0;
        aroma_container_get_scroll(node->parent_node, &scroll_x, &scroll_y);
        y -= scroll_y;
    }

    for (size_t i = 0; i < list->item_count; i++)
    {
        int ih = item_height_at(list, (int)i);
        if (screen_y >= y && screen_y < y + ih)
        {
            if (is_header(list, (int)i) || is_separator(list, (int)i))
                return -1;
            return (int)i;
        }
        y += ih;
    }
    return -1;
}

static void commit_selection(AromaListViewInternal *list, AromaNode *node, int hit)
{
    list->selected_index = hit;
    if (list->callback && is_selectable(list, hit))
    {
        int adjusted = selectable_index(list, hit);
        list->callback(adjusted, list->user_data);
    }
    AromaPlatformInterface *plat = aroma_backend_abi.get_platform_interface();
    if (plat && plat->android_vibrate)
        plat->android_vibrate(60);
    aroma_node_invalidate(node);
}

static bool listview_handle_event(AromaEvent *ev, void *user_data)
{
    (void)user_data;
    if (!ev || !ev->target_node)
        return false;

    AromaListViewInternal *list = get_internal(ev->target_node);
    if (!list)
        return false;

    AromaNode *node = ev->target_node;
    AromaRect bounds = list->rect;

    switch (ev->event_type)
    {

    case EVENT_TYPE_MOUSE_CLICK:
    {
        if (list->active_pointer_id != -1)
            return false;
        int x = ev->data.mouse.x, y = ev->data.mouse.y;
        if (x < bounds.x || x >= bounds.x + bounds.width ||
            y < bounds.y || y >= bounds.y + bounds.height)
            return false;
        list->active_pointer_id = 0;
        int hit = hit_test(node, list, y);
        if (hit >= 0 && is_selectable(list, hit))
        {
            list->pressed_index = hit;
            aroma_node_invalidate(node);
        }
        return true;
    }

    case EVENT_TYPE_TOUCH_DOWN:
    {
        int tx = ev->data.touch.x, ty = ev->data.touch.y;
        if (tx < bounds.x || tx >= bounds.x + bounds.width ||
            ty < bounds.y || ty >= bounds.y + bounds.height)
            return false;
        list->active_pointer_id = ev->data.touch.id;
        int hit = hit_test(node, list, ty);
        if (hit >= 0 && is_selectable(list, hit))
        {
            list->pressed_index = hit;
            aroma_node_invalidate(node);
        }
        return false;
    }

    case EVENT_TYPE_MOUSE_RELEASE:
    {
        if (list->active_pointer_id != 0)
            return false;
        list->active_pointer_id = -1;
        int x = ev->data.mouse.x, y = ev->data.mouse.y;
        bool in_bounds = x >= bounds.x && x < bounds.x + bounds.width &&
                         y >= bounds.y && y < bounds.y + bounds.height;
        int hit = in_bounds ? hit_test(node, list, y) : -1;
        bool activated = (list->pressed_index == hit && hit >= 0);
        list->pressed_index = -1;
        aroma_node_invalidate(node);
        if (activated && is_selectable(list, hit))
        {
            commit_selection(list, node, hit);
            return true;
        }
        return false;
    }

    case EVENT_TYPE_TOUCH_UP:
    {
        if (ev->data.touch.id != list->active_pointer_id)
            return false;
        list->active_pointer_id = -1;
        int hit = hit_test(node, list, ev->data.touch.y);
        bool activated = (list->pressed_index == hit && hit >= 0);
        list->pressed_index = -1;
        aroma_node_invalidate(node);
        if (activated && is_selectable(list, hit))
        {
            commit_selection(list, node, hit);
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

AromaNode *aroma_listview_create(AromaNode *parent, int x, int y,
                                 int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
        return NULL;

#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif

    AromaListViewInternal *list =
        (AromaListViewInternal *)aroma_widget_alloc(sizeof(AromaListViewInternal));
    if (!list)
        return NULL;
    memset(list, 0, sizeof(AromaListViewInternal));

    list->rect = (AromaRect){x, y, width, height};
    list->selected_index = -1;
    list->pressed_index = -1;
    list->active_pointer_id = -1;
    list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    list->corner_radius = 8.0f;
    list->selected_corner_radius = 6.0f;
    list->text_scale = 1.0f;
    list->secondary_text_scale = 0.8f;
    list->show_headers = true;

    for (int i = 0; i < AROMA_LIST_MAX_ITEMS; i++)
        list->item_types[i] = AROMA_LIST_ITEM_NORMAL;

    AromaTheme theme = aroma_theme_get_global();
    list->header_bg_color = aroma_color_blend(theme.colors.surface,
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

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK,
                          listview_handle_event, NULL, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE,
                          listview_handle_event, NULL, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN,
                          listview_handle_event, NULL, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP,
                          listview_handle_event, NULL, 90);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif
    return node;
}

static void safe_copy(char *dst, const char *src, size_t dstsz)
{
    if (!src || dstsz == 0)
    {
        if (dstsz)
            dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

static AromaListItem *reserve_item(AromaListViewInternal *list, uint8_t type)
{
    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return NULL;
    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));
    list->item_types[list->item_count] = type;
    list->item_count++;
    return item;
}

void aroma_listview_add_item(AromaNode *node, const char *text,
                             const char *secondary, void *user_data)
{
    if (!node || !text)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list)
        return;
    AromaListItem *item = reserve_item(list, AROMA_LIST_ITEM_NORMAL);
    if (!item)
        return;
    safe_copy(item->text, text, sizeof(item->text));
    safe_copy(item->secondary_text, secondary, sizeof(item->secondary_text));
    item->user_data = user_data;
    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_add_item_with_icon(AromaNode *node, const char *text,
                                       const char *secondary,
                                       const char *icon_code, void *user_data)
{
    if (!node || !text)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list)
        return;
    AromaListItem *item = reserve_item(list, AROMA_LIST_ITEM_NORMAL);
    if (!item)
        return;
    safe_copy(item->text, text, sizeof(item->text));
    safe_copy(item->secondary_text, secondary, sizeof(item->secondary_text));
    safe_copy(item->icon, icon_code, sizeof(item->icon));
    item->user_data = user_data;
    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_add_header(AromaNode *node, const char *text)
{
    if (!node || !text)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list)
        return;
    AromaListItem *item = reserve_item(list, AROMA_LIST_ITEM_HEADER);
    if (!item)
        return;
    safe_copy(item->text, text, sizeof(item->text));
    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_add_separator(AromaNode *node)
{
    if (!node)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list)
        return;
    if (!reserve_item(list, AROMA_LIST_ITEM_SEPARATOR))
        return;
    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_remove_item(AromaNode *node, int index)
{
    if (!node)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list || !item_in_range(list, index))
        return;

    for (int i = index; i < (int)list->item_count - 1; i++)
    {
        memcpy(&list->items[i], &list->items[i + 1], sizeof(AromaListItem));
        list->item_types[i] = list->item_types[i + 1];
    }
    list->item_count--;

    if (list->selected_index == index)
        list->selected_index = -1;
    else if (list->selected_index > index)
        list->selected_index--;
    if (list->pressed_index == index)
        list->pressed_index = -1;
    else if (list->pressed_index > index)
        list->pressed_index--;

    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_clear(AromaNode *node)
{
    if (!node)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list)
        return;
    list->item_count = 0;
    list->selected_index = -1;
    list->pressed_index = -1;
    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_update_title_text(AromaNode *node, int index, const char *text)
{
    if (!node || !text)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list || !item_in_range(list, index))
        return;
    safe_copy(list->items[index].text, text, sizeof(list->items[index].text));
    aroma_node_invalidate(node);
}

void aroma_listview_update_secondary_text(AromaNode *node, int index, const char *text)
{
    if (!node || !text)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list || !item_in_range(list, index))
        return;
    safe_copy(list->items[index].secondary_text, text,
              sizeof(list->items[index].secondary_text));
    aroma_node_invalidate(node);
}

int aroma_listview_get_selected(AromaNode *n)
{
    AromaListViewInternal *l = get_internal(n);
    return l ? l->selected_index : -1;
}

size_t aroma_listview_get_count(AromaNode *n)
{
    AromaListViewInternal *l = get_internal(n);
    return l ? l->item_count : 0;
}

size_t aroma_listview_get_selectable_count(AromaNode *n)
{
    AromaListViewInternal *l = get_internal(n);
    if (!l)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < l->item_count; i++)
    {
        if (is_selectable(l, (int)i))
            count++;
    }
    return count;
}

void *aroma_listview_get_item_data(AromaNode *n, int i)
{
    AromaListViewInternal *l = get_internal(n);
    return (l && item_in_range(l, i)) ? l->items[i].user_data : NULL;
}

void aroma_listview_set_callback(AromaNode *n,
                                 void (*cb)(int, void *), void *ud)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->callback = cb;
        l->user_data = ud;
    }
}

void aroma_listview_set_font(AromaNode *node, AromaFont *font)
{
    if (!node)
        return;
    AromaListViewInternal *list = get_internal(node);
    if (!list)
        return;
    list->font = font;
    list->item_height = font
                            ? (int)(aroma_font_get_line_height(font) * 1.5f)
                            : AROMA_LIST_MIN_ITEM_HEIGHT;
    if (list->item_height < AROMA_LIST_MIN_ITEM_HEIGHT)
        list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    update_content_height(list);
    aroma_node_invalidate(node);
}

void aroma_listview_set_secondary_font(AromaNode *n, AromaFont *f)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->secondary_font = f;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_set_icon_font(AromaNode *n, AromaFont *f)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->icon_font = f;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_set_item_height(AromaNode *n, int h)
{
    if (!n || h <= 0)
        return;
    AromaListViewInternal *l = get_internal(n);
    if (!l)
        return;
    l->item_height = h;
    update_content_height(l);
    aroma_node_invalidate(n);
}

void aroma_listview_set_text_scale(AromaNode *n, float s)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->text_scale = s;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_set_secondary_text_scale(AromaNode *n, float s)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->secondary_text_scale = s;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_set_corner_radius(AromaNode *n, float r)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->corner_radius = r;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_set_selected_corner_radius(AromaNode *n, float r)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->selected_corner_radius = r;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_show_headers(AromaNode *n, bool show)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->show_headers = show;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_set_header_colors(AromaNode *n, uint32_t bg, uint32_t text)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->header_bg_color = bg;
        l->header_text_color = text;
        aroma_node_invalidate(n);
    }
}

void aroma_listview_draw(AromaNode *node, size_t window_id)
{
    if (!node || aroma_node_is_hidden(node))
        return;

    AromaListViewInternal *list = get_internal(node);
    if (!list || !list->font)
    {
        if (list)
            LOG_ERROR("listview draw: font is NULL");
        return;
    }

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text)
        return;

    AromaTheme theme = aroma_theme_get_global();
    int width = list->rect.width;

    int current_y = list->rect.y;
    int primary_lh = aroma_font_get_line_height(list->font);

    for (size_t i = 0; i < list->item_count; i++)
    {
        int ih = item_height_at(list, (int)i);

        bool hdr = is_header(list, (int)i);
        bool sep = is_separator(list, (int)i);
        bool selected = ((int)i == list->selected_index);
        bool pressed = ((int)i == list->pressed_index);

        if (sep)
        {
            gfx->fill_rectangle(window_id,
                                list->rect.x + AROMA_LIST_ITEM_PADDING,
                                current_y + ih / 2,
                                width - AROMA_LIST_ITEM_PADDING * 2, 1,
                                aroma_color_blend(theme.colors.text_secondary,
                                                  theme.colors.surface, 0.35f),
                                false, 0);
            current_y += ih;
            continue;
        }

        if (hdr && list->show_headers)
            gfx->fill_rectangle(window_id,
                                list->rect.x, current_y, width, ih,
                                list->header_bg_color, true, 0);

        if ((selected || pressed) && !hdr)
        {
            uint32_t hi = pressed
                              ? aroma_color_blend(theme.colors.primary, theme.colors.surface, 0.3f)
                              : aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.2f);
            gfx->fill_rectangle(window_id,
                                list->rect.x + 2, current_y, width - 4, ih,
                                hi, true, list->selected_corner_radius);
        }

        int text_x = list->rect.x + AROMA_LIST_ITEM_PADDING;

        if (list->items[i].icon[0] != '\0' && list->icon_font)
        {
            int icon_sz = (int)(aroma_font_get_px_size(list->icon_font) * 0.6f);
            int icon_y = current_y + (ih - icon_sz) / 2;
            uint32_t col = AROMA_MATERIAL_COLORS[i % AROMA_MATERIAL_COLOR_COUNT];

            gfx->fill_rectangle(window_id,
                                text_x - 4,
                                current_y + (ih - icon_sz) / 2 - 4,
                                icon_sz + 8, icon_sz + 8,
                                aroma_color_blend(col, theme.colors.surface, 0.4f),
                                true, icon_sz / 2 + 4);

            gfx->render_text(window_id, list->icon_font,
                             list->items[i].icon, text_x, icon_y,
                             hdr ? list->header_text_color : theme.colors.text_primary,
                             0.6f);

            text_x += icon_sz + AROMA_LIST_ICON_PADDING;
        }

        bool has_secondary = !hdr && list->items[i].secondary_text[0] != '\0';

        if (list->items[i].text[0] != '\0')
        {
            int text_y;
            if (has_secondary)
            {
                int sec_lh = (int)(primary_lh * list->secondary_text_scale);
                int total = primary_lh + sec_lh + 4;
                text_y = current_y + (ih - total) / 2;
            }
            else
            {
                text_y = current_y + (ih - primary_lh) / 2;
            }
            gfx->render_text(window_id, list->font,
                             list->items[i].text, text_x, text_y,
                             hdr ? list->header_text_color : theme.colors.text_primary,
                             hdr ? list->secondary_text_scale : list->text_scale);
        }

        if (has_secondary)
        {
            AromaFont *sf = list->secondary_font ? list->secondary_font : list->font;
            int sec_lh = (int)(primary_lh * list->secondary_text_scale);
            int total = primary_lh + sec_lh + 4;
            int text_y = current_y + (ih - total) / 2;
            int sec_y = text_y + primary_lh + 2;
            gfx->render_text(window_id, sf,
                             list->items[i].secondary_text, text_x, sec_y,
                             theme.colors.text_secondary,
                             list->secondary_text_scale);
        }

        current_y += ih;
    }

}

void aroma_listview_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr)
    {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}