#include "widgets/aroma_segmented.h"
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
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

struct AromaSegmented {
    AromaRect rect;
    char labels[AROMA_SEGMENTED_MAX][AROMA_SEGMENT_LABEL_MAX];
    int count;
    int selected_index;
    uint32_t track_color;
    uint32_t thumb_color;
    uint32_t text_color;
    uint32_t text_selected_color;
    uint32_t border_color;
    bool use_theme_colors;
    AromaFont* font;
    float corner_radius;
    float text_scale;
    void (*on_change)(AromaNode*, int, void*);
    void* user_data;
};

static void __segmented_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static int __segmented_index_from_x(AromaSegmented* seg, int x)
{
    if (!seg || seg->count <= 0) return -1;
    if (x < seg->rect.x || x >= (seg->rect.x + seg->rect.width)) return -1;

    int base_width = seg->rect.width / seg->count;
    if (base_width <= 0) base_width = 1;

    int start_x = seg->rect.x;
    for (int i = 0; i < seg->count; i++) {
        int w = (i == seg->count - 1)
            ? (seg->rect.x + seg->rect.width - start_x)
            : base_width;
        if (w <= 0) w = 1;
        if (x < start_x + w) {
            return i;
        }
        start_x += w;
    }

    return -1;
}

static bool __segmented_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaSegmented* seg = (AromaSegmented*)event->target_node->node_widget_ptr;
    if (!seg) return false;

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

    bool in_bounds = (adjusted_x >= seg->rect.x &&
                      adjusted_x <= seg->rect.x + seg->rect.width &&
                      adjusted_y >= seg->rect.y &&
                      adjusted_y <= seg->rect.y + seg->rect.height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_MOVE:
            return in_bounds;
        case EVENT_TYPE_MOUSE_EXIT:
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                int index = __segmented_index_from_x(seg, adjusted_x);
                if (index >= 0 && index < seg->count && index != seg->selected_index) {
                    seg->selected_index = index;
                    if (seg->on_change) {
                        seg->on_change(event->target_node, index, seg->user_data);
                    }
                    aroma_node_invalidate(event->target_node);
                    __segmented_request_redraw(user_data);
                }
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

AromaNode* aroma_segmented_create(AromaNode* parent, int x, int y, int width, int height,
                                  const char** labels, int count)
{
    if (!parent || !labels || count <= 0) return NULL;

#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif

    AromaSegmented* seg = (AromaSegmented*)aroma_widget_alloc(sizeof(AromaSegmented));
    if (!seg) return NULL;

    memset(seg, 0, sizeof(AromaSegmented));
    seg->rect.x = x;
    seg->rect.y = y;
    seg->rect.width = width;
    seg->rect.height = height;
    seg->count = (count > AROMA_SEGMENTED_MAX) ? AROMA_SEGMENTED_MAX : count;
    seg->selected_index = 0;

    AromaTheme theme = aroma_theme_get_global();
    seg->track_color = theme.colors.surface;
    seg->thumb_color = theme.colors.primary;
    seg->text_color = theme.colors.text_primary;
    seg->text_selected_color = theme.colors.surface;
    seg->border_color = theme.colors.border;
    seg->use_theme_colors = true;
    seg->corner_radius = 8.0f;
    seg->text_scale = 1.0f;

    for (int i = 0; i < seg->count; i++) {
        if (labels[i]) {
            strncpy(seg->labels[i], labels[i], AROMA_SEGMENT_LABEL_MAX - 1);
            seg->labels[i][AROMA_SEGMENT_LABEL_MAX - 1] = '\0';
        } else {
            seg->labels[i][0] = '\0';
        }
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, seg);
    if (!node) {
        aroma_widget_free(seg);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_segmented_draw);

    if (!seg->font) {
        AromaNode* root_node = parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    if (g_windows[i].default_font) {
                        seg->font = g_windows[i].default_font;
                    }
                    break;
                }
            }
        }
    }

    aroma_node_invalidate(node);

    return node;
}

void aroma_segmented_set_selected(AromaNode* seg_node, int index)
{
    if (!seg_node || !seg_node->node_widget_ptr) return;
    AromaSegmented* seg = (AromaSegmented*)seg_node->node_widget_ptr;

    if (index < 0 || index >= seg->count || index >= AROMA_SEGMENTED_MAX) return;

    if (seg->selected_index != index) {
        seg->selected_index = index;
        aroma_node_invalidate(seg_node);
    }
}

int aroma_segmented_get_selected(AromaNode* seg_node)
{
    if (!seg_node || !seg_node->node_widget_ptr) return -1;
    AromaSegmented* seg = (AromaSegmented*)seg_node->node_widget_ptr;
    return seg->selected_index;
}

