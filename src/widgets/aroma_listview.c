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

#define AROMA_LIST_MAX_ITEMS 64
#define AROMA_LIST_ITEM_PADDING 12
#define AROMA_LIST_ICON_PADDING 12
#define AROMA_LIST_MIN_ITEM_HEIGHT 28


typedef struct {
    AromaRect rect;
    AromaListItem items[AROMA_LIST_MAX_ITEMS];
    size_t item_count;
    int selected_index;
    int pressed_index;
    AromaFont *font;
    AromaFont *icon_font;
    AromaFont *secondary_font;
    void (*callback)(int index, void *user_data);
    void *user_data;
    int item_height;
    float corner_radius;
    float selected_corner_radius;
    float text_scale;
    float secondary_text_scale;
    int active_pointer_id;
    bool show_headers;
    uint32_t header_bg_color;
    uint32_t header_text_color;
    uint8_t item_types[AROMA_LIST_MAX_ITEMS];
} AromaListViewInternal;

static inline AromaListViewInternal* get_listview_internal(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return NULL;
    return (AromaListViewInternal*)node->node_widget_ptr;
}

static bool __is_header_item(const AromaListViewInternal* list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count) return false;
    return (list->item_types[index] == AROMA_LIST_ITEM_HEADER);
}

static bool __is_separator_item(const AromaListViewInternal* list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count) return false;
    return (list->item_types[index] == AROMA_LIST_ITEM_SEPARATOR);
}

static bool __item_is_selectable(const AromaListViewInternal* list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count) return false;
    return (list->item_types[index] == AROMA_LIST_ITEM_NORMAL);
}

static int __get_item_height(const AromaListViewInternal* list, int index)
{
    if (!list || index < 0 || index >= (int)list->item_count) return list->item_height;
    
    if (__is_header_item(list, index)) {
        return list->item_height / 2;
    }
    
    if (__is_separator_item(list, index)) {
        return 1;
    }
    
    if (list->items[index].secondary_text[0] != '\0') {
        return (int)(list->item_height * 1.5f);
    }
    
    return list->item_height;
}

static bool __listview_handle_event(AromaEvent *event, void *user_data)
{
    (void)user_data;

    if (!event || !event->target_node)
        return false;

    AromaListViewInternal* list = get_listview_internal(event->target_node);
    if (!list)
        return false;

    int x = 0, y = 0;
    bool is_press = false;
    bool is_release = false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            if (list->active_pointer_id != -1) return false;
            x = event->data.mouse.x;
            y = event->data.mouse.y;
            is_press = true;
            list->active_pointer_id = 0;
            break;
            
        case EVENT_TYPE_MOUSE_RELEASE:
            if (list->active_pointer_id != 0) return false;
            x = event->data.mouse.x;
            y = event->data.mouse.y;
            is_release = true;
            list->active_pointer_id = -1;
            break;
            
        case EVENT_TYPE_TOUCH_DOWN:
            if (list->active_pointer_id == -1) {
                list->active_pointer_id = event->data.touch.id;
                x = event->data.touch.x;
                y = event->data.touch.y;
                is_press = true;
            }
            break;
            
        case EVENT_TYPE_TOUCH_UP:
            if (list->active_pointer_id == event->data.touch.id) {
                list->active_pointer_id = -1;
                x = event->data.touch.x;
                y = event->data.touch.y;
                is_release = true;
            }
            break;
            
        default:
            return false;
    }

    AromaRect bounds = list->rect;

    if (x < bounds.x || x >= bounds.x + bounds.width ||
        y < bounds.y || y >= bounds.y + bounds.height)
    {
        if (is_release) {
            list->active_pointer_id = -1;
            if (list->pressed_index != -1) {
                list->pressed_index = -1;
                aroma_node_invalidate(event->target_node);
            }
        }
        return false;
    }

    int rel_y = y - bounds.y;
    int current_y = 0;
    int hit_index = -1;
    
    for (size_t i = 0; i < list->item_count; i++) {
        int item_height = __get_item_height(list, i);
        
        if (rel_y >= current_y && rel_y < current_y + item_height) {
            hit_index = (int)i;
            break;
        }
        current_y += item_height;
    }

    if (hit_index < 0 || hit_index >= (int)list->item_count) {
        if (is_release) {
            list->pressed_index = -1;
            aroma_node_invalidate(event->target_node);
        }
        return false;
    }

    if (is_press) {
        if (__item_is_selectable(list, hit_index)) {
            list->pressed_index = hit_index;
            aroma_node_invalidate(event->target_node);
        }
        return true;
    }

    if (is_release) {
        bool was_pressed = (list->pressed_index == hit_index);
        list->pressed_index = -1;
        aroma_node_invalidate(event->target_node);
        
        if (was_pressed && __item_is_selectable(list, hit_index)) {
            list->selected_index = hit_index;

            if (list->callback)
                list->callback(hit_index, list->user_data);

            AromaPlatformInterface *platform =
                aroma_backend_abi.get_platform_interface();
            if (platform && platform->android_vibrate)
                platform->android_vibrate(60);

            return true;
        }
    }

    return false;
}

