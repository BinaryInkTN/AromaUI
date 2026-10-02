#include "core/aroma_node.h"
#include "core/aroma_common.h"
#include "core/aroma_font.h"
#include "widgets/aroma_label.h"
#include "widgets/aroma_listview.h"
#include "widgets/aroma_container.h"
#include "aroma_widgets.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static bool g_layout_ready = false;

static inline bool is_valid_node(const AromaNode* node)
{
    if (!node) return false;
    #ifdef __EMSCRIPTEN__
    if (((uintptr_t)node & 3) != 0) return false;
    #else
    if (((uintptr_t)node % _Alignof(AromaNode)) != 0) return false;
    #endif
    return true;
}

static inline bool is_valid_widget_ptr(const void* ptr)
{
    if (!ptr) return false;
    #ifdef __EMSCRIPTEN__
    if (((uintptr_t)ptr & 3) != 0) return false;
    #else
    if (((uintptr_t)ptr % _Alignof(max_align_t)) != 0) return false;
    #endif
    return true;
}

void aroma_node_set_layout_anchor(AromaNode* node, int left, int top, int right, int bottom) {
    if (!node) return;
    node->layout.type   = AROMA_LAYOUT_ANCHOR;
    node->layout.left   = left;
    node->layout.top    = top;
    node->layout.right  = right;
    node->layout.bottom = bottom;
}

void aroma_node_set_layout_fill(AromaNode* node) {
    if (!node) return;
    node->layout.type = AROMA_LAYOUT_FILL_PARENT;
}

void aroma_node_set_layout_center(AromaNode* node) {
    if (!node) return;
    node->layout.type = AROMA_LAYOUT_CENTER;
}

void aroma_node_set_layout_mode(AromaNode* node, AromaLayoutMode mode) {
    if (node) node->layout.mode = mode;
}

void aroma_node_set_flex_direction(AromaNode* node, AromaFlexDirection dir) {
    if (node) node->layout.flex_direction = dir;
}

void aroma_node_set_justify_content(AromaNode* node, AromaJustifyContent justify) {
    if (node) node->layout.justify_content = justify;
}

void aroma_node_set_align_items(AromaNode* node, AromaAlignItems align) {
    if (node) node->layout.align_items = align;
}

void aroma_node_set_flex_grow(AromaNode* node, float grow) {
    if (node) node->layout.flex_grow = grow;
}

void aroma_node_set_gap(AromaNode* node, int gap) {
    if (node) node->layout.gap = gap;
}

void aroma_node_set_grid_cols(AromaNode* node, int cols) {
    if (node) node->layout.grid_cols = cols;
}

void aroma_node_set_grid_rows(AromaNode* node, int rows) {
    if (node) node->layout.grid_rows = rows;
}

static void measure_child_size(AromaNode* child, AromaRect* child_wb) {
    if (!child || !child_wb) return;
    
    if (child_wb->width > 0 && child_wb->height > 0) return;
    
    if (child->draw_cb == aroma_label_draw) {
        const char* text  = aroma_label_get_text(child);
        AromaFont*  font  = aroma_label_get_font(child);
        if (text && font) {
            float scale = aroma_label_get_scale(child);
            if (child_wb->width <= 0) {
                child_wb->width  = (int)(aroma_font_get_line_width(font, text) * scale);
            }
            if (child_wb->height <= 0) {
                child_wb->height = (int)(aroma_font_get_line_height(font) * scale);
            }
        }
    }
    
    if (child_wb->width <= 0)  child_wb->width  = 10;
    if (child_wb->height <= 0) child_wb->height = 10;
}

