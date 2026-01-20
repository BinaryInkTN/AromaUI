#include "widgets/aroma_tabs.h"
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

#define AROMA_TABS_CONTENT_MAX 8

struct AromaTabs {
    AromaRect rect;
    char labels[AROMA_TABS_MAX][AROMA_TAB_LABEL_MAX];
    int count;
    int selected_index;
    int hovered_index;
    AromaNode* content_nodes[AROMA_TABS_MAX][AROMA_TABS_CONTENT_MAX];
    int content_counts[AROMA_TABS_MAX];
    uint32_t bg_color;
    uint32_t selected_color;
    uint32_t text_color;
    uint32_t text_selected_color;
    AromaFont* font;
    void (*on_change)(AromaNode*, int, void*);
    void* user_data;
};

static void __tabs_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static int __tabs_index_from_x(AromaTabs* tabs, int x)
{
    if (!tabs || tabs->count <= 0) return -1;
    if (x < tabs->rect.x || x >= (tabs->rect.x + tabs->rect.width)) return -1;

    int base_width = tabs->rect.width / tabs->count;
    if (base_width <= 0) return -1;

    int start_x = tabs->rect.x;
    for (int i = 0; i < tabs->count; i++) {
        int w = (i == tabs->count - 1)
            ? (tabs->rect.x + tabs->rect.width - start_x)
            : base_width;
        if (x < start_x + w) {
            return i;
        }
        start_x += w;
    }

    return tabs->count - 1;
}

static void __tabs_set_hidden_recursive(AromaNode* node, bool hidden)
{
    if (!node) return;
    aroma_node_set_hidden(node, hidden);
    for (uint64_t i = 0; i < node->child_count; i++) {
        if (node->child_nodes[i]) {
            __tabs_set_hidden_recursive(node->child_nodes[i], hidden);
        }
    }
}

static void __tabs_update_content_visibility(AromaTabs* tabs)
{
    if (!tabs) return;
    for (int i = 0; i < tabs->count; i++) {
        bool hide = (i != tabs->selected_index);
        for (int j = 0; j < tabs->content_counts[i]; j++) {
            AromaNode* content = tabs->content_nodes[i][j];
            if (!content) continue;
            __tabs_set_hidden_recursive(content, hide);
        }
    }
}

static bool __tabs_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaTabs* tabs = (AromaTabs*)event->target_node->node_widget_ptr;
    if (!tabs) return false;

    bool in_bounds = (event->data.mouse.x >= tabs->rect.x && event->data.mouse.x <= tabs->rect.x + tabs->rect.width &&
                      event->data.mouse.y >= tabs->rect.y && event->data.mouse.y <= tabs->rect.y + tabs->rect.height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_MOVE: {
            int new_hover = in_bounds ? __tabs_index_from_x(tabs, event->data.mouse.x) : -1;
            if (new_hover != tabs->hovered_index) {
                tabs->hovered_index = new_hover;
                aroma_node_invalidate(event->target_node);
                __tabs_request_redraw(user_data);
            }
            return in_bounds;
        }
        case EVENT_TYPE_MOUSE_EXIT:
            if (tabs->hovered_index != -1) {
                tabs->hovered_index = -1;
                aroma_node_invalidate(event->target_node);
                __tabs_request_redraw(user_data);
            }
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                int index = __tabs_index_from_x(tabs, event->data.mouse.x);
                if (index >= 0 && index < tabs->count && index != tabs->selected_index) {
                    tabs->selected_index = index;
                    __tabs_update_content_visibility(tabs);
                    if (tabs->on_change) {
                        tabs->on_change(event->target_node, index, tabs->user_data);
                    }
                    aroma_node_invalidate(event->target_node);
                    __tabs_request_redraw(user_data);
                }
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

AromaNode* aroma_tabs_create(AromaNode* parent, int x, int y, int width, int height,
                             const char** labels, int count)
{
    if (!parent || !labels || count <= 0) return NULL;

    AromaTabs* tabs = (AromaTabs*)aroma_widget_alloc(sizeof(AromaTabs));
    if (!tabs) return NULL;

    memset(tabs, 0, sizeof(AromaTabs));
    tabs->rect.x = x;
    tabs->rect.y = y;
    tabs->rect.width = width;
    tabs->rect.height = height;
    tabs->count = (count > AROMA_TABS_MAX) ? AROMA_TABS_MAX : count;
    tabs->selected_index = 0;
    tabs->hovered_index = -1;

    AromaTheme theme = aroma_theme_get_global();
    tabs->bg_color = theme.colors.surface;
    tabs->selected_color = theme.colors.primary;
    tabs->text_color = theme.colors.text_primary;
    tabs->text_selected_color = theme.colors.surface;

    for (int i = 0; i < tabs->count; i++) {
        if (labels[i]) {
            strncpy(tabs->labels[i], labels[i], AROMA_TAB_LABEL_MAX - 1);
        } else {
            tabs->labels[i][0] = '\0';
        }
        tabs->content_counts[i] = 0;
        for (int j = 0; j < AROMA_TABS_CONTENT_MAX; j++) {
            tabs->content_nodes[i][j] = NULL;
        }
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, tabs);
    if (!node) {
        aroma_widget_free(tabs);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_tabs_draw);

    if (!tabs->font) {
        AromaNode* root_node = parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    if (g_windows[i].default_font) {
                        tabs->font = g_windows[i].default_font;
                    }
                    break;
                }
            }
        }
    }

    aroma_node_invalidate(node);

    return node;
}

