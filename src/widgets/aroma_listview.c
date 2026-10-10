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
#include "aroma_dp.h"
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_LIST_MAX_ITEMS 64

#define AROMA_LIST_ITEM_PADDING_DP 12
#define AROMA_LIST_ICON_PADDING_DP 12
#define AROMA_LIST_MIN_ITEM_HEIGHT_DP 28
#define AROMA_LIST_CORNER_RADIUS_DP 8
#define AROMA_LIST_SELECTED_CORNER_RADIUS_DP 6
#define AROMA_LIST_SEPARATOR_HEIGHT_DP 1
#define AROMA_LIST_TRAILING_REVEAL_DP 160
#define AROMA_MATERIAL_COLOR_COUNT 16
#define AROMA_LISTVIEW_LONG_PRESS_TIMEOUT_MS 500

#ifdef __ANDROID__
static inline int list_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float list_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int list_dp(int dp) { return dp; }
static inline float list_dp_f(float dp) { return dp; }
#endif

static long __listview_press_elapsed_ms(const struct timespec *down,
                                        const struct timespec *up)
{
    return (long)(up->tv_sec - down->tv_sec) * 1000L +
           (long)(up->tv_nsec - down->tv_nsec) / 1000000L;
}

typedef struct
{
    AromaRect rect;
    AromaFont *font;
    AromaFont *secondary_font;
    AromaFont *icon_font;
    AromaNode *self_node;

    void (*callback)(int index, void *user_data);
    void *user_data;
    void (*long_press_callback)(int index, void *user_data);
    void *long_press_user_data;
    struct timespec press_down_ts;
    bool press_down_valid;
    size_t item_count;

    uint32_t header_bg_color;
    uint32_t header_text_color;

    int selected_index;
    int pressed_index;
    int item_height;
    int active_pointer_id;
    int bottom_padding;
    int viewport_height;
    int content_height;

    float corner_radius;
    float selected_corner_radius;
    float text_scale;
    float secondary_text_scale;

    bool show_headers;
    bool use_theme_colors;
    uint8_t _padding[2];

    uint8_t item_types[AROMA_LIST_MAX_ITEMS];
    bool item_hidden[AROMA_LIST_MAX_ITEMS];
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
        return list_dp(AROMA_LIST_MIN_ITEM_HEIGHT_DP);
    if (!item_in_range(list, i))
        return list->item_height;
    if (is_header(list, i))
        return list->item_height / 2;
    if (is_separator(list, i))
        return list_dp(AROMA_LIST_SEPARATOR_HEIGHT_DP);
    if (list->items[i].secondary_text[0] != '\0')
        return (int)(list->item_height * 1.5f);
    return list->item_height;
}

static int total_content_height(const AromaListViewInternal *list)
{
    int h = 0;
    for (size_t i = 0; i < list->item_count; i++)
    {
        if (list->item_hidden[i])
            continue;
        h += item_height_at(list, (int)i);
    }
    return h;
}