static void apply_flex_layout(AromaNode* node, int x, int y, int width, int height) {

    if (width <= 0 || height <= 0) return;

    int child_count = 0;
    int visible_indices[AROMA_MAX_CHILD_NODES];

    if (node->child_count > AROMA_MAX_CHILD_NODES) {
        LOG_ERROR("apply_flex_layout: node %llu has corrupt child_count %llu",
                  (unsigned long long)node->node_id,
                  (unsigned long long)node->child_count);
        return;
    }

    for (uint64_t i = 0; i < node->child_count; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!is_valid_node(child)) continue;
        if (child->is_hidden) continue;
        if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;
        visible_indices[child_count++] = (int)i;
    }
    if (child_count == 0) return;

    bool is_row        = (node->layout.flex_direction == AROMA_FLEX_ROW);
    int main_axis_size = is_row ? width : height;
    int cross_axis_size = is_row ? height : width;
    int gap            = node->layout.gap;

    float total_grow      = 0;
    int total_fixed_size  = 0;
    int child_measurements[AROMA_MAX_CHILD_NODES];
    memset(child_measurements, 0, sizeof(child_measurements));

    for (int idx = 0; idx < child_count; idx++) {
        int i = visible_indices[idx];
        AromaNode* child = node->child_nodes[i];
        if (!is_valid_node(child)) continue;
        if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;

        AromaRect* child_wb = (AromaRect*)child->node_widget_ptr;
        measure_child_size(child, child_wb);

        total_grow += child->layout.flex_grow;

        int child_size = is_row ? child_wb->width : child_wb->height;
        child_measurements[idx] = child_size;

        total_fixed_size += child_size;
    }

    int total_gap_size  = (child_count > 1) ? (child_count - 1) * gap : 0;
    int remaining_space = main_axis_size - total_fixed_size - total_gap_size;

    int current_main_pos = 0;
    int adjusted_gap     = gap;

    if (total_grow == 0 && remaining_space > 0) {
        switch (node->layout.justify_content) {
            case AROMA_JUSTIFY_CENTER:
                current_main_pos = remaining_space / 2;
                break;
            case AROMA_JUSTIFY_END:
                current_main_pos = remaining_space;
                break;
            case AROMA_JUSTIFY_SPACE_BETWEEN:
                if (child_count > 1) {
                    adjusted_gap     = remaining_space / (child_count - 1);
                    current_main_pos = 0;
                }
                break;
            case AROMA_JUSTIFY_SPACE_AROUND:
                if (child_count > 0) {
                    adjusted_gap     = remaining_space / child_count;
                    current_main_pos = adjusted_gap / 2;
                }
                break;
            case AROMA_JUSTIFY_SPACE_EVENLY:
                if (child_count > 0) {
                    adjusted_gap     = remaining_space / (child_count + 1);
                    current_main_pos = adjusted_gap;
                }
                break;
            default: break;
        }
    } else if (total_grow > 0 && remaining_space > 0) {
        for (int idx = 0; idx < child_count; idx++) {
            int i = visible_indices[idx];
            AromaNode* child = node->child_nodes[i];
            if (!is_valid_node(child)) continue;
            if (child->layout.flex_grow > 0) {
                int extra = (int)((child->layout.flex_grow / total_grow) * remaining_space);
                child_measurements[idx] += extra;
            }
        }
    }

    for (int idx = 0; idx < child_count; idx++) {
        int i = visible_indices[idx];
        AromaNode* child = node->child_nodes[i];
        if (!is_valid_node(child)) continue;
        if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;

        AromaRect* child_w     = (AromaRect*)child->node_widget_ptr;
        int child_main_size     = child_measurements[idx];

        if (is_row) {
            if (child_main_size > 0) child_w->width = child_main_size;
        } else {
            if (child_main_size > 0) child_w->height = child_main_size;
        }

        child_main_size  = is_row ? child_w->width  : child_w->height;
        int child_cross_size = is_row ? child_w->height : child_w->width;
        int child_cross_pos  = 0;

        switch (node->layout.align_items) {
            case AROMA_ALIGN_STRETCH:
                child_cross_size = cross_axis_size;
                break;
            case AROMA_ALIGN_CENTER:
                child_cross_pos = (cross_axis_size - child_cross_size) / 2;
                break;
            case AROMA_ALIGN_END:
                child_cross_pos = cross_axis_size - child_cross_size;
                break;
            default:
                child_cross_pos = 0;
                break;
        }

        if (is_row) {
            child_w->height = child_cross_size;
            child_w->x      = x + current_main_pos;
            child_w->y      = y + child_cross_pos;
        } else {
            child_w->width = child_cross_size;
            child_w->x     = x + child_cross_pos;
            child_w->y     = y + current_main_pos;
        }

        if (child_w->width <= 0)  child_w->width  = 10;
        if (child_w->height <= 0) child_w->height = 10;

        current_main_pos += child_main_size;
        if (idx < child_count - 1) {
            current_main_pos += adjusted_gap;
        }
    }

    for (int idx = 0; idx < child_count; idx++) {
        int i = visible_indices[idx];
        AromaNode* child = node->child_nodes[i];
        if (!is_valid_node(child)) continue;
        if (child->is_hidden) continue;
        if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;

        AromaRect* child_w = (AromaRect*)child->node_widget_ptr;
        aroma_node_update_layout(child,
                                 child_w->x, child_w->y,
                                 child_w->width, child_w->height);
    }
}