void aroma_segmented_set_on_change(AromaNode* seg_node,
                                   void (*callback)(AromaNode*, int, void*),
                                   void* user_data)
{
    if (!seg_node || !seg_node->node_widget_ptr) return;
    AromaSegmented* seg = (AromaSegmented*)seg_node->node_widget_ptr;
    seg->on_change = callback;
    seg->user_data = user_data;
}

void aroma_segmented_set_font(AromaNode* seg_node, AromaFont* font)
{
    if (!seg_node || !seg_node->node_widget_ptr) return;
    AromaSegmented* seg = (AromaSegmented*)seg_node->node_widget_ptr;
    seg->font = font;
    aroma_node_invalidate(seg_node);
}

bool aroma_segmented_setup_events(AromaNode* seg_node, void (*on_redraw_callback)(void*), void* user_data)
{
    (void)user_data;
    if (!seg_node) return false;

    aroma_event_subscribe(seg_node->node_id, EVENT_TYPE_MOUSE_MOVE,
                          __segmented_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(seg_node->node_id, EVENT_TYPE_MOUSE_EXIT,
                          __segmented_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(seg_node->node_id, EVENT_TYPE_MOUSE_CLICK,
                          __segmented_handle_event, (void*)on_redraw_callback, 90);
    return true;
}

void aroma_segmented_draw(AromaNode* seg_node, size_t window_id)
{
    if (!seg_node || !seg_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(seg_node)) return;

    AromaSegmented* seg = (AromaSegmented*)seg_node->node_widget_ptr;

    if (seg->count <= 0 || seg->count > AROMA_SEGMENTED_MAX) return;

#ifndef ESP32
    if (!seg->font) {
        for (int i = 0; i < g_window_count; ++i) {
            if (i >= AROMA_MAX_WINDOWS) break;
            if (g_windows[i].is_active && g_windows[i].window_id == window_id && g_windows[i].default_font) {
                seg->font = g_windows[i].default_font;
                break;
            }
        }
    }
#endif

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text) return;

    if (seg->use_theme_colors) {
        AromaTheme theme = aroma_theme_get_global();
        seg->track_color = theme.colors.surface;
        seg->thumb_color = theme.colors.primary;
        seg->text_color = theme.colors.text_primary;
        seg->text_selected_color = theme.colors.surface;
        seg->border_color = theme.colors.border;
    }

    gfx->fill_rectangle(window_id, seg->rect.x, seg->rect.y, seg->rect.width,
                        seg->rect.height, seg->track_color, false, seg->corner_radius);
    if (gfx->draw_hollow_rectangle) {
        gfx->draw_hollow_rectangle(window_id, seg->rect.x, seg->rect.y, seg->rect.width,
                                   seg->rect.height, seg->border_color, 1, false, seg->corner_radius);
    }

    int seg_width = seg->rect.width / seg->count;
    if (seg_width <= 0) seg_width = 1;

    for (int i = 0; i < seg->count; i++) {
        int x = seg->rect.x + i * seg_width;
        int w = (i == seg->count - 1) ? (seg->rect.x + seg->rect.width - x) : seg_width;
        if (w <= 0) w = 1;

        bool selected = (i == seg->selected_index);

        if (selected) {
            int pad = 2;
            gfx->fill_rectangle(window_id, x + pad, seg->rect.y + pad, w - pad * 2,
                                seg->rect.height - pad * 2, seg->thumb_color, false,
                                seg->corner_radius > 2.0f ? seg->corner_radius - 2.0f : 0.0f);
        }

        if (seg->font && gfx->render_text && seg->labels[i][0] != '\0') {
            uint32_t text_color = selected ? seg->text_selected_color : seg->text_color;
            int raw_w = aroma_font_get_line_width(seg->font, seg->labels[i]);
            /* Shrink-to-fit so long labels (e.g. "All Photos") stay
               inside their segment instead of overflowing. */
            float fit_scale = seg->text_scale;
            int avail = w - 12;
            if (raw_w > 0 && avail > 0) {
                float need = (float)raw_w * seg->text_scale;
                if (need > (float)avail) {
                    fit_scale = seg->text_scale * ((float)avail / need);
                    if (fit_scale < seg->text_scale * 0.6f)
                        fit_scale = seg->text_scale * 0.6f;
                }
            }
            int text_w = (int)(raw_w * fit_scale);
            int line_height = (int)(aroma_font_get_line_height(seg->font) * fit_scale);
            int text_x = x + (w - text_w) / 2;
            int text_y = seg->rect.y + (seg->rect.height - line_height) / 2;
            gfx->render_text(window_id, seg->font, seg->labels[i],
                             text_x, text_y, text_color, fit_scale);
        }
    }
}

void aroma_segmented_destroy(AromaNode* seg_node)
{
    if (!seg_node) return;

    if (seg_node->node_widget_ptr) {
        AromaSegmented* seg = (AromaSegmented*)seg_node->node_widget_ptr;
        aroma_widget_free(seg);
        seg_node->node_widget_ptr = NULL;
    }
}