AromaNode *aroma_listview_create(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
        return NULL;

    
    AromaListViewInternal* list = 
        (AromaListViewInternal*)aroma_widget_alloc(sizeof(AromaListViewInternal));

    if (!list)
        return NULL;

    memset(list, 0, sizeof(AromaListViewInternal));

    list->rect.x = x;
    list->rect.y = y;
    list->rect.width = width;
    list->rect.height = height;

    list->selected_index = -1;
    list->pressed_index = -1;
    list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    list->corner_radius = 8.0f;
    list->selected_corner_radius = 6.0f;
    list->text_scale = 1.0f;
    list->secondary_text_scale = 0.8f;
    list->active_pointer_id = -1;
    list->show_headers = true;

    
    for (int i = 0; i < AROMA_LIST_MAX_ITEMS; i++) {
        list->item_types[i] = AROMA_LIST_ITEM_NORMAL;
    }

    AromaTheme theme = aroma_theme_get_global();
    list->header_bg_color = aroma_color_blend(theme.colors.surface, 
                                              theme.colors.primary, 0.1f);
    list->header_text_color = theme.colors.text_secondary;

    AromaNode *node =
        __add_child_node(NODE_TYPE_WIDGET, parent, list);

    if (!node)
    {
        aroma_widget_free(list);
        return NULL;
    }

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

void aroma_listview_add_item(AromaNode *list_node, const char *text, const char *secondary_text, void *user_data)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list || !text) return;

    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));

    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';

    if (secondary_text) {
        strncpy(item->secondary_text, secondary_text, sizeof(item->secondary_text) - 1);
        item->secondary_text[sizeof(item->secondary_text) - 1] = '\0';
    }

    item->user_data = user_data;
    list->item_types[list->item_count] = AROMA_LIST_ITEM_NORMAL;
    list->item_count++;

    aroma_node_invalidate(list_node);
}

void aroma_listview_add_item_with_icon(AromaNode *list_node, const char *text, const char *secondary_text, const char *icon_code, void *user_data)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list || !text) return;

    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));

    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';

    if (secondary_text) {
        strncpy(item->secondary_text, secondary_text, sizeof(item->secondary_text) - 1);
        item->secondary_text[sizeof(item->secondary_text) - 1] = '\0';
    }
if (icon_code) {
    strncpy(item->icon, icon_code, sizeof(item->icon) - 1);
    item->icon[sizeof(item->icon) - 1] = '\0';
}


    item->user_data = user_data;
    list->item_types[list->item_count] = AROMA_LIST_ITEM_NORMAL;
    list->item_count++;

    aroma_node_invalidate(list_node);
}

void aroma_listview_add_header(AromaNode *list_node, const char *text)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list || !text) return;

    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));

    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    
    list->item_types[list->item_count] = AROMA_LIST_ITEM_HEADER;
    list->item_count++;

    aroma_node_invalidate(list_node);
}

