

#include "widgets/aroma_menu.h"
#include "widgets/aroma_container.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_gesture.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include "aroma_dp.h"
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_MENU_MAX_ITEMS 32
#define AROMA_MENU_WIDTH_DP 200
#define AROMA_MENU_ITEM_HEIGHT_DP 28
#define AROMA_MENU_CORNER_RADIUS_DP 8
#define AROMA_MENU_SEPARATOR_INSET_DP 8
#define AROMA_MENU_TEXT_X_DP 12
#define AROMA_MENU_ICON_OFFSET_DP 24

#ifdef __ANDROID__
static inline int menu_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float menu_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int menu_dp(int dp) { return dp; }
static inline float menu_dp_f(float dp) { return dp; }
#endif

typedef struct   AromaMenu {
    AromaRect rect;
    AromaMenuItem items[AROMA_MENU_MAX_ITEMS];
    size_t item_count;
    bool visible;
    AromaFont* font;
    AromaFont* icon_font;
    int item_height;
    float corner_radius;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t text_color;
    float text_scale;
    bool use_theme_colors;
    AromaGestureObserver* longpress_obs;
    int pressed_index;
} AromaMenu;

#define AROMA_MENU_MAX_OPEN 8

static AromaNode* s_menu_overlays[AROMA_MENU_MAX_OPEN];
static size_t s_menu_overlay_count;

static void __menu_register_overlay(AromaNode* node)
{
    if (!node)
        return;
    for (size_t i = 0; i < s_menu_overlay_count; i++) {
        if (s_menu_overlays[i] == node)
            return;
    }
    if (s_menu_overlay_count >= AROMA_MENU_MAX_OPEN)
        return;
    s_menu_overlays[s_menu_overlay_count++] = node;
}

static void __menu_unregister_overlay(AromaNode* node)
{
    if (!node)
        return;
    for (size_t i = 0; i < s_menu_overlay_count; i++) {
        if (s_menu_overlays[i] == node) {
            s_menu_overlays[i] = s_menu_overlays[--s_menu_overlay_count];
            return;
        }
    }
}

static bool __menu_node_visible(AromaNode* node)
{
    AromaNode* anc = node;
    int depth = 0;
    while (anc && depth < 64) {
        if (anc->is_hidden)
            return false;
        anc = anc->parent_node;
        depth++;
    }
    return true;
}

bool aroma_menu_overlay_hit_test(int x, int y, AromaNode** out_node)
{
    (void)x;
    (void)y;
    for (size_t i = 0; i < s_menu_overlay_count; i++) {
        AromaNode* node = s_menu_overlays[i];
        if (!node || !node->node_widget_ptr || !__menu_node_visible(node))
            continue;
        AromaMenu* menu = (AromaMenu*)node->node_widget_ptr;
        if (!menu->visible)
            continue;
        if (out_node)
            *out_node = node;
        return true;
    }
    return false;
}

static int __menu_item_at(AromaMenu* menu, int x, int y)
{
    if (!menu)
        return -1;
    int rel_y = y - menu->rect.y;
    int rel_x = x - menu->rect.x;
    if (rel_x < 0 || rel_x >= menu->rect.width)
        return -1;
    int index = rel_y / menu->item_height;
    if (index < 0 || index >= (int)menu->item_count)
        return -1;
    if (menu->items[index].separator)
        return -1;
    return index;
}

