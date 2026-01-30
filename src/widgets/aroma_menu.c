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

#include "widgets/aroma_menu.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_MENU_MAX_ITEMS 32

typedef struct AromaMenu {
    AromaRect rect;
    AromaMenuItem items[AROMA_MENU_MAX_ITEMS];
    size_t item_count;
    bool visible;
    AromaFont* font;
    int item_height;
    float corner_radius;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t text_color;
    float text_scale;
} AromaMenu;

static bool __menu_handle_event(AromaEvent* event, void* user_data)
{
    (void)user_data;
    if (!event || !event->target_node) return false;
    AromaMenu* menu = (AromaMenu*)event->target_node->node_widget_ptr;
    if (!menu || !menu->visible) return false;

    if (event->event_type != EVENT_TYPE_MOUSE_CLICK) return false;

    int rel_y = event->data.mouse.y - menu->rect.y;
    int item_height = menu->item_height;
    int index = rel_y / item_height;
    if (index >= 0 && index < (int)menu->item_count) {
        AromaMenuItem* item = &menu->items[index];
        if (item->enabled && item->callback) item->callback(item->user_data);
        menu->visible = false;
        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);
        return true;
    }
    return false;
}

AromaNode* aroma_menu_create(AromaNode* parent, int x, int y)
{
    if (!parent) return NULL;
    AromaMenu* menu = (AromaMenu*)aroma_widget_alloc(sizeof(AromaMenu));
    if (!menu) return NULL;

    memset(menu, 0, sizeof(AromaMenu));
    menu->rect.x = x;
    menu->rect.y = y;
    menu->rect.width = 200;
    menu->rect.height = 0;
    menu->item_count = 0;
    menu->visible = false;
    menu->font = NULL;
    menu->item_height = 28;
    menu->corner_radius = 8.0f;
    AromaTheme theme = aroma_theme_get_global();
    menu->bg_color = theme.colors.surface;
    menu->border_color = theme.colors.border;
    menu->text_color = theme.colors.text_primary;
    menu->text_scale = 1.0f;

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

void aroma_menu_add_item(AromaNode* menu_node, const char* text, void (*callback)(void* user_data), void* user_data)
{
    if (!menu_node || !menu_node->node_widget_ptr || !text) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    if (menu->item_count >= AROMA_MENU_MAX_ITEMS) return;

    AromaMenuItem* item = &menu->items[menu->item_count++];
    memset(item, 0, sizeof(AromaMenuItem));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->enabled = true;
    item->callback = callback;
    item->user_data = user_data;
    menu->rect.height = (int)menu->item_count * menu->item_height;
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
}

void aroma_menu_show(AromaNode* menu_node)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->visible = true;
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_menu_hide(AromaNode* menu_node)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->visible = false;
    aroma_node_invalidate(menu_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_menu_set_font(AromaNode* menu_node, AromaFont* font)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    menu->font = font;
}

void aroma_menu_draw(AromaNode* menu_node, size_t window_id)
{
    if (!menu_node || !menu_node->node_widget_ptr) return;
    AromaMenu* menu = (AromaMenu*)menu_node->node_widget_ptr;
    if (aroma_node_is_hidden(menu_node)) return;
    if (!menu->visible) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    gfx->fill_rectangle(window_id, menu->rect.x, menu->rect.y, menu->rect.width, menu->rect.height,
                        menu->bg_color, true, menu->corner_radius);
    gfx->draw_hollow_rectangle(window_id, menu->rect.x, menu->rect.y, menu->rect.width, menu->rect.height,
                               menu->border_color, 1, true, menu->corner_radius);

    for (size_t i = 0; i < menu->item_count; ++i) {
        int y = menu->rect.y + (int)i * menu->item_height;
        if (menu->items[i].separator) {
            gfx->fill_rectangle(window_id, menu->rect.x + 8, y + menu->item_height / 2, menu->rect.width - 16, 1,
                                menu->border_color, false, 0.0f);
            continue;
        }
        if (menu->font && gfx->render_text) {
            gfx->render_text(window_id, menu->font, menu->items[i].text, menu->rect.x + 12, y + (menu->item_height/2), menu->text_color, menu->text_scale);
        }
    }
}

void aroma_menu_destroy(AromaNode* menu_node)
{
    if (!menu_node) return;
    if (menu_node->node_widget_ptr) {
        aroma_widget_free(menu_node->node_widget_ptr);
        menu_node->node_widget_ptr = NULL;
    }
    __destroy_node(menu_node);
}