static void apply_grid_layout(AromaNode* node, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    
    int cols = node->layout.grid_cols;
    int rows = node->layout.grid_rows;
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    int gap        = node->layout.gap;
    int total_cells = cols * rows;

    int cell_w = (width  - (cols - 1) * gap) / cols;
    int cell_h = (height - (rows - 1) * gap) / rows;
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;

    int current_col = 0;
    int current_row = 0;
    int cells_used  = 0;

    if (node->child_count > AROMA_MAX_CHILD_NODES) {
        LOG_ERROR("apply_grid_layout: node %llu has corrupt child_count %llu",
                  (unsigned long long)node->node_id,
                  (unsigned long long)node->child_count);
        return;
    }

    for (uint64_t i = 0; i < node->child_count; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!is_valid_node(child)) continue;
        if (child->is_hidden) continue;
        if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;

        if (cells_used >= total_cells) {
            break;
        }

        AromaRect* widget = (AromaRect*)child->node_widget_ptr;

        widget->width  = cell_w;
        widget->height = cell_h;
        widget->x      = x + (current_col * (cell_w + gap));
        widget->y      = y + (current_row * (cell_h + gap));

        if (widget->width <= 0)  widget->width  = 10;
        if (widget->height <= 0) widget->height = 10;

        cells_used++;
        current_col++;
        if (current_col >= cols) {
            current_col = 0;
            current_row++;
        }
    }

    for (uint64_t i = 0; i < node->child_count; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!is_valid_node(child)) continue;
        if (child->is_hidden) continue;
        if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;

        AromaRect* child_w = (AromaRect*)child->node_widget_ptr;
        aroma_node_update_layout(child,
                                 child_w->x, child_w->y,
                                 child_w->width, child_w->height);
    }
}