static void update_content_height(AromaListViewInternal *list)
{
    if (!list)
        return;
    int h = total_content_height(list) + list->bottom_padding;
    list->content_height = h > 0 ? h : 1;
    list->rect.height = list->viewport_height > 0 ? list->viewport_height : 1;



    if (list->self_node && list->self_node->parent_node &&
        aroma_container_is_scrollable(list->self_node->parent_node))
    {
        AromaNode *scroll_container = list->self_node->parent_node;
        AromaRect *viewport = aroma_node_get_rect(scroll_container);
        int content_x = list->rect.x;
        int content_y = list->rect.y;
        if (viewport)
        {
            content_x -= viewport->x;
            content_y -= viewport->y;





            if (content_x < 0)
                content_x = 0;
            if (content_y < 0)
                content_y = 0;
        }




        int content_width = content_x + list->rect.width;





        int trailing_reveal = list_dp(AROMA_LIST_TRAILING_REVEAL_DP);
        int content_height = content_y + list->content_height + trailing_reveal;
        if (viewport)
        {
            if (content_width < viewport->width)
                content_width = viewport->width;
            if (content_height < viewport->height)
                content_height = viewport->height;
        }
        aroma_container_set_content_size(scroll_container,
                                         content_width, content_height);
    }
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

static void list_screen_origin(const AromaNode *node, const AromaListViewInternal *list,
                               int *x, int *y)
{
    if (!list || !x || !y)
        return;

    *x = list->rect.x;
    *y = list->rect.y;
    const AromaNode *cur = node ? node->parent_node : NULL;
    while (cur)
    {
        if (cur->node_type == NODE_TYPE_CONTAINER &&
            aroma_container_is_scrollable((AromaNode *)cur))
        {
            int scroll_x = 0;
            int scroll_y = 0;
            aroma_container_get_scroll((AromaNode *)cur, &scroll_x, &scroll_y);
            *x -= scroll_x;
            *y -= scroll_y;
        }
        cur = cur->parent_node;
    }
}

static void node_screen_origin(const AromaNode *node, const AromaRect *rect,
                               int *x, int *y)
{
    if (!node || !rect || !x || !y)
        return;

    *x = rect->x;
    *y = rect->y;
    for (const AromaNode *cur = node->parent_node; cur; cur = cur->parent_node)
    {
        if (cur->node_type == NODE_TYPE_CONTAINER &&
            aroma_container_is_scrollable((AromaNode *)cur))
        {
            int sx = 0;
            int sy = 0;
            aroma_container_get_scroll((AromaNode *)cur, &sx, &sy);
            *x -= sx;
            *y -= sy;
        }
    }
}

static int hit_test(AromaNode *node, const AromaListViewInternal *list, int screen_y)
{
    if (!list)
        return -1;

    int list_x = 0;
    int y = 0;
    list_screen_origin(node, list, &list_x, &y);
    (void)list_x;

    for (size_t i = 0; i < list->item_count; i++)
    {
        if (list->item_hidden[i])
        {
            continue;
        }
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
    int screen_x = 0;
    int screen_y = 0;
    list_screen_origin(node, list, &screen_x, &screen_y);
    bounds.x = screen_x;
    bounds.y = screen_y;







    AromaNode *scroll_container = node->parent_node;
    if (scroll_container &&
        aroma_container_is_scrollable(scroll_container))
    {
        AromaRect *viewport = aroma_node_get_rect(scroll_container);
        if (viewport)
        {
            bounds.x = viewport->x;
            bounds.y = viewport->y;
            bounds.width = viewport->width;
            bounds.height = viewport->height;
        }
    }

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
            list->press_down_ts = ev->timestamp;
            list->press_down_valid = true;
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
            list->press_down_ts = ev->timestamp;
            list->press_down_valid = true;
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
        bool long_press = false;
        if (activated && list->press_down_valid && list->long_press_callback)
        {
            long elapsed_ms = __listview_press_elapsed_ms(&list->press_down_ts,
                                                           &ev->timestamp);
            long_press = (elapsed_ms >= AROMA_LISTVIEW_LONG_PRESS_TIMEOUT_MS);
        }
        list->pressed_index = -1;
        list->press_down_valid = false;
        aroma_node_invalidate(node);
        if (activated && is_selectable(list, hit))
        {
            if (long_press && list->long_press_callback)
            {
                list->long_press_callback(selectable_index(list, hit),
                                          list->long_press_user_data);
                return true;
            }
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
        bool long_press = false;
        if (activated && list->press_down_valid && list->long_press_callback)
        {
            long elapsed_ms = __listview_press_elapsed_ms(&list->press_down_ts,
                                                           &ev->timestamp);
            long_press = (elapsed_ms >= AROMA_LISTVIEW_LONG_PRESS_TIMEOUT_MS);
        }
        list->pressed_index = -1;
        list->press_down_valid = false;
        aroma_node_invalidate(node);
        if (activated && is_selectable(list, hit))
        {
            if (long_press && list->long_press_callback)
            {
                list->long_press_callback(selectable_index(list, hit),
                                          list->long_press_user_data);
                return true;
            }
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
    list->viewport_height = height;
    list->content_height = height;
    list->selected_index = -1;
    list->pressed_index = -1;
    list->active_pointer_id = -1;
    list->item_height = list_dp(AROMA_LIST_MIN_ITEM_HEIGHT_DP);
    list->corner_radius = list_dp_f((float)AROMA_LIST_CORNER_RADIUS_DP);
    list->selected_corner_radius = list_dp_f((float)AROMA_LIST_SELECTED_CORNER_RADIUS_DP);
    list->text_scale = 1.0f;
    list->secondary_text_scale = 0.8f;
    list->show_headers = true;
    list->use_theme_colors = true;

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

AromaNode *aroma_listview_get_scroll_container(AromaNode *list_node)
{
    if (!list_node) return NULL;
    return list_node->parent_node;
}

int aroma_listview_get_content_height(AromaNode *list_node)
{
    if (!list_node) return 0;
    AromaListViewInternal *list = get_internal(list_node);
    if (!list) return 0;
    return list->content_height;
}

void aroma_listview_refresh_scroll_extent(AromaNode *list_node)
{
    AromaListViewInternal *list = get_internal(list_node);
    if (!list)
        return;

    AromaNode *scroll_container = list_node->parent_node;
    if (scroll_container &&
        aroma_container_is_scrollable(scroll_container))
    {
        AromaRect *viewport = aroma_node_get_rect(scroll_container);
        if (viewport && viewport->height > 0)
            list->viewport_height = viewport->height;
    }

    update_content_height(list);
    aroma_node_invalidate(list_node);
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

void aroma_listview_set_item_hidden(AromaNode *node, int index, bool hidden)
{
    if (!node) return;
    AromaListViewInternal *list = get_internal(node);
    if (!list || !item_in_range(list, index)) return;
    if (list->item_hidden[index] == hidden) return;
    list->item_hidden[index] = hidden;
    update_content_height(list);
    aroma_node_invalidate(node);
}

bool aroma_listview_is_item_hidden(AromaNode *node, int index)
{
    if (!node) return false;
    AromaListViewInternal *list = get_internal(node);
    if (!list || !item_in_range(list, index)) return false;
    return list->item_hidden[index];
}

const char *aroma_listview_get_item_text(AromaNode *node, int index)
{
    if (!node) return NULL;
    AromaListViewInternal *list = get_internal(node);
    if (!list || !item_in_range(list, index)) return NULL;
    return list->items[index].text;
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
        if (l->item_hidden[i])
            continue;
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

void aroma_listview_set_long_press_callback(AromaNode *n,
                                            void (*cb)(int, void *), void *ud)
{
    AromaListViewInternal *l = get_internal(n);
    if (l)
    {
        l->long_press_callback = cb;
        l->long_press_user_data = ud;
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
                            : list_dp(AROMA_LIST_MIN_ITEM_HEIGHT_DP);
    if (list->item_height < list_dp(AROMA_LIST_MIN_ITEM_HEIGHT_DP))
        list->item_height = list_dp(AROMA_LIST_MIN_ITEM_HEIGHT_DP);
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
        l->use_theme_colors = false;
        aroma_node_invalidate(n);
    }
    }

    void aroma_listview_set_bottom_padding(AromaNode *node, int padding)
    {
        if (!node)
            return;
        AromaListViewInternal *list = get_internal(node);
        if (!list)
            return;
        if (padding < 0)
            padding = 0;
        list->bottom_padding = padding;
        update_content_height(list);
        aroma_node_invalidate(node);
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
    if (list->use_theme_colors)
    {
        list->header_bg_color = aroma_color_blend(theme.colors.surface,
                                                  theme.colors.primary, 0.1f);
        list->header_text_color = theme.colors.text_secondary;
    }
    int width = list->rect.width;

    int current_y = list->rect.y;
    int screen_list_x = 0;
    int screen_list_y = 0;
    list_screen_origin(node, list, &screen_list_x, &screen_list_y);
    int viewport_top = screen_list_y;
    int viewport_bottom = screen_list_y + list->viewport_height;
    if (node->parent_node &&
        aroma_container_is_scrollable(node->parent_node))
    {
        AromaRect *viewport = aroma_node_get_rect(node->parent_node);
        if (viewport)
        {
            int viewport_x = 0;
            node_screen_origin(node->parent_node, viewport,
                               &viewport_x, &viewport_top);
            viewport_bottom = viewport_top + viewport->height;
        }
    }
    int primary_lh = aroma_font_get_line_height(list->font);

    for (size_t i = 0; i < list->item_count; i++)
    {
        if (list->item_hidden[i])
            continue;

        int ih = item_height_at(list, (int)i);
        int screen_row_y = screen_list_y + (current_y - list->rect.y);
        if (screen_row_y + ih <= viewport_top ||
            screen_row_y >= viewport_bottom)
        {
            current_y += ih;
            continue;
        }

        bool hdr = is_header(list, (int)i);
        bool sep = is_separator(list, (int)i);
        bool selected = ((int)i == list->selected_index);
        bool pressed = ((int)i == list->pressed_index);

        if (sep)
        {
            gfx->fill_rectangle(window_id,
                                list->rect.x + list_dp(AROMA_LIST_ITEM_PADDING_DP),
                                current_y + ih / 2,
                                width - list_dp(AROMA_LIST_ITEM_PADDING_DP) * 2, list_dp(AROMA_LIST_SEPARATOR_HEIGHT_DP),
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
                                list->rect.x + list_dp(2), current_y, width - list_dp(4), ih,
                                hi, true, list->selected_corner_radius);
        }

        int text_x = list->rect.x + list_dp(AROMA_LIST_ITEM_PADDING_DP);

        if (list->items[i].icon[0] != '\0' && list->icon_font)
        {
            int icon_sz = (int)(aroma_font_get_px_size(list->icon_font) * 0.6f);
            int icon_y = current_y + (ih - icon_sz) / 2;
            uint32_t col = AROMA_MATERIAL_COLORS[i % AROMA_MATERIAL_COLOR_COUNT];

            gfx->fill_rectangle(window_id,
                                text_x - list_dp(4),
                                current_y + (ih - icon_sz) / 2 - list_dp(4),
                                icon_sz + list_dp(8), icon_sz + list_dp(8),
                                aroma_color_blend(col, theme.colors.surface, 0.4f),
                                true, icon_sz / 2 + list_dp(4));

            gfx->render_text(window_id, list->icon_font,
                             list->items[i].icon, text_x, icon_y,
                             hdr ? list->header_text_color : theme.colors.text_primary,
                             0.6f);

            text_x += icon_sz + list_dp(AROMA_LIST_ICON_PADDING_DP);
        }

        bool has_secondary = !hdr && list->items[i].secondary_text[0] != '\0';

        if (list->items[i].text[0] != '\0')
        {
            int text_y;
            if (has_secondary)
            {
                int sec_lh = (int)(primary_lh * list->secondary_text_scale);
                int total = primary_lh + sec_lh + list_dp(4);
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
            int total = primary_lh + sec_lh + list_dp(4);
            int text_y = current_y + (ih - total) / 2;
            int sec_y = text_y + primary_lh + list_dp(2);
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