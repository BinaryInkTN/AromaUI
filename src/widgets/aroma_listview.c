#include "widgets/aroma_listview.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "core/aroma_timer.h"
#include "core/aroma_time.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define AROMA_LIST_MAX_ITEMS 64
#define AROMA_LIST_ITEM_PADDING 12
#define AROMA_LIST_ICON_PADDING 12
#define AROMA_LIST_MIN_ITEM_HEIGHT 28

/* ── scroll physics constants ──────────────────────────────────────────── */
#define LV_SCROLL_SLOP          8
#define LV_FLING_FRICTION       0.97f
#define LV_FLING_MIN_VELOCITY   0.5f
#define LV_FLING_TICK_MS        16
#define LV_OVERSCROLL_RESIST    0.35f
#define LV_OVERSCROLL_MAX_PX    100
#define LV_BOUNCE_DURATION_MS   250
#define LV_SCROLLBAR_WIDTH      4
#define LV_SCROLLBAR_MIN_THUMB  24
#define LV_SCROLLBAR_PADDING    2
#define LV_SCROLLBAR_FADE_DELAY 1200
#define LV_SCROLLBAR_COLOR      0x88888880


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

    /* ── internal scroll ───────────────────────────────────────────── */
    float scroll_fy;               /* current scroll offset (float)     */
    float velocity_y;              /* fling velocity                    */
    float overscroll_y;            /* rubber-band offset                */
    float scroll_speed;

    bool is_dragging;
    int drag_start_y;
    float drag_scroll_start_y;
    int last_touch_y;
    uint64_t last_touch_time_ms;
    int scroll_pointer_id;         /* separate from item-tap pointer    */

    AromaTimer* fling_timer;
    AromaNode* self_node;

    bool bouncing;
    float bounce_start_y;
    uint64_t bounce_start_time;

    uint64_t last_scroll_time;
    float scrollbar_opacity;
} AromaListViewInternal;

const uint32_t AROMA_MATERIAL_COLORS[] = {
    0xF44336, // Red
    0xE91E63, // Pink
    0x9C27B0, // Purple
    0x673AB7, // Deep Purple
    0x3F51B5, // Indigo
    0x2196F3, // Blue
    0x03A9F4, // Light Blue
    0x00BCD4, // Cyan
    0x009688, // Teal
    0x4CAF50, // Green
    0x8BC34A, // Light Green
    0xCDDC39, // Lime
    0xFFEB3B, // Yellow
    0xFFC107, // Amber
    0xFF9800, // Orange
    0xFF5722, // Deep Orange
};

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

/* ── listview scroll helpers ───────────────────────────────────────────── */

static int lv_total_content_height(const AromaListViewInternal* list)
{
    int total = 0;
    for (size_t i = 0; i < list->item_count; i++)
        total += __get_item_height(list, i);
    return total;
}

static int lv_max_scroll(const AromaListViewInternal* list)
{
    int m = lv_total_content_height(list) - list->rect.height;
    return m > 0 ? m : 0;
}

static void lv_clamp_scroll(AromaListViewInternal* list)
{
    float mx = (float)lv_max_scroll(list);
    if (list->scroll_fy < 0.0f) list->scroll_fy = 0.0f;
    if (list->scroll_fy > mx)   list->scroll_fy = mx;
}

static inline int lv_scroll_int(const AromaListViewInternal* list) {
    return (int)roundf(list->scroll_fy);
}

static inline int lv_effective_scroll(const AromaListViewInternal* list) {
    return lv_scroll_int(list) + (int)roundf(list->overscroll_y);
}

static bool lv_can_scroll(const AromaListViewInternal* list) {
    return lv_total_content_height(list) > list->rect.height;
}

static void lv_stop_fling(AromaListViewInternal* list)
{
    if (list->fling_timer) {
        aroma_timer_cancel(list->fling_timer);
        list->fling_timer = NULL;
    }
    list->bouncing = false;
}

static void lv_ensure_animation_timer(AromaListViewInternal* list);

