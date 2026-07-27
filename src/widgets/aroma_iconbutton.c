#include "widgets/aroma_iconbutton.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stddef.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_ICON_TEXT_MAX 16

typedef struct AromaIconButton
{
    AromaRect rect;
    AromaFont *font;

    void (*callback)(void *user_data);
    void *user_data;

    uint32_t bg_color;
    uint32_t icon_color;
    uint32_t border_color;

    float corner_radius;
    float text_scale;

    int text_x;
    int text_y;

    AromaIconButtonVariant variant;

    bool is_hovered;
    bool is_pressed;
    bool use_theme_colors;

    char icon_text[AROMA_ICON_TEXT_MAX];

} AromaIconButton;

static bool __iconbutton_handle_event(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || !event->target_node)
        return false;
    AromaIconButton *btn = (AromaIconButton *)event->target_node->node_widget_ptr;
    if (!btn)
        return false;


    int adjusted_x = event->data.mouse.x;
    int adjusted_y = event->data.mouse.y;

    AromaNode *cur = event->target_node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int scroll_x, scroll_y;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            adjusted_x += scroll_x;
            adjusted_y += scroll_y;
        }
        cur = cur->parent_node;
    }
    

    AromaRect r = btn->rect;
    
    // Get coordinates based on event type
    int x, y;
    if (event->event_type == EVENT_TYPE_TOUCH_DOWN || 
        event->event_type == EVENT_TYPE_TOUCH_UP || 
        event->event_type == EVENT_TYPE_TOUCH_MOVE) {
        x = event->data.touch.x;
        y = event->data.touch.y;
    } else {
        x = event->data.mouse.x;
        y = event->data.mouse.y;
    }
    
    bool in_bounds = (adjusted_x >= r.x && adjusted_x <= r.x + r.width &&
                      adjusted_y >= r.y && adjusted_y <= r.y + r.height);
                      
    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_ENTER:
        btn->is_hovered = true;
        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);
        return true;
        
    case EVENT_TYPE_MOUSE_EXIT:
        btn->is_hovered = false;
        btn->is_pressed = false;
        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);
        return false;
        
    case EVENT_TYPE_MOUSE_CLICK:
    case EVENT_TYPE_TOUCH_DOWN:
        if (in_bounds)
        {
            btn->is_pressed = true;
            aroma_node_invalidate(event->target_node);
            aroma_ui_request_redraw(NULL);
            return true;
        }
        break;
        
    case EVENT_TYPE_MOUSE_RELEASE:
    case EVENT_TYPE_TOUCH_UP:
        if (btn->is_pressed)
        {
            btn->is_pressed = false;
            aroma_node_invalidate(event->target_node);
            if (in_bounds && btn->callback)
                btn->callback(btn->user_data);
            aroma_ui_request_redraw(NULL);
            return in_bounds;
        }
        break;
        
    default:
        break;
    }

    return false;
}

static void __iconbutton_update_layout(AromaIconButton *btn)
{
    btn->corner_radius = (float)btn->rect.height / 2.0f;
    int font_px = btn->font ? aroma_font_get_px_size(btn->font) : 0;
    btn->text_x = btn->rect.x + btn->rect.width / 2 - font_px / 2;
    int asc = 0;
    int line = btn->font ? font_px : btn->rect.height;
    btn->text_y = btn->rect.y + (btn->rect.height - line) / 2 + asc;
}

AromaNode *aroma_iconbutton_create(AromaNode *parent, const char *icon_text, int x, int y, int size, AromaIconButtonVariant variant)
{
    if (!parent || size <= 0)
        return NULL;

#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    size = aroma_android_dp_to_px(size);
#endif

    AromaIconButton *btn = (AromaIconButton *)aroma_widget_alloc(sizeof(AromaIconButton));
    if (!btn)
        return NULL;

    AromaTheme theme = aroma_theme_get_global();
    btn->rect.x = x;
    btn->rect.y = y;
    btn->rect.width = size;
    btn->rect.height = size;
    btn->variant = variant;
    btn->bg_color = (variant == ICON_BUTTON_FILLED || variant == ICON_BUTTON_TONAL)
                        ? theme.colors.primary
                        : theme.colors.surface;
    btn->icon_color = (variant == ICON_BUTTON_FILLED || variant == ICON_BUTTON_TONAL)
                          ? theme.colors.surface
                          : theme.colors.text_primary;
    btn->is_hovered = false;
    btn->is_pressed = false;
    btn->callback = NULL;
    btn->user_data = NULL;
    btn->font = NULL;
    btn->text_scale = 1.0f;
    btn->border_color = theme.colors.border;
    btn->text_x = 0;
    btn->text_y = 0;
    btn->use_theme_colors = true;

    if (icon_text)
    {
        strncpy(btn->icon_text, icon_text, AROMA_ICON_TEXT_MAX - 1);
        btn->icon_text[AROMA_ICON_TEXT_MAX - 1] = '\0';
    }
    else
    {
        btn->icon_text[0] = '\0';
    }

    __iconbutton_update_layout(btn);

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, btn);
    if (!node)
    {
        aroma_widget_free(btn);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_iconbutton_draw);

    // Subscribe to mouse events
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __iconbutton_handle_event, NULL, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __iconbutton_handle_event, NULL, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __iconbutton_handle_event, NULL, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __iconbutton_handle_event, NULL, 70);
    
    // Subscribe to touch events for Android
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, __iconbutton_handle_event, NULL, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP, __iconbutton_handle_event, NULL, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_MOVE, __iconbutton_handle_event, NULL, 60);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_iconbutton_set_callback(AromaNode *button_node, void (*callback)(void *user_data), void *user_data)
{
    if (!button_node || !button_node->node_widget_ptr)
        return;
    AromaIconButton *btn = (AromaIconButton *)button_node->node_widget_ptr;
    btn->callback = callback;
    btn->user_data = user_data;
}