void aroma_listview_add_separator(AromaNode *list_node)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    if (list->item_count >= AROMA_LIST_MAX_ITEMS)
        return;

    AromaListItem *item = &list->items[list->item_count];
    memset(item, 0, sizeof(AromaListItem));
    
    list->item_types[list->item_count] = AROMA_LIST_ITEM_SEPARATOR;
    list->item_count++;

    aroma_node_invalidate(list_node);
}

void aroma_listview_remove_item(AromaNode *list_node, int index)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    if (index < 0 || index >= (int)list->item_count)
        return;

    for (int i = index; i < (int)list->item_count - 1; i++) {
        memcpy(&list->items[i], &list->items[i + 1], sizeof(AromaListItem));
        list->item_types[i] = list->item_types[i + 1];
    }

    list->item_count--;

    if (list->selected_index == index) {
        list->selected_index = -1;
    } else if (list->selected_index > index) {
        list->selected_index--;
    }

    if (list->pressed_index == index) {
        list->pressed_index = -1;
    } else if (list->pressed_index > index) {
        list->pressed_index--;
    }

    aroma_node_invalidate(list_node);
}

void aroma_listview_clear(AromaNode *list_node)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->item_count = 0;
    list->selected_index = -1;
    list->pressed_index = -1;

    aroma_node_invalidate(list_node);
}

int aroma_listview_get_selected(AromaNode *list_node)
{
    if (!list_node) return -1;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return -1;

    return list->selected_index;
}

size_t aroma_listview_get_count(AromaNode *list_node)
{
    if (!list_node) return 0;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return 0;

    return list->item_count;
}

void* aroma_listview_get_item_data(AromaNode *list_node, int index)
{
    if (!list_node) return NULL;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return NULL;

    if (index < 0 || index >= (int)list->item_count)
        return NULL;

    return list->items[index].user_data;
}

void aroma_listview_set_callback(AromaNode *list_node, void (*callback)(int, void *), void *user_data)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->callback = callback;
    list->user_data = user_data;
}

void aroma_listview_set_font(AromaNode *list_node, AromaFont *font)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->font = font;

    if (font) {
        int lh = aroma_font_get_line_height(font);
        list->item_height = (int)(lh * 1.5f);
        if (list->item_height < AROMA_LIST_MIN_ITEM_HEIGHT)
            list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    } else {
        list->item_height = AROMA_LIST_MIN_ITEM_HEIGHT;
    }

    aroma_node_invalidate(list_node);
}

void aroma_listview_set_secondary_font(AromaNode *list_node, AromaFont *font)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->secondary_font = font;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_icon_font(AromaNode *list_node, AromaFont *font)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->icon_font = font;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_item_height(AromaNode *list_node, int height)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    if (height > 0) {
        list->item_height = height;
        aroma_node_invalidate(list_node);
    }
}

void aroma_listview_set_text_scale(AromaNode *list_node, float scale)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->text_scale = scale;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_secondary_text_scale(AromaNode *list_node, float scale)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->secondary_text_scale = scale;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_corner_radius(AromaNode *list_node, float radius)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->corner_radius = radius;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_selected_corner_radius(AromaNode *list_node, float radius)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->selected_corner_radius = radius;
    aroma_node_invalidate(list_node);
}

void aroma_listview_show_headers(AromaNode *list_node, bool show)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->show_headers = show;
    aroma_node_invalidate(list_node);
}

void aroma_listview_set_header_colors(AromaNode *list_node, uint32_t bg_color, uint32_t text_color)
{
    if (!list_node) return;
    
    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    list->header_bg_color = bg_color;
    list->header_text_color = text_color;
    aroma_node_invalidate(list_node);
}