static void lv_fling_tick(void* user_data)
{
    AromaNode* node = (AromaNode*)user_data;
    AromaListViewInternal* list = get_listview_internal(node);
    if (!list) return;

    bool still_moving = false;
    uint64_t now = aroma_time_now_ms();

    /* bounce-back */
    if (list->bouncing) {
        float elapsed = (float)(now - list->bounce_start_time);
        float t = elapsed / (float)LV_BOUNCE_DURATION_MS;
        if (t >= 1.0f) t = 1.0f;
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        list->overscroll_y = list->bounce_start_y * (1.0f - ease);
        if (t >= 1.0f) {
            list->overscroll_y = 0.0f;
            list->bouncing = false;
        } else {
            still_moving = true;
        }
        aroma_node_invalidate(node);
    }

    /* fling */
    if (!list->bouncing && !list->is_dragging) {
        list->velocity_y *= LV_FLING_FRICTION;
        bool vel_active = false;
        if (fabsf(list->velocity_y) > LV_FLING_MIN_VELOCITY) {
            list->scroll_fy += list->velocity_y;
            vel_active = true;
        } else {
            list->velocity_y = 0.0f;
        }

        float my = (float)lv_max_scroll(list);
        if (list->scroll_fy < 0.0f) {
            list->overscroll_y = list->scroll_fy;
            list->scroll_fy = 0.0f;
            list->velocity_y = 0.0f;
        } else if (list->scroll_fy > my) {
            list->overscroll_y = list->scroll_fy - my;
            list->scroll_fy = my;
            list->velocity_y = 0.0f;
        }

        if ((list->overscroll_y != 0.0f) && !list->bouncing) {
            list->bouncing = true;
            list->bounce_start_y = list->overscroll_y;
            list->bounce_start_time = now;
            still_moving = true;
        } else if (vel_active) {
            still_moving = true;
        }

        if (vel_active) {
            list->last_scroll_time = now;
            aroma_node_invalidate(node);
        }
    }

    /* scrollbar fade */
    {
        uint64_t since = now - list->last_scroll_time;
        float target = (list->is_dragging || still_moving || since < LV_SCROLLBAR_FADE_DELAY) ? 1.0f : 0.0f;
        float speed = (target > list->scrollbar_opacity) ? 0.3f : 0.08f;
        list->scrollbar_opacity += (target - list->scrollbar_opacity) * speed;
        if (list->scrollbar_opacity < 0.01f) list->scrollbar_opacity = 0.0f;
        if (list->scrollbar_opacity > 0.99f) list->scrollbar_opacity = 1.0f;
        if (list->scrollbar_opacity > 0.0f) still_moving = true;
        aroma_node_invalidate(node);
    }

    if (!still_moving) lv_stop_fling(list);
}

static void lv_ensure_animation_timer(AromaListViewInternal* list)
{
    if (!list->fling_timer && list->self_node) {
        list->fling_timer = aroma_timer_create(
            LV_FLING_TICK_MS, true, lv_fling_tick, list->self_node);
    }
}

/* ── item hit-test (accounts for scroll offset) ───────────────────────── */

static int lv_hit_test_item(const AromaListViewInternal* list, int screen_y)
{
    int eff_scroll = lv_effective_scroll(list);
    int rel_y = screen_y - list->rect.y + eff_scroll;  /* content-space y */
    int current_y = 0;
    for (size_t i = 0; i < list->item_count; i++) {
        int ih = __get_item_height(list, i);
        if (rel_y >= current_y && rel_y < current_y + ih)
            return (int)i;
        current_y += ih;
    }
    return -1;
}

/* ── listview event handler (tap + scroll) ─────────────────────────────── */

