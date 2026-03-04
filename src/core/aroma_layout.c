#include "core/aroma_node.h"
#include "core/aroma_common.h"
#include "core/aroma_font.h"
#include "widgets/aroma_label.h"
#include "widgets/aroma_listview.h"
#include "widgets/aroma_container.h"
#include "aroma_widgets.h"
#include <stdio.h>

void aroma_node_set_layout_anchor(AromaNode* node, int left, int top, int right, int bottom) {
    if (!node) return;
    node->layout.type = AROMA_LAYOUT_ANCHOR;
    node->layout.left = left;
    node->layout.top = top;
    node->layout.right = right;
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

static void apply_flex_layout(AromaNode* node, int x, int y, int width, int height) {
    
    int child_count = 0;
    int visible_indices[AROMA_MAX_CHILD_NODES];
    
    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        if (node->child_nodes[i] && !node->child_nodes[i]->is_hidden && node->child_nodes[i]->node_widget_ptr) {
            visible_indices[child_count++] = i;
        }
    }
    if (child_count == 0) return;

    bool is_row = (node->layout.flex_direction == AROMA_FLEX_ROW);
    int main_axis_size = is_row ? width : height;
    int cross_axis_size = is_row ? height : width;
    int gap = node->layout.gap;
    
    float total_grow = 0;
    int total_fixed_size = 0;
    int child_measurements[AROMA_MAX_CHILD_NODES] = {0};
    
    
    for (int idx = 0; idx < child_count; idx++) {
        int i = visible_indices[idx];
        AromaNode* child = node->child_nodes[i];
        if (!child || !child->node_widget_ptr) continue;

        WidgetBase* child_wb = (WidgetBase*)child->node_widget_ptr;

        
        if (child->draw_cb == aroma_label_draw) {
            const char* text = aroma_label_get_text(child);
            AromaFont* font = aroma_label_get_font(child);
            if (text && font) {
                float scale = aroma_label_get_scale(child);
                child_wb->rect.width = (int)(aroma_font_get_line_width(font, text) * scale);
                child_wb->rect.height = (int)(aroma_font_get_line_height(font) * scale);
            }
        }

        total_grow += child->layout.flex_grow;
        
        int child_size = is_row ? child_wb->rect.width : child_wb->rect.height;
        child_measurements[idx] = child_size;

        if (child->layout.flex_grow == 0) {
            total_fixed_size += child_size;
        }
    }

    int total_gap_size = (child_count > 1) ? (child_count - 1) * gap : 0;
    int remaining_space = main_axis_size - total_fixed_size - total_gap_size;
    
    int current_main_pos = 0;
    int adjusted_gap = gap;
    
    
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
                    adjusted_gap = remaining_space / (child_count - 1);
                    current_main_pos = 0;
                }
                break;
            case AROMA_JUSTIFY_SPACE_AROUND:
                if (child_count > 0) {
                    adjusted_gap = remaining_space / child_count;
                    current_main_pos = adjusted_gap / 2;
                }
                break;
            case AROMA_JUSTIFY_SPACE_EVENLY:
                if (child_count > 0) {
                    adjusted_gap = remaining_space / (child_count + 1);
                    current_main_pos = adjusted_gap;
                }
                break;
            default: break;
        }
    } 
    
    else if (total_grow > 0 && remaining_space > 0) {
        
        for (int idx = 0; idx < child_count; idx++) {
            int i = visible_indices[idx];
            AromaNode* child = node->child_nodes[i];
            if (!child) continue;
            
            if (child->layout.flex_grow > 0) {
                child_measurements[idx] = (int)((child->layout.flex_grow / total_grow) * remaining_space);
            }
        }
    }

    
    for (int idx = 0; idx < child_count; idx++) {
        int i = visible_indices[idx];
        AromaNode* child = node->child_nodes[i];
        if (!child || !child->node_widget_ptr) continue;

        WidgetBase* child_w = (WidgetBase*)child->node_widget_ptr;
        int child_main_size = child_measurements[idx];

        
        if (is_row) {
            if (child_main_size > 0) child_w->rect.width = child_main_size;
        } else {
            if (child_main_size > 0) child_w->rect.height = child_main_size;
        }

        
        child_main_size = is_row ? child_w->rect.width : child_w->rect.height;
        int child_cross_size = is_row ? child_w->rect.height : child_w->rect.width;
        int child_cross_pos = 0;

        
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
            child_w->rect.height = child_cross_size;
            child_w->rect.x = x + current_main_pos;
            child_w->rect.y = y + child_cross_pos;
        } else {
            child_w->rect.width = child_cross_size;
            child_w->rect.x = x + child_cross_pos;
            child_w->rect.y = y + current_main_pos;
        }

        
        current_main_pos += child_main_size;
        if (idx < child_count - 1) {
            current_main_pos += adjusted_gap;
        }
    }
    
    
    for (int idx = 0; idx < child_count; idx++) {
        int i = visible_indices[idx];
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden || !child->node_widget_ptr) continue;
        
        WidgetBase* child_w = (WidgetBase*)child->node_widget_ptr;
        aroma_node_update_layout(child, child_w->rect.x, child_w->rect.y, 
                               child_w->rect.width, child_w->rect.height);
    }
}