void aroma_iconbutton_set_colors(AromaNode *button_node, uint32_t bg_color, uint32_t icon_color)
{
    if (!button_node || !button_node->node_widget_ptr)
        return;
    AromaIconButton *btn = (AromaIconButton *)button_node->node_widget_ptr;
    btn->bg_color = bg_color;
    btn->icon_color = icon_color;
    btn->use_theme_colors = false;
    aroma_node_invalidate(button_node);
}

void aroma_iconbutton_set_icon(AromaNode *button_node, const char *icon_text)
{
    if (!button_node || !button_node->node_widget_ptr)
        return;
    AromaIconButton *btn = (AromaIconButton *)button_node->node_widget_ptr;
    if (icon_text)
    {
        strncpy(btn->icon_text, icon_text, AROMA_ICON_TEXT_MAX - 1);
        btn->icon_text[AROMA_ICON_TEXT_MAX - 1] = '\0';
    }
    else
    {
        btn->icon_text[0] = '\0';
    }
    aroma_node_invalidate(button_node);
}

void aroma_iconbutton_set_font(AromaNode *button_node, AromaFont *font)
{
    if (!button_node || !button_node->node_widget_ptr)
        return;
    AromaIconButton *btn = (AromaIconButton *)button_node->node_widget_ptr;
    btn->font = font;
    __iconbutton_update_layout(btn);
}

void aroma_iconbutton_draw(AromaNode *button_node, size_t window_id)
{
    if (!button_node || !button_node->node_widget_ptr)
        return;
    AromaIconButton *btn = (AromaIconButton *)button_node->node_widget_ptr;

    if (aroma_node_is_hidden(button_node))
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    if (btn->use_theme_colors)
    {
        AromaTheme theme = aroma_theme_get_global();
        btn->bg_color = (btn->variant == ICON_BUTTON_FILLED || btn->variant == ICON_BUTTON_TONAL)
                            ? theme.colors.primary
                            : theme.colors.surface;
        btn->icon_color = (btn->variant == ICON_BUTTON_FILLED || btn->variant == ICON_BUTTON_TONAL)
                              ? theme.colors.surface
                              : theme.colors.text_primary;
        btn->border_color = theme.colors.border;
    }

    uint32_t bg = btn->bg_color;
    if (btn->is_pressed)
        bg = aroma_color_adjust(bg, -0.1f);
    else if (btn->is_hovered)
        bg = aroma_color_adjust(bg, 0.08f);

    gfx->fill_rectangle(window_id, btn->rect.x, btn->rect.y, btn->rect.width, btn->rect.height,
                        bg, true, btn->corner_radius);

    if (btn->variant == ICON_BUTTON_OUTLINED)
    {
        gfx->draw_hollow_rectangle(window_id, btn->rect.x, btn->rect.y, btn->rect.width, btn->rect.height,
                                   btn->border_color, 1, true, btn->corner_radius);
    }

    if (btn->font && btn->icon_text[0] && gfx->render_text)
    {
        int font_px = aroma_font_get_px_size(btn->font);
        int tx = btn->rect.x + btn->rect.width / 2 - font_px / 2;
        int ty = btn->rect.y + (btn->rect.height - font_px) / 2;
        gfx->render_text(window_id, btn->font, btn->icon_text, tx, ty, btn->icon_color, btn->text_scale);
    }
}

void aroma_iconbutton_destroy(AromaNode *button_node)
{
    if (!button_node)
        return;
    if (button_node->node_widget_ptr)
    {
        aroma_widget_free(button_node->node_widget_ptr);
        button_node->node_widget_ptr = NULL;
    }
    __destroy_node(button_node);
}