static bool __listview_handle_event(AromaEvent *event, void *user_data)
{
    (void)user_data;

    if (!event || !event->target_node)
        return false;

    AromaListViewInternal* list = get_listview_internal(event->target_node);
    if (!list) return false;

    AromaNode* node = event->target_node;
    AromaRect bounds = list->rect;

    switch (event->event_type) {

    /* ── PRESS (mouse or touch) ────────────────────────────────────── */
    case EVENT_TYPE_MOUSE_CLICK: {
        if (list->active_pointer_id != -1) return false;
        int x = event->data.mouse.x, y = event->data.mouse.y;
        if (x < bounds.x || x >= bounds.x + bounds.width ||
            y < bounds.y || y >= bounds.y + bounds.height) return false;
        list->active_pointer_id = 0;
        int hit = lv_hit_test_item(list, y);
        if (hit >= 0 && __item_is_selectable(list, hit)) {
            list->pressed_index = hit;
            aroma_node_invalidate(node);
        }
        return true;
    }

    case EVENT_TYPE_TOUCH_DOWN: {
        int tx = event->data.touch.x, ty = event->data.touch.y;
        if (tx < bounds.x || tx >= bounds.x + bounds.width ||
            ty < bounds.y || ty >= bounds.y + bounds.height) return false;

        /* Stop any active fling */
        lv_stop_fling(list);
        list->overscroll_y = 0.0f;
        list->velocity_y = 0.0f;

        list->active_pointer_id = event->data.touch.id;
        list->scroll_pointer_id = event->data.touch.id;
        list->drag_start_y = ty;
        list->drag_scroll_start_y = list->scroll_fy;
        list->last_touch_y = ty;
        list->last_touch_time_ms = aroma_time_now_ms();
        list->is_dragging = false;
        list->scrollbar_opacity = 1.0f;
        list->last_scroll_time = list->last_touch_time_ms;

        int hit = lv_hit_test_item(list, ty);
        if (hit >= 0 && __item_is_selectable(list, hit)) {
            list->pressed_index = hit;
            aroma_node_invalidate(node);
        }
        return false;  /* let intercept mechanism also see it */
    }

    /* ── MOVE (touch scroll) ───────────────────────────────────────── */
    case EVENT_TYPE_TOUCH_MOVE: {
        if (event->data.touch.id != list->scroll_pointer_id) return false;
        int ty = event->data.touch.y;
        int dy = list->drag_start_y - ty;

        if (!list->is_dragging) {
            int abs_dy = dy < 0 ? -dy : dy;
            if (abs_dy < LV_SCROLL_SLOP) return false;
            list->is_dragging = true;
            /* Cancel any pressed item when we start scrolling */
            if (list->pressed_index != -1) {
                list->pressed_index = -1;
                aroma_node_invalidate(node);
            }
        }

        if (!lv_can_scroll(list)) return false;

        float new_sy = list->drag_scroll_start_y + (float)dy * list->scroll_speed;
        float my = (float)lv_max_scroll(list);

        list->overscroll_y = 0.0f;
        if (new_sy < 0.0f) {
            list->overscroll_y = new_sy * LV_OVERSCROLL_RESIST;
            if (list->overscroll_y < -LV_OVERSCROLL_MAX_PX)
                list->overscroll_y = -LV_OVERSCROLL_MAX_PX;
            new_sy = 0.0f;
        } else if (new_sy > my) {
            list->overscroll_y = (new_sy - my) * LV_OVERSCROLL_RESIST;
            if (list->overscroll_y > LV_OVERSCROLL_MAX_PX)
                list->overscroll_y = LV_OVERSCROLL_MAX_PX;
            new_sy = my;
        }

        /* Track velocity */
        uint64_t now = aroma_time_now_ms();
        uint64_t dt = now - list->last_touch_time_ms;
        if (dt > 0 && dt < 200) {
            float vy = (float)(list->last_touch_y - ty) / (float)dt * LV_FLING_TICK_MS;
            list->velocity_y = list->velocity_y * 0.4f + vy * 0.6f;
        }
        list->last_touch_y = ty;
        list->last_touch_time_ms = now;

        list->scroll_fy = new_sy;
        list->last_scroll_time = now;
        aroma_node_invalidate(node);
        return true;
    }

    /* ── RELEASE ───────────────────────────────────────────────────── */
    case EVENT_TYPE_MOUSE_RELEASE: {
        if (list->active_pointer_id != 0) return false;
        list->active_pointer_id = -1;
        int x = event->data.mouse.x, y = event->data.mouse.y;
        bool in_bounds = x >= bounds.x && x < bounds.x + bounds.width &&
                         y >= bounds.y && y < bounds.y + bounds.height;
        int hit = in_bounds ? lv_hit_test_item(list, y) : -1;
        bool was_pressed = (list->pressed_index == hit && hit >= 0);
        list->pressed_index = -1;
        aroma_node_invalidate(node);
        if (was_pressed && __item_is_selectable(list, hit)) {
            list->selected_index = hit;
            if (list->callback) list->callback(hit, list->user_data);
            AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
            if (platform && platform->android_vibrate) platform->android_vibrate(60);
            return true;
        }
        return false;
    }

    case EVENT_TYPE_TOUCH_UP: {
        if (event->data.touch.id != list->active_pointer_id) return false;
        list->active_pointer_id = -1;
        list->scroll_pointer_id = -1;

        bool was_dragging = list->is_dragging;
        list->is_dragging = false;

        if (was_dragging) {
            /* Clamp and start fling */
            float max_vel = 40.0f;
            if (list->velocity_y >  max_vel) list->velocity_y =  max_vel;
            if (list->velocity_y < -max_vel) list->velocity_y = -max_vel;
            if (list->overscroll_y != 0.0f) {
                list->bouncing = true;
                list->bounce_start_y = list->overscroll_y;
                list->bounce_start_time = aroma_time_now_ms();
                list->velocity_y = 0.0f;
            }
            list->last_scroll_time = aroma_time_now_ms();
            lv_ensure_animation_timer(list);
            list->pressed_index = -1;
            aroma_node_invalidate(node);
            return true;
        }

        /* It was a tap, not a drag */
        int ty = event->data.touch.y;
        int hit = lv_hit_test_item(list, ty);
        bool was_pressed = (list->pressed_index == hit && hit >= 0);
        list->pressed_index = -1;
        aroma_node_invalidate(node);
        if (was_pressed && __item_is_selectable(list, hit)) {
            list->selected_index = hit;
            if (list->callback) list->callback(hit, list->user_data);
            AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
            if (platform && platform->android_vibrate) platform->android_vibrate(60);
            return true;
        }
        return false;
    }

    /* ── mouse wheel scroll ────────────────────────────────────────── */
    case EVENT_TYPE_MOUSE_MOVE: {
        int mx = event->data.mouse.x, my_pos = event->data.mouse.y;
        if (mx < bounds.x || mx >= bounds.x + bounds.width ||
            my_pos < bounds.y || my_pos >= bounds.y + bounds.height) return false;
        int delta = event->data.mouse.delta_y;
        if (delta == 0 || !lv_can_scroll(list)) return false;
        float old = list->scroll_fy;
        list->scroll_fy += (float)delta * list->scroll_speed;
        lv_clamp_scroll(list);
        if (list->scroll_fy != old) {
            list->last_scroll_time = aroma_time_now_ms();
            list->scrollbar_opacity = 1.0f;
            lv_ensure_animation_timer(list);
            aroma_node_invalidate(node);
        }
        return true;
    }

    default:
        return false;
    }
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

    /* Scroll defaults */
    list->scroll_fy = 0.0f;
    list->velocity_y = 0.0f;
    list->overscroll_y = 0.0f;
    list->scroll_speed = 1.0f;
    list->is_dragging = false;
    list->scroll_pointer_id = -1;
    list->fling_timer = NULL;
    list->bouncing = false;
    list->scrollbar_opacity = 0.0f;

    
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

    /* Store self_node for timer callback */
    list->self_node = node;

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
                          EVENT_TYPE_MOUSE_MOVE,
                          __listview_handle_event,
                          NULL,
                          90);
    aroma_event_subscribe(node->node_id,
                          EVENT_TYPE_TOUCH_DOWN,
                          __listview_handle_event,
                          NULL,
                          90);
    aroma_event_subscribe(node->node_id,
                          EVENT_TYPE_TOUCH_MOVE,
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

    /* Background (drawn before scissor so corners aren't clipped) */
    gfx->fill_rectangle(window_id,
                        list->rect.x, list->rect.y,
                        width, height,
                        theme.colors.background,
                        true,
                        list->corner_radius);

    /* Enable scissor clipping */
    if (gfx->graphics_set_clip) {
        gfx->graphics_set_clip(list->rect.x, list->rect.y, width, height);
    }

    int eff_scroll = lv_effective_scroll(list);
    int current_y = list->rect.y - eff_scroll;

    for (size_t i = 0; i < list->item_count; ++i)
    {
        int item_height = __get_item_height(list, i);
        
        /* Skip items above visible area */
        if (current_y + item_height <= list->rect.y) {
            current_y += item_height;
            continue;
        }
        
        /* Stop when past visible area */
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
                                                theme.colors.surface, 0.35f),
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
                int icon_lh = aroma_font_get_px_size(list->icon_font) * 0.6f;
                int icon_y = current_y + (item_height - icon_lh) / 2;
                
                const size_t color_index = i % (sizeof(AROMA_MATERIAL_COLORS) / sizeof(AROMA_MATERIAL_COLORS[0]));
                uint32_t icon_color = AROMA_MATERIAL_COLORS[color_index];

                gfx->fill_rectangle(window_id,
                                   text_x - 4,
                                   current_y + (item_height - icon_lh) / 2 - 4,
                                   icon_lh + 8,
                                   icon_lh + 8,
                                   aroma_color_blend(icon_color, theme.colors.surface, 0.4f),
                                   true,
                                   icon_lh / 2 + 4);

                gfx->render_text(window_id,
                                 list->icon_font,
                                 list->items[i].icon,
                                 text_x,
                                 icon_y,
                                 is_header ? list->header_text_color : theme.colors.text_primary,
                                 0.6f);

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

    /* Disable scissor */
    if (gfx->graphics_clear_clip) {
        gfx->graphics_clear_clip();
    }

    /* ── Scrollbar indicator (drawn outside scissor) ──────────────── */
    if (lv_can_scroll(list) && list->scrollbar_opacity > 0.01f) {
        int content_h = lv_total_content_height(list);
        if (content_h > 0) {
            float ratio = (float)height / (float)content_h;
            int thumb_h = (int)(ratio * height);
            if (thumb_h < LV_SCROLLBAR_MIN_THUMB) thumb_h = LV_SCROLLBAR_MIN_THUMB;
            if (thumb_h > height) thumb_h = height;

            int max_s = lv_max_scroll(list);
            float scroll_ratio = (max_s > 0) ? list->scroll_fy / (float)max_s : 0.0f;
            if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
            if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
            int track_h = height - thumb_h;
            int thumb_y = list->rect.y + (int)(scroll_ratio * track_h);

            int sb_x = list->rect.x + width - LV_SCROLLBAR_WIDTH - LV_SCROLLBAR_PADDING;

            /* Apply opacity to scrollbar color */
            uint32_t sb_color = LV_SCROLLBAR_COLOR;
            uint8_t base_alpha = (sb_color >> 0) & 0xFF;
            uint8_t final_alpha = (uint8_t)(base_alpha * list->scrollbar_opacity);
            sb_color = (sb_color & 0xFFFFFF00) | final_alpha;

            gfx->fill_rectangle(window_id,
                               sb_x, thumb_y,
                               LV_SCROLLBAR_WIDTH, thumb_h,
                               sb_color,
                               true,
                               LV_SCROLLBAR_WIDTH / 2.0f);
        }
    }
}

void aroma_listview_destroy(AromaNode *list_node)
{
    if (!list_node) return;

    if (list_node->node_widget_ptr) {
        AromaListViewInternal* list = get_listview_internal(list_node);
        if (list) lv_stop_fling(list);
        aroma_widget_free(list_node->node_widget_ptr);
        list_node->node_widget_ptr = NULL;
    }
}