static void apply_grid_layout(AromaNode* node, int x, int y, int width, int height) {
    int cols = node->layout.grid_cols;
    int rows = node->layout.grid_rows;
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    int gap = node->layout.gap;
    int total_cells = cols * rows;
    
    int total_gap_width = (cols - 1) * gap;
    int total_gap_height = (rows - 1) * gap;
    
    
    int cell_w = (width - total_gap_width) / cols;
    int cell_h = (height - total_gap_height) / rows;
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;

    int current_col = 0;
    int current_row = 0;
    int cells_used = 0;

    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden || !child->node_widget_ptr) continue;
        
        if (cells_used >= total_cells) {
            #ifndef ESP32
            printf("Warning: Grid overflow - more children than cells\n");
            #endif
            break;
        }

        WidgetBase* widget = (WidgetBase*)child->node_widget_ptr;
        
        
        widget->rect.width = cell_w;
        widget->rect.height = cell_h;
        
        
        widget->rect.x = x + (current_col * (cell_w + gap));
        widget->rect.y = y + (current_row * (cell_h + gap));
        
        cells_used++;
        current_col++;
        if (current_col >= cols) {
            current_col = 0;
            current_row++;
        }
    }
    
    
    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden || !child->node_widget_ptr) continue;
        
        WidgetBase* child_w = (WidgetBase*)child->node_widget_ptr;
        aroma_node_update_layout(child, child_w->rect.x, child_w->rect.y, 
                               child_w->rect.width, child_w->rect.height);
    }
}