static void __menu_select_index(AromaNode* menu_node, AromaMenu* menu, int index)
{
    if (index < 0)
        return;
    AromaMenuItem* item = &menu->items[index];
    if (item->enabled && item->callback)
        item->callback(item->user_data);
    menu->visible = false;
    __menu_unregister_overlay(menu_node);
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

static void __menu_dismiss(AromaNode* menu_node, AromaMenu* menu)
{
    menu->visible = false;
    __menu_unregister_overlay(menu_node);
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

static bool __menu_handle_event(AromaEvent* event, void* user_data)
{
    (void)user_data;
    if (!event || !event->target_node) return false;
    AromaMenu* menu = (AromaMenu*)event->target_node->node_widget_ptr;
    if (!menu || !menu->visible) return false;

    if (event->event_type != EVENT_TYPE_MOUSE_CLICK) return false;

    int adjusted_y = event->data.mouse.y;
    AromaNode *cur = event->target_node->parent_node;
    while (cur)
    {
        if (cur->node_type == NODE_TYPE_CONTAINER && aroma_container_is_scrollable(cur))
        {
            int scroll_x = 0, scroll_y = 0;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            adjusted_y += scroll_y;
        }
        cur = cur->parent_node;
    }

    int rel_y = adjusted_y - menu->rect.y;
    int item_height = menu->item_height;
    int index = rel_y / item_height;
    if (index >= 0 && index < (int)menu->item_count) {
        AromaMenuItem* item = &menu->items[index];
        if (!item->separator) {
            if (item->enabled && item->callback)
                item->callback(item->user_data);
            __menu_dismiss(event->target_node, menu);
            return true;
        }
        return true;
    }
    /* Click-away dismiss. */
    __menu_dismiss(event->target_node, menu);
    return true;
}

static int __menu_adjust_touch(AromaEvent* event, int *out_x, int *out_y)
{
    if (!event || !out_x || !out_y)
        return false;
    int x = event->data.touch.x;
    int y = event->data.touch.y;
    AromaNode *cur = event->target_node ? event->target_node->parent_node : NULL;
    while (cur)
    {
        if (cur->node_type == NODE_TYPE_CONTAINER && aroma_container_is_scrollable(cur))
        {
            int scroll_x = 0, scroll_y = 0;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            y += scroll_y;
            x += scroll_x;
        }
        cur = cur->parent_node;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static bool __menu_touch_handler(AromaEvent* event, void* user_data)
{
    (void)user_data;
    if (!event || !event->target_node)
        return false;
    AromaMenu* menu = (AromaMenu*)event->target_node->node_widget_ptr;
    if (!menu || !menu->visible)
        return false;

    if (event->event_type == EVENT_TYPE_TOUCH_DOWN) {
        int x = 0, y = 0;
        if (!__menu_adjust_touch(event, &x, &y))
            return false;
        menu->pressed_index = __menu_item_at(menu, x, y);
        if (menu->pressed_index < 0) {
            __menu_dismiss(event->target_node, menu);
            return true;
        }
        aroma_node_invalidate(event->target_node);
        return true;
    }
    if (event->event_type == EVENT_TYPE_TOUCH_UP) {
        int x = 0, y = 0;
        if (!__menu_adjust_touch(event, &x, &y))
            return false;
        int index = __menu_item_at(menu, x, y);
        if (index >= 0 && index == menu->pressed_index)
            __menu_select_index(event->target_node, menu, index);
        else if (index < 0)
            __menu_dismiss(event->target_node, menu);
        menu->pressed_index = -1;
        return true;
    }
    return false;
}

static void __menu_longpress_cb(const AromaGestureEvent* gev, void* user_data)
{
    AromaNode* menu_node = (AromaNode*)user_data;
    if (!menu_node || !menu_node->node_widget_ptr || !gev)
        return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    AromaNode* parent = menu_node->parent_node;
    int px = 0, py = 0, pw = 0, ph = 0;
    if (parent) {
        AromaRect* pr = aroma_node_get_rect(parent);
        if (pr) {
            px = pr->x;
            py = pr->y;
            pw = pr->width;
            ph = pr->height;
        }
    }
    int nx = gev->x;
    int ny = gev->y;
    if (pw > 0 && nx + menu->rect.width > px + pw)
        nx = px + pw - menu->rect.width;
    if (ph > 0 && ny + menu->rect.height > py + ph)
        ny = py + ph - menu->rect.height;
    if (nx < px)
        nx = px;
    if (ny < py)
        ny = py;
    menu->rect.x = nx;
    menu->rect.y = ny;
    AromaRect* r = aroma_node_get_rect(menu_node);
    if (r) {
        r->x = nx;
        r->y = ny;
    }
    menu->pressed_index = -1;
    menu->visible = true;
    __menu_register_overlay(menu_node);
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_menu_setup_events(AromaNode* menu_node,
                             void (*on_redraw)(void*), void* user_data)
{
    (void)on_redraw;
    (void)user_data;
    if (!menu_node || !menu_node->node_widget_ptr)
        return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->pressed_index = -1;
    /* NOTE: MOUSE_CLICK selection is subscribed in aroma_menu_create;
     * do not subscribe it again here (callbacks would fire twice). */
    aroma_event_subscribe(menu_node->node_id, EVENT_TYPE_TOUCH_DOWN,
                          __menu_touch_handler, NULL, 80);
    aroma_event_subscribe(menu_node->node_id, EVENT_TYPE_TOUCH_UP,
                          __menu_touch_handler, NULL, 80);
    if (!menu->longpress_obs && menu_node->parent_node) {
        menu->longpress_obs = aroma_gesture_observe(
            menu_node->parent_node, AROMA_GESTURE_LONG_PRESS,
            __menu_longpress_cb, menu_node);
    }
}

AromaNode* aroma_menu_create(AromaNode* parent, int x, int y)
{
    if (!parent) return NULL;


#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
#endif

    AromaMenu* menu = (AromaMenu*)aroma_widget_alloc(sizeof(AromaMenu));
    if (!menu) return NULL;

    memset(menu, 0, sizeof(AromaMenu));
    menu->rect.x = x;
    menu->rect.y = y;
    menu->rect.width = menu_dp(AROMA_MENU_WIDTH_DP);
    menu->rect.height = 0;
    menu->item_count = 0;
    menu->visible = false;
    menu->pressed_index = -1;
    menu->font = NULL;
    menu->item_height = menu_dp(AROMA_MENU_ITEM_HEIGHT_DP);
    menu->corner_radius = menu_dp_f((float)AROMA_MENU_CORNER_RADIUS_DP);
    AromaTheme theme = aroma_theme_get_global();
    menu->bg_color = theme.colors.surface;
    menu->border_color = theme.colors.border;
    menu->text_color = theme.colors.text_primary;
    menu->text_scale = 1.0f;
    menu->use_theme_colors = true;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, menu);
    if (!node) {
        aroma_widget_free(menu);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_menu_draw);

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __menu_handle_event, NULL, 80);

    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}


static void __menu_sync_rect(AromaNode* menu_node)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    AromaRect *r = aroma_node_get_rect(menu_node);
    if (r)
    {
        r->x = menu->rect.x;
        r->y = menu->rect.y;
        r->width = menu->rect.width;
        r->height = menu->rect.height;
    }
    aroma_node_invalidate(menu_node);
}

void aroma_menu_add_item(AromaNode* menu_node, const char* text, void (*callback)(void* user_data), void* user_data)
{
    if (!menu_node || !menu_node->node_widget_ptr || !text) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    if (menu->item_count >= AROMA_MENU_MAX_ITEMS) return;

    AromaMenuItem* item = &menu->items[menu->item_count++];
    memset(item, 0, sizeof(AromaMenuItem));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    item->enabled = true;
    item->callback = callback;
    item->user_data = user_data;
    menu->rect.height = (int)menu->item_count * menu->item_height;
    __menu_sync_rect(menu_node);
}

void aroma_menu_add_item_with_icon(AromaNode* menu_node, const char* text, const char* icon_code, void (*callback)(void* user_data), void* user_data)
{
    if (!menu_node || !menu_node->node_widget_ptr || !text) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    if (menu->item_count >= AROMA_MENU_MAX_ITEMS) return;

    AromaMenuItem* item = &menu->items[menu->item_count++];
    memset(item, 0, sizeof(AromaMenuItem));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    if (icon_code) {
        strncpy(item->icon, icon_code, 7);
        item->icon[7] = '\0';
    }
    item->enabled = true;
    item->callback = callback;
    item->user_data = user_data;
    menu->rect.height = (int)menu->item_count * menu->item_height;
    __menu_sync_rect(menu_node);
}

void aroma_menu_add_separator(AromaNode* menu_node)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    if (menu->item_count >= AROMA_MENU_MAX_ITEMS) return;
    AromaMenuItem* item = &menu->items[menu->item_count++];
    memset(item, 0, sizeof(AromaMenuItem));
    item->separator = true;
    menu->rect.height = (int)menu->item_count * menu->item_height;
    __menu_sync_rect(menu_node);
}

void aroma_menu_show(AromaNode* menu_node)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->visible = true;
    menu->pressed_index = -1;
    __menu_register_overlay(menu_node);
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_menu_hide(AromaNode* menu_node)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->visible = false;
    menu->pressed_index = -1;
    __menu_unregister_overlay(menu_node);
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_menu_set_font(AromaNode* menu_node, AromaFont* font)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->font = font;
}

