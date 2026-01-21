/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "widgets/aroma_iconbutton.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_ICON_TEXT_MAX 16

typedef struct AromaIconButton {
    AromaRect rect;
    char icon_text[AROMA_ICON_TEXT_MAX];
    AromaIconButtonVariant variant;
    uint32_t bg_color;
    uint32_t icon_color;
    bool is_hovered;
    bool is_pressed;
    void (*callback)(void* user_data);
    void* user_data;
    AromaFont* font;
} AromaIconButton;

static bool __iconbutton_handle_event(AromaEvent* event, void* user_data)
{
    (void)user_data;
    if (!event || !event->target_node) return false;
    AromaIconButton* btn = (AromaIconButton*)event->target_node->node_widget_ptr;
    if (!btn) return false;

    AromaRect* r = &btn->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);

    switch (event->event_type) {
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
            if (in_bounds) {
                btn->is_pressed = true;
                aroma_node_invalidate(event->target_node);
                aroma_ui_request_redraw(NULL);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (btn->is_pressed) {
                btn->is_pressed = false;
                aroma_node_invalidate(event->target_node);
                if (in_bounds && btn->callback) btn->callback(btn->user_data);
                aroma_ui_request_redraw(NULL);
                return in_bounds;
            }
            break;
        default:
            break;
    }

    return false;
}

AromaNode* aroma_iconbutton_create(AromaNode* parent, const char* icon_text, int x, int y, int size, AromaIconButtonVariant variant)
{
    if (!parent || size <= 0) return NULL;

    AromaIconButton* btn = (AromaIconButton*)aroma_widget_alloc(sizeof(AromaIconButton));
    if (!btn) return NULL;

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

    if (icon_text) {
        strncpy(btn->icon_text, icon_text, AROMA_ICON_TEXT_MAX - 1);
    } else {
        btn->icon_text[0] = '\0';
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, btn);
    if (!node) {
        aroma_widget_free(btn);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_iconbutton_draw);

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __iconbutton_handle_event, NULL, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __iconbutton_handle_event, NULL, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __iconbutton_handle_event, NULL, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __iconbutton_handle_event, NULL, 70);

    return node;
}

void aroma_iconbutton_set_callback(AromaNode* button_node, void (*callback)(void* user_data), void* user_data)
{
    if (!button_node || !button_node->node_widget_ptr) return;
    AromaIconButton* btn = (AromaIconButton*)button_node->node_widget_ptr;
    btn->callback = callback;
    btn->user_data = user_data;
}

void aroma_iconbutton_set_colors(AromaNode* button_node, uint32_t bg_color, uint32_t icon_color)
{
    if (!button_node || !button_node->node_widget_ptr) return;
    AromaIconButton* btn = (AromaIconButton*)button_node->node_widget_ptr;
    btn->bg_color = bg_color;
    btn->icon_color = icon_color;
    aroma_node_invalidate(button_node);
}

void aroma_iconbutton_set_font(AromaNode* button_node, AromaFont* font)
{
    if (!button_node || !button_node->node_widget_ptr) return;
    AromaIconButton* btn = (AromaIconButton*)button_node->node_widget_ptr;
    btn->font = font;
}

void aroma_iconbutton_draw(AromaNode* button_node, size_t window_id)
{
    if (!button_node || !button_node->node_widget_ptr) return;
    AromaIconButton* btn = (AromaIconButton*)button_node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    uint32_t bg = btn->bg_color;
    if (btn->is_pressed) {
    if (aroma_node_is_hidden(button_node)) return;
        bg = aroma_color_adjust(bg, -0.1f);
    } else if (btn->is_hovered) {
        bg = aroma_color_adjust(bg, 0.08f);
    }

    gfx->fill_rectangle(window_id, btn->rect.x, btn->rect.y, btn->rect.width, btn->rect.height,
                        bg, true, btn->rect.height / 2.0f);

    if (btn->variant == ICON_BUTTON_OUTLINED) {
        AromaTheme theme = aroma_theme_get_global();
        gfx->draw_hollow_rectangle(window_id, btn->rect.x, btn->rect.y, btn->rect.width, btn->rect.height,
                                   theme.colors.border, 1, true, btn->rect.height / 2.0f);
    }

    if (btn->font && btn->icon_text[0] && gfx->render_text) {
        int asc = aroma_font_get_ascender(btn->font);
        int line = aroma_font_get_line_height(btn->font);
        int baseline = btn->rect.y + (btn->rect.height - line) / 2 + asc;
        int text_x = btn->rect.x + btn->rect.width / 2 - 6;
        gfx->render_text(window_id, btn->font, btn->icon_text, text_x, baseline, btn->icon_color);
    }
}

void aroma_iconbutton_destroy(AromaNode* button_node)
{
    if (!button_node) return;
    if (button_node->node_widget_ptr) {
        aroma_widget_free(button_node->node_widget_ptr);
        button_node->node_widget_ptr = NULL;
    }
    __destroy_node(button_node);
}