void aroma_listview_draw(AromaNode *list_node, size_t window_id)
{
    if (!list_node) return;
    if (aroma_node_is_hidden(list_node)) return;

    AromaListViewInternal* list = get_listview_internal(list_node);
    if (!list) return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text) return;

    AromaTheme theme = aroma_theme_get_global();

    int width  = list->rect.width;
    int height = list->rect.height;

    gfx->fill_rectangle(window_id,
                        list->rect.x, list->rect.y,
                        width, height,
                        theme.colors.surface,
                        true,
                        list->corner_radius);

    int current_y = list->rect.y;

    for (size_t i = 0; i < list->item_count; ++i)
    {
        int item_height = __get_item_height(list, i);
        
        if (current_y + item_height <= list->rect.y) {
            current_y += item_height;
            continue;
        }
        
        if (current_y >= list->rect.y + height)
            break;

        bool is_header = __is_header_item(list, i);
        bool is_separator = __is_separator_item(list, i);
        bool is_selected = ((int)i == list->selected_index);
        bool is_pressed = ((int)i == list->pressed_index);

        if (is_separator) {
            int separator_y = current_y + item_height / 2;
            gfx->fill_rectangle(window_id,
                               list->rect.x + AROMA_LIST_ITEM_PADDING,
                               separator_y,
                               width - (AROMA_LIST_ITEM_PADDING * 2),
                               1,
                               aroma_color_blend(theme.colors.text_secondary, 
                                                theme.colors.surface, 0.5f),
                               false,
                               0);
            current_y += item_height;
            continue;
        }

        if (is_header && list->show_headers) {
            gfx->fill_rectangle(window_id,
                               list->rect.x,
                               current_y,
                               width,
                               item_height,
                               list->header_bg_color,
                               true,
                               0);
        }

        if ((is_selected || is_pressed) && !is_header) {
            uint32_t highlight_color;
            if (is_pressed) {
                highlight_color = aroma_color_blend(theme.colors.primary,
                                                   theme.colors.surface,
                                                   0.3f);
            } else {
                highlight_color = aroma_color_blend(theme.colors.surface,
                                                   theme.colors.primary_light,
                                                   0.2f);
            }
            
            gfx->fill_rectangle(window_id,
                               list->rect.x + 2,
                               current_y,
                               width - 4,
                               item_height,
                               highlight_color,
                               true,
                               list->selected_corner_radius);
        }

        if (gfx->render_text) {
            int text_x = list->rect.x + AROMA_LIST_ITEM_PADDING;

            if (list->items[i].icon[0] != '\0' && list->icon_font) {
                int icon_lh = aroma_font_get_line_height(list->icon_font);
                int icon_y = current_y + (item_height - icon_lh) / 2;
                
                gfx->render_text(window_id,
                                 list->icon_font,
                                 list->items[i].icon,
                                 text_x,
                                 icon_y,
                                 is_header ? list->header_text_color : theme.colors.text_primary,
                                 1.0f);

                text_x += icon_lh + AROMA_LIST_ICON_PADDING;
            }

            if (list->font && list->items[i].text[0] != '\0') {
                int line_height = aroma_font_get_line_height(list->font);
                int text_y = current_y + (item_height - line_height) / 2;
                
                if (list->items[i].secondary_text[0] != '\0') {
                    text_y = current_y + (item_height / 2) - line_height;
                }
                
                uint32_t text_color = is_header ? list->header_text_color : theme.colors.text_primary;
                
                gfx->render_text(window_id,
                                 list->font,
                                 list->items[i].text,
                                 text_x,
                                 text_y,
                                 text_color,
                                 is_header ? list->secondary_text_scale : list->text_scale);
            }

            if (list->items[i].secondary_text[0] != '\0' && !is_header) {
                AromaFont* sec_font = list->secondary_font ? list->secondary_font : list->font;
                if (sec_font) {
                    int sec_line_height = aroma_font_get_line_height(sec_font);
                    int sec_text_y = current_y + (item_height / 2) + 2;
                    
                    gfx->render_text(window_id,
                                     sec_font,
                                     list->items[i].secondary_text,
                                     text_x,
                                     sec_text_y,
                                     theme.colors.text_secondary,
                                     list->secondary_text_scale);
                }
            }
        }

        current_y += item_height;
    }
}

void aroma_listview_destroy(AromaNode *list_node)
{
    if (!list_node) return;

    if (list_node->node_widget_ptr) {
        aroma_widget_free(list_node->node_widget_ptr);
        list_node->node_widget_ptr = NULL;
    }
}