void aroma_node_update_layout(AromaNode* start_node, int parent_x, int parent_y, int parent_width, int parent_height) {
    if (!start_node) return;
    
    
    if (!start_node->node_widget_ptr) {
        
        for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
            if (start_node->child_nodes[i] && !start_node->child_nodes[i]->is_hidden) {
                aroma_node_update_layout(start_node->child_nodes[i], parent_x, parent_y, parent_width, parent_height);
            }
        }
        return;
    }

    AromaRect* widget = (AromaRect*)start_node->node_widget_ptr;
    int new_x = parent_x;
    int new_y = parent_y;
    int new_w = parent_width;
    int new_h = parent_height;
    
    
    switch (start_node->layout.type) {
        case AROMA_LAYOUT_FILL_PARENT:
            widget->x = parent_x;
            widget->y = parent_y;
            widget->width = parent_width;
            widget->height = parent_height;
            new_x = parent_x;
            new_y = parent_y;
            new_w = parent_width;
            new_h = parent_height;
            break;

        case AROMA_LAYOUT_CENTER:
            
            if (widget->width == 0) widget->width = parent_width;
            if (widget->height == 0) widget->height = parent_height;
            
            widget->x = parent_x + (parent_width - widget->width) / 2;
            widget->y = parent_y + (parent_height - widget->height) / 2;
            new_x = widget->x;
            new_y = widget->y;
            new_w = widget->width;
            new_h = widget->height;
            break;

        case AROMA_LAYOUT_ANCHOR: {
            
            int width = widget->width;
            int height = widget->height;
            
            
            if (start_node->layout.left >= 0) {
                new_x = parent_x + start_node->layout.left;
            }
            
            if (start_node->layout.top >= 0) {
                new_y = parent_y + start_node->layout.top;
            }
            
            
            if (start_node->layout.left >= 0 && start_node->layout.right >= 0) {
                width = parent_width - start_node->layout.left - start_node->layout.right;
                if (width < 0) width = 0;
            } 
            
            else if (start_node->layout.right >= 0 && start_node->layout.left < 0) {
                new_x = parent_x + parent_width - start_node->layout.right - width;
            }
            
            
            if (start_node->layout.top >= 0 && start_node->layout.bottom >= 0) {
                height = parent_height - start_node->layout.top - start_node->layout.bottom;
                if (height < 0) height = 0;
            }
            
            else if (start_node->layout.bottom >= 0 && start_node->layout.top < 0) {
                new_y = parent_y + parent_height - start_node->layout.bottom - height;
            }
            
            
            widget->x = new_x;
            widget->y = new_y;
            widget->width = width;
            widget->height = height;
            
            new_w = width;
            new_h = height;
            break;
        }

        default: 
            
            new_x = widget->x;
            new_y = widget->y;
            new_w = widget->width;
            new_h = widget->height;
            break;
    }

    
    aroma_node_invalidate(start_node);

    /* For scrollable containers with explicit content size, expand the
       layout area so children are positioned across the entire scrollable
       region.  For auto-sizing containers, use the viewport dimensions
       for layout (flex wraps / sizes relative to viewport), then measure
       children afterwards. */
    int layout_w = new_w;
    int layout_h = new_h;
    bool is_scrollable = aroma_container_is_scrollable(start_node);
    if (is_scrollable) {
        int cw = 0, ch = 0;
        aroma_container_get_content_size(start_node, &cw, &ch);
        /* Only expand if content size was explicitly set (larger than
           viewport).  Auto-measured containers keep viewport dims so
           flex grow/stretch use the viewport, not the content. */
        if (cw > layout_w) layout_w = cw;
        if (ch > layout_h) layout_h = ch;
    }

    if (start_node->layout.mode == AROMA_LAYOUT_MODE_FLEX) {
        apply_flex_layout(start_node, new_x, new_y, layout_w, layout_h);
    } else if (start_node->layout.mode == AROMA_LAYOUT_MODE_GRID) {
        apply_grid_layout(start_node, new_x, new_y, layout_w, layout_h);
    } else {
        /* NONE layout mode: children keep their relative positions.
           When the container moves (e.g. repositioned by a parent flex layout),
           shift all children by the same delta so they follow. */
        int delta_x = new_x - start_node->layout._cache_x;
        int delta_y = new_y - start_node->layout._cache_y;
        start_node->layout._cache_x = new_x;
        start_node->layout._cache_y = new_y;

        for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
            AromaNode* child = start_node->child_nodes[i];
            if (!child || child->is_hidden) continue;
            
            if (child->node_widget_ptr) {
                AromaRect* child_w = (AromaRect*)child->node_widget_ptr;
                child_w->x += delta_x;
                child_w->y += delta_y;
                aroma_node_update_layout(child, child_w->x, child_w->y, 
                                       child_w->width, child_w->height);
            }
        }
    }

    /* After children are laid out, auto-measure the content extent
       for scrollable containers that don't have an explicit size. */
    if (is_scrollable) {
        aroma_container_update_auto_content_size(start_node);
    }
}