void aroma_node_update_layout(AromaNode* start_node, int parent_x, int parent_y,
                              int parent_width, int parent_height) {
    if (!is_valid_node(start_node)) return;
    if (!is_valid_widget_ptr(start_node->node_widget_ptr)) {
    
        if (start_node->child_count > AROMA_MAX_CHILD_NODES) return;
        for (uint64_t i = 0; i < start_node->child_count; i++) {
            AromaNode* child = start_node->child_nodes[i];
            if (is_valid_node(child) &&
                is_valid_widget_ptr(child->node_widget_ptr) &&
                !child->is_hidden) {
                aroma_node_update_layout(child, parent_x, parent_y,
                                         parent_width, parent_height);
            }
        }
        return;
    }

    if (start_node->child_count > AROMA_MAX_CHILD_NODES) {
        LOG_ERROR("aroma_node_update_layout: node %llu corrupt child_count %llu",
                  (unsigned long long)start_node->node_id,
                  (unsigned long long)start_node->child_count);
        return;
    }

    AromaRect* widget = aroma_node_get_rect(start_node);
    if (!widget) return;

    int new_x = parent_x;
    int new_y = parent_y;
    int new_w = parent_width;
    int new_h = parent_height;

    switch (start_node->layout.type) {
        case AROMA_LAYOUT_FILL_PARENT:
            widget->x      = parent_x;
            widget->y      = parent_y;
            widget->width  = parent_width;
            widget->height = parent_height;
            new_x = parent_x;
            new_y = parent_y;
            new_w = parent_width;
            new_h = parent_height;
            break;

        case AROMA_LAYOUT_CENTER:
            if (widget->width  == 0) widget->width  = parent_width;
            if (widget->height == 0) widget->height = parent_height;
            widget->x = parent_x + (parent_width  - widget->width)  / 2;
            widget->y = parent_y + (parent_height - widget->height) / 2;
            new_x = widget->x;
            new_y = widget->y;
            new_w = widget->width;
            new_h = widget->height;
            break;

        case AROMA_LAYOUT_ANCHOR: {
            int w = widget->width;
            int h = widget->height;

            if (start_node->layout.left >= 0) {
                new_x = parent_x + start_node->layout.left;
            }
            if (start_node->layout.top >= 0) {
                new_y = parent_y + start_node->layout.top;
            }

            if (start_node->layout.left >= 0 && start_node->layout.right >= 0) {
                w = parent_width - start_node->layout.left - start_node->layout.right;
                if (w < 0) w = 0;
            } else if (start_node->layout.right >= 0 && start_node->layout.left < 0) {
                if (w <= 0) w = parent_width;
                new_x = parent_x + parent_width - start_node->layout.right - w;
            }

            if (start_node->layout.top >= 0 && start_node->layout.bottom >= 0) {
                h = parent_height - start_node->layout.top - start_node->layout.bottom;
                if (h < 0) h = 0;
            } else if (start_node->layout.bottom >= 0 && start_node->layout.top < 0) {
                if (h <= 0) h = parent_height;
                new_y = parent_y + parent_height - start_node->layout.bottom - h;
            }

            widget->x      = new_x;
            widget->y      = new_y;
            widget->width  = w;
            widget->height = h;
            new_w = w;
            new_h = h;
            break;
        }

        default:
            new_x = widget->x;
            new_y = widget->y;
            new_w = widget->width;
            new_h = widget->height;
            break;
    }

    if (widget->width <= 0)  widget->width  = 10;
    if (widget->height <= 0) widget->height = 10;
    if (new_w <= 0) new_w = 10;
    if (new_h <= 0) new_h = 10;

    aroma_node_invalidate(start_node);

    int prev_cache_x = start_node->layout._cache_x;
    int prev_cache_y = start_node->layout._cache_y;
    start_node->layout._cache_x = new_x;
    start_node->layout._cache_y = new_y;

    int layout_w     = new_w;
    int layout_h     = new_h;
    bool is_scrollable = aroma_container_is_scrollable(start_node);
    if (is_scrollable) {
        int cw = 0, ch = 0;
        aroma_container_get_content_size(start_node, &cw, &ch);
        if (cw > layout_w) layout_w = cw;
        if (ch > layout_h) layout_h = ch;
    }

    if (start_node->layout.mode == AROMA_LAYOUT_MODE_FLEX) {
        apply_flex_layout(start_node, new_x, new_y, layout_w, layout_h);
    } else if (start_node->layout.mode == AROMA_LAYOUT_MODE_GRID) {
        apply_grid_layout(start_node, new_x, new_y, layout_w, layout_h);
    } else {
        int delta_x = new_x - prev_cache_x;
        int delta_y = new_y - prev_cache_y;

        for (uint64_t i = 0; i < start_node->child_count; i++) {
            AromaNode* child = start_node->child_nodes[i];
            if (!is_valid_node(child)) continue;
            if (!is_valid_widget_ptr(child->node_widget_ptr)) continue;

            AromaRect* child_w = aroma_node_get_rect(child);
            if (!child_w) continue;

            if (child->layout.type == AROMA_LAYOUT_NONE && (delta_x || delta_y)) {
                child_w->x += delta_x;
                child_w->y += delta_y;
            }

            if (!child->is_hidden) {
                aroma_node_update_layout(child, new_x, new_y, new_w, new_h);
            }
        }
    }

    if (is_scrollable) {
        aroma_container_update_auto_content_size(start_node);
    }
}

bool aroma_is_layout_ready(void) {
    return g_layout_ready;
}

void aroma_set_layout_ready(void) {
    g_layout_ready = true;
}