void aroma_tabs_set_selected(AromaNode* tabs_node, int index)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    if (index < 0 || index >= tabs->count) return;
    if (tabs->selected_index != index) {
        tabs->selected_index = index;
    }
    __tabs_update_content_visibility(tabs);
    aroma_node_invalidate(tabs_node);
}

int aroma_tabs_get_selected(AromaNode* tabs_node)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return -1;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    return tabs->selected_index;
}

void aroma_tabs_set_on_change(AromaNode* tabs_node,
                              void (*callback)(AromaNode*, int, void*),
                              void* user_data)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    tabs->on_change = callback;
    tabs->user_data = user_data;
}

void aroma_tabs_set_font(AromaNode* tabs_node, AromaFont* font)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    tabs->font = font;
    aroma_node_invalidate(tabs_node);
}

void aroma_tabs_set_content(AromaNode* tabs_node, int index, AromaNode** content_nodes, int content_count)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    if (index < 0 || index >= tabs->count) return;
    int max_count = (content_count > AROMA_TABS_CONTENT_MAX) ? AROMA_TABS_CONTENT_MAX : content_count;
    tabs->content_counts[index] = 0;
    for (int i = 0; i < max_count; i++) {
        tabs->content_nodes[index][i] = content_nodes ? content_nodes[i] : NULL;
        if (tabs->content_nodes[index][i]) {
            tabs->content_counts[index]++;
        }
    }
    __tabs_update_content_visibility(tabs);
}

bool aroma_tabs_setup_events(AromaNode* tabs_node, void (*on_redraw_callback)(void*), void* user_data)
{
    (void)user_data;
    if (!tabs_node) return false;
    aroma_event_subscribe(tabs_node->node_id, EVENT_TYPE_MOUSE_MOVE, __tabs_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(tabs_node->node_id, EVENT_TYPE_MOUSE_EXIT, __tabs_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(tabs_node->node_id, EVENT_TYPE_MOUSE_CLICK, __tabs_handle_event, (void*)on_redraw_callback, 90);
    return true;
}

void aroma_tabs_draw(AromaNode* tabs_node, size_t window_id)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(tabs_node)) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;

    if (!tabs->font) {
        for (int i = 0; i < g_window_count; ++i) {
            if (g_windows[i].is_active && g_windows[i].window_id == window_id && g_windows[i].default_font) {
                tabs->font = g_windows[i].default_font;
                break;
            }
        }
    }

    __tabs_update_content_visibility(tabs);

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    AromaTheme theme = aroma_theme_get_global();
    tabs->bg_color = theme.colors.surface;
    tabs->selected_color = theme.colors.primary;
    tabs->text_color = theme.colors.text_primary;
    tabs->text_selected_color = theme.colors.surface;

    float radius = 0.0f;
    gfx->fill_rectangle(window_id, tabs->rect.x, tabs->rect.y, tabs->rect.width,
                        tabs->rect.height, tabs->bg_color, false, radius);

    if (tabs->count <= 0) return;

    int tab_width = tabs->rect.width / tabs->count;
    int ascender = tabs->font ? aroma_font_get_ascender(tabs->font) : 10;

    for (int i = 0; i < tabs->count; i++) {
        int x = tabs->rect.x + i * tab_width;
        int w = (i == tabs->count - 1) ? (tabs->rect.x + tabs->rect.width - x) : tab_width;
        bool selected = (i == tabs->selected_index);
        bool hovered = (i == tabs->hovered_index);

        uint32_t fill = selected ? tabs->selected_color : tabs->bg_color;
        if (hovered && !selected) {
            fill = aroma_color_blend(fill, tabs->selected_color, 0.12f);
        }

        gfx->fill_rectangle(window_id, x, tabs->rect.y, w, tabs->rect.height, fill, false, radius);
        gfx->draw_hollow_rectangle(window_id, x, tabs->rect.y, w, tabs->rect.height,
                       aroma_color_adjust(tabs->bg_color, -0.2f), 1, false, radius);

        if (tabs->font && gfx->render_text) {
            uint32_t text_color = selected ? tabs->text_selected_color : tabs->text_color;
            int text_y = tabs->rect.y + (tabs->rect.height - ascender) / 2 + ascender;
            gfx->render_text(window_id, tabs->font, tabs->labels[i], x + 12, text_y, text_color);
        }
    }
}

void aroma_tabs_destroy(AromaNode* tabs_node)
{
    if (!tabs_node) return;
    if (tabs_node->node_widget_ptr) {
        aroma_widget_free(tabs_node->node_widget_ptr);
        tabs_node->node_widget_ptr = NULL;
    }
}