void aroma_menu_set_icon_font(AromaNode* menu_node, AromaFont* font)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->icon_font = font;
}

void aroma_menu_draw(AromaNode* menu_node, size_t window_id)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    if (aroma_node_is_hidden(menu_node)) return;
    if (!menu->visible) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();

    if (menu->use_theme_colors) {
        AromaTheme theme = aroma_theme_get_global();
        menu->bg_color = theme.colors.surface;
        menu->border_color = theme.colors.border;
        menu->text_color = theme.colors.text_primary;
    }

    if (!gfx) return;
    gfx->fill_rectangle(window_id, menu->rect.x, menu->rect.y, menu->rect.width, menu->rect.height,
                        menu->bg_color, true, menu->corner_radius);
    gfx->draw_hollow_rectangle(window_id, menu->rect.x, menu->rect.y, menu->rect.width, menu->rect.height,
                               menu->border_color, menu_dp(1) < 1 ? 1 : menu_dp(1), true, menu->corner_radius);

    for (size_t i = 0; i < menu->item_count; ++i) {
        int y = menu->rect.y + (int)i * menu->item_height;
        if (menu->items[i].separator) {
            int sep_hairline = menu_dp(1);
            if (sep_hairline < 1) sep_hairline = 1;
            gfx->fill_rectangle(window_id, menu->rect.x + menu_dp(AROMA_MENU_SEPARATOR_INSET_DP), y + menu->item_height / 2, menu->rect.width - menu_dp(AROMA_MENU_SEPARATOR_INSET_DP * 2), sep_hairline,
                                menu->border_color, false, 0.0f);
            continue;
        }
        if (menu->font && gfx->render_text) {
            int line_h = aroma_font_get_line_height(menu->font);
            int text_x = menu->rect.x + menu_dp(AROMA_MENU_TEXT_X_DP);
            int text_y = y + (menu->item_height - line_h) / 2;

            if (menu->items[i].icon[0] != '\0' && menu->icon_font) {
                 int line_h = aroma_font_get_line_height(menu->icon_font);
                 gfx->render_text(window_id, menu->icon_font, menu->items[i].icon,
                     text_x, y + ((menu->item_height - line_h)/2),
                     menu->text_color, 1.0f);
                 text_x += menu_dp(AROMA_MENU_ICON_OFFSET_DP);
            }

            gfx->render_text(window_id, menu->font, menu->items[i].text, text_x, text_y, menu->text_color, menu->text_scale);
        }
    }
}

void aroma_menu_destroy(AromaNode* menu_node)
{
    if (!menu_node) return;
    if (menu_node->node_widget_ptr) {
        AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
        if (menu->longpress_obs) {
            aroma_gesture_unobserve(menu->longpress_obs);
            menu->longpress_obs = NULL;
        }
        __menu_unregister_overlay(menu_node);
        aroma_widget_free(menu_node->node_widget_ptr);
        menu_node->node_widget_ptr = NULL;
    }
    __destroy_node(menu_node);
}