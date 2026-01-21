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

#include "widgets/aroma_sidebar.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_SIDEBAR_CONTENT_MAX 16

struct AromaSidebar {
    AromaRect rect;
    char labels[AROMA_SIDEBAR_MAX_ITEMS][AROMA_SIDEBAR_LABEL_MAX];
    int count;
    int selected_index;
    int hovered_index;
    int item_height;
    AromaNode* content_nodes[AROMA_SIDEBAR_MAX_ITEMS][AROMA_SIDEBAR_CONTENT_MAX];
    int content_counts[AROMA_SIDEBAR_MAX_ITEMS];
    AromaFont* font;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t selected_color;
    void (*on_select)(AromaNode*, int, void*);
    void* user_data;
};

static void __sidebar_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static int __sidebar_index_from_y(AromaSidebar* sidebar, int y)
{
    if (!sidebar || sidebar->count <= 0) return -1;
    int local_y = y - sidebar->rect.y;
    if (local_y < 0) return -1;
    int index = local_y / sidebar->item_height;
    if (index < 0) return -1;
    if (index >= sidebar->count) return -1;
    return index;
}

static void __sidebar_set_hidden_recursive(AromaNode* node, bool hidden)
{
    if (!node) return;
    aroma_node_set_hidden(node, hidden);
    for (uint64_t i = 0; i < node->child_count; i++) {
        if (node->child_nodes[i]) {
            __sidebar_set_hidden_recursive(node->child_nodes[i], hidden);
        }
    }
}

static void __sidebar_update_content_visibility(AromaSidebar* sidebar)
{
    if (!sidebar) return;
    for (int i = 0; i < sidebar->count; i++) {
        bool hide = (i != sidebar->selected_index);
        for (int j = 0; j < sidebar->content_counts[i]; j++) {
            AromaNode* content = sidebar->content_nodes[i][j];
            if (!content) continue;
            __sidebar_set_hidden_recursive(content, hide);
        }
    }
}

static bool __sidebar_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaSidebar* sidebar = (AromaSidebar*)event->target_node->node_widget_ptr;
    if (!sidebar) return false;

    bool in_bounds = (event->data.mouse.x >= sidebar->rect.x && event->data.mouse.x <= sidebar->rect.x + sidebar->rect.width &&
                      event->data.mouse.y >= sidebar->rect.y && event->data.mouse.y <= sidebar->rect.y + sidebar->rect.height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_MOVE: {
            int new_hover = in_bounds ? __sidebar_index_from_y(sidebar, event->data.mouse.y) : -1;
            if (new_hover != sidebar->hovered_index) {
                sidebar->hovered_index = new_hover;
                aroma_node_invalidate(event->target_node);
                __sidebar_request_redraw(user_data);
            }
            return in_bounds;
        }
        case EVENT_TYPE_MOUSE_EXIT:
            if (sidebar->hovered_index != -1) {
                sidebar->hovered_index = -1;
                aroma_node_invalidate(event->target_node);
                __sidebar_request_redraw(user_data);
            }
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                int index = __sidebar_index_from_y(sidebar, event->data.mouse.y);
                if (index >= 0 && index < sidebar->count && index != sidebar->selected_index) {
                    sidebar->selected_index = index;
                    __sidebar_update_content_visibility(sidebar);
                    if (sidebar->on_select) {
                        sidebar->on_select(event->target_node, index, sidebar->user_data);
                    }
                    aroma_node_invalidate(event->target_node);
                    __sidebar_request_redraw(user_data);
                }
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

AromaNode* aroma_sidebar_create(AromaNode* parent, int x, int y, int width, int height,
                                const char** labels, int count)
{
    if (!parent || !labels || count <= 0) return NULL;

    AromaSidebar* sidebar = (AromaSidebar*)aroma_widget_alloc(sizeof(AromaSidebar));
    if (!sidebar) return NULL;

    memset(sidebar, 0, sizeof(AromaSidebar));
    sidebar->rect.x = x;
    sidebar->rect.y = y;
    sidebar->rect.width = width;
    sidebar->rect.height = height;
    sidebar->count = (count > AROMA_SIDEBAR_MAX_ITEMS) ? AROMA_SIDEBAR_MAX_ITEMS : count;
    sidebar->selected_index = 0;
    sidebar->hovered_index = -1;
    sidebar->item_height = 44;

    AromaTheme theme = aroma_theme_get_global();
    sidebar->bg_color = theme.colors.surface;
    sidebar->text_color = theme.colors.text_primary;
    sidebar->selected_color = theme.colors.primary;

    for (int i = 0; i < sidebar->count; i++) {
        if (labels[i]) {
            strncpy(sidebar->labels[i], labels[i], AROMA_SIDEBAR_LABEL_MAX - 1);
        } else {
            sidebar->labels[i][0] = '\0';
        }
        sidebar->content_counts[i] = 0;
        for (int j = 0; j < AROMA_SIDEBAR_CONTENT_MAX; j++) {
            sidebar->content_nodes[i][j] = NULL;
        }
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, sidebar);
    if (!node) {
        aroma_widget_free(sidebar);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_sidebar_draw);

    if (!sidebar->font) {
        AromaNode* root_node = parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    if (g_windows[i].default_font) {
                        sidebar->font = g_windows[i].default_font;
                    }
                    break;
                }
            }
        }
    }

    aroma_node_invalidate(node);

    return node;
}

void aroma_sidebar_set_selected(AromaNode* sidebar_node, int index)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr) return;
    AromaSidebar* sidebar = (AromaSidebar*)sidebar_node->node_widget_ptr;
    if (index < 0 || index >= sidebar->count) return;
    if (sidebar->selected_index != index) {
        sidebar->selected_index = index;
    }
    __sidebar_update_content_visibility(sidebar);
    aroma_node_invalidate(sidebar_node);
}

int aroma_sidebar_get_selected(AromaNode* sidebar_node)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr) return -1;
    AromaSidebar* sidebar = (AromaSidebar*)sidebar_node->node_widget_ptr;
    return sidebar->selected_index;
}

void aroma_sidebar_set_on_select(AromaNode* sidebar_node,
                                 void (*callback)(AromaNode*, int, void*),
                                 void* user_data)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr) return;
    AromaSidebar* sidebar = (AromaSidebar*)sidebar_node->node_widget_ptr;
    sidebar->on_select = callback;
    sidebar->user_data = user_data;
}

void aroma_sidebar_set_font(AromaNode* sidebar_node, AromaFont* font)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr) return;
    AromaSidebar* sidebar = (AromaSidebar*)sidebar_node->node_widget_ptr;
    sidebar->font = font;
    aroma_node_invalidate(sidebar_node);
}

void aroma_sidebar_set_content(AromaNode* sidebar_node, int index, AromaNode** content_nodes, int content_count)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr) return;
    AromaSidebar* sidebar = (AromaSidebar*)sidebar_node->node_widget_ptr;
    if (index < 0 || index >= sidebar->count) return;
    int max_count = (content_count > AROMA_SIDEBAR_CONTENT_MAX) ? AROMA_SIDEBAR_CONTENT_MAX : content_count;
    sidebar->content_counts[index] = 0;
    for (int i = 0; i < max_count; i++) {
        sidebar->content_nodes[index][i] = content_nodes ? content_nodes[i] : NULL;
        if (sidebar->content_nodes[index][i]) {
            sidebar->content_counts[index]++;
        }
    }
    __sidebar_update_content_visibility(sidebar);
}

bool aroma_sidebar_setup_events(AromaNode* sidebar_node, void (*on_redraw_callback)(void*), void* user_data)
{
    (void)user_data;
    if (!sidebar_node) return false;
    aroma_event_subscribe(sidebar_node->node_id, EVENT_TYPE_MOUSE_MOVE, __sidebar_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(sidebar_node->node_id, EVENT_TYPE_MOUSE_EXIT, __sidebar_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(sidebar_node->node_id, EVENT_TYPE_MOUSE_CLICK, __sidebar_handle_event, (void*)on_redraw_callback, 90);
    return true;
}

void aroma_sidebar_draw(AromaNode* sidebar_node, size_t window_id)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(sidebar_node)) return;
    AromaSidebar* sidebar = (AromaSidebar*)sidebar_node->node_widget_ptr;

    if (!sidebar->font) {
        for (int i = 0; i < g_window_count; ++i) {
            if (g_windows[i].is_active && g_windows[i].window_id == window_id && g_windows[i].default_font) {
                sidebar->font = g_windows[i].default_font;
                break;
            }
        }
    }

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    AromaTheme theme = aroma_theme_get_global();
    sidebar->bg_color = theme.colors.surface;
    sidebar->text_color = theme.colors.text_primary;
    sidebar->selected_color = theme.colors.primary;

    gfx->fill_rectangle(window_id, sidebar->rect.x, sidebar->rect.y,
                        sidebar->rect.width, sidebar->rect.height,
                        sidebar->bg_color, true, 12.0f);

    int ascender = sidebar->font ? aroma_font_get_ascender(sidebar->font) : 10;

    for (int i = 0; i < sidebar->count; i++) {
        int item_y = sidebar->rect.y + i * sidebar->item_height;
        if (item_y + sidebar->item_height > sidebar->rect.y + sidebar->rect.height) break;

        bool selected = (i == sidebar->selected_index);
        bool hovered = (i == sidebar->hovered_index);
        uint32_t row_color = sidebar->bg_color;

        if (selected) {
            row_color = aroma_color_blend(sidebar->bg_color, sidebar->selected_color, 0.16f);
        } else if (hovered) {
            row_color = aroma_color_blend(sidebar->bg_color, sidebar->selected_color, 0.08f);
        }

        gfx->fill_rectangle(window_id, sidebar->rect.x + 6, item_y + 4,
                            sidebar->rect.width - 12, sidebar->item_height - 8,
                            row_color, true, 10.0f);

        if (sidebar->font && gfx->render_text) {
            uint32_t text_color = selected ? sidebar->selected_color : sidebar->text_color;
            int text_y = item_y + (sidebar->item_height - ascender) / 2 + ascender;
            gfx->render_text(window_id, sidebar->font, sidebar->labels[i], sidebar->rect.x + 14, text_y, text_color);
        }
    }
}

void aroma_sidebar_destroy(AromaNode* sidebar_node)
{
    if (!sidebar_node) return;
    if (sidebar_node->node_widget_ptr) {
        aroma_widget_free(sidebar_node->node_widget_ptr);
        sidebar_node->node_widget_ptr = NULL;
    }
}
