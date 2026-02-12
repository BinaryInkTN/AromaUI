/*
 Copyright (c) 2026 BinaryInkTN
 */

#include "core/aroma_node.h"
#include "core/aroma_common.h"
#include "core/aroma_font.h"
#include "widgets/aroma_label.h"
#include <stdio.h>

typedef struct WidgetBase {
    AromaRect rect;
} WidgetBase;

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
    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        if (node->child_nodes[i] && !node->child_nodes[i]->is_hidden) child_count++;
    }
    if (child_count == 0) return;

    bool is_row = (node->layout.flex_direction == AROMA_FLEX_ROW);
    int main_axis_size = is_row ? width : height;
    int cross_axis_size = is_row ? height : width;
    int gap = node->layout.gap;
    
    float total_grow = 0;
    int used_space = 0;
    int fixed_children_count = 0;

    // First pass: Measure children and fixed size
    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden) continue;

        WidgetBase* widget = (WidgetBase*)child->node_widget_ptr;
        if (!widget) continue;

        // Auto-measure text widgets if size is 0
        if (child->draw_cb == aroma_label_draw && (widget->rect.width == 0 || widget->rect.height == 0)) {
            const char* text = aroma_label_get_text(child);
            AromaFont* font = aroma_label_get_font(child);
            if (text && font) {
                float scale = aroma_label_get_scale(child);
                widget->rect.width = (int)(aroma_font_get_line_width(font, text) * scale);
                widget->rect.height = (int)(aroma_font_get_line_height(font) * scale);
            }
        }

        total_grow += child->layout.flex_grow;
        
        if (child->layout.flex_grow == 0) {
           used_space += (is_row ? widget->rect.width : widget->rect.height);
        }
        fixed_children_count++;
    }

    if (fixed_children_count > 1) {
        used_space += (fixed_children_count - 1) * gap;
    }

    int remaining_space = main_axis_size - used_space;
    if (remaining_space < 0) remaining_space = 0;

    int current_main_pos = 0;
    int gap_space = gap;
    
    if (total_grow == 0) {
        switch (node->layout.justify_content) {
            case AROMA_JUSTIFY_CENTER:
                current_main_pos = remaining_space / 2;
                break;
            case AROMA_JUSTIFY_END:
                current_main_pos = remaining_space;
                break;
            case AROMA_JUSTIFY_SPACE_BETWEEN:
                if (fixed_children_count > 1) {
                    gap_space = remaining_space / (fixed_children_count - 1);
                    current_main_pos = 0;
                  
                }
                break;
             case AROMA_JUSTIFY_SPACE_AROUND:
                 if (fixed_children_count > 0) {
                     gap_space = remaining_space / fixed_children_count; 
                     current_main_pos = gap_space / 2;
                 }
                 break;
             case AROMA_JUSTIFY_SPACE_EVENLY:
                  if (fixed_children_count > 0) {
                     gap_space = remaining_space / (fixed_children_count + 1);
                     current_main_pos = gap_space;
                  }
                  break;
            default: break;
        }
    }

    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden) continue;
        
        WidgetBase* widget = (WidgetBase*)child->node_widget_ptr;
        if (!widget) continue;

        int child_main_size = 0;
        if (child->layout.flex_grow > 0 && total_grow > 0) {
             child_main_size = (int)((child->layout.flex_grow / total_grow) * remaining_space);
             if (child->layout.flex_grow == 0) {
                 child_main_size = is_row ? widget->rect.width : widget->rect.height;
             }
        } else {
            child_main_size = is_row ? widget->rect.width : widget->rect.height;
        }
        
        if (is_row) widget->rect.width = child_main_size;
        else widget->rect.height = child_main_size;

        int child_cross_size = is_row ? widget->rect.height : widget->rect.width;
        int child_cross_pos = 0;

        switch (node->layout.align_items) {
            case AROMA_ALIGN_STRETCH:
                child_cross_size = cross_axis_size;
                break;
            case AROMA_ALIGN_CENTER:
                child_cross_pos =  (cross_axis_size - child_cross_size) / 2;
                break;
            case AROMA_ALIGN_END:
                child_cross_pos = cross_axis_size - child_cross_size;
                break;
            default: break;
        }

        if (is_row) {
            widget->rect.height = child_cross_size;
            widget->rect.x = x + current_main_pos;
            widget->rect.y = y + child_cross_pos;
        } else {
            widget->rect.width = child_cross_size;
            widget->rect.x = x + child_cross_pos;
            widget->rect.y = y + current_main_pos;
        }

        current_main_pos += child_main_size + (total_grow == 0 ? gap_space : gap);
        
        aroma_node_update_layout(child, widget->rect.x, widget->rect.y, widget->rect.width, widget->rect.height);
    }
}

static void apply_grid_layout(AromaNode* node, int x, int y, int width, int height) {
    int cols = node->layout.grid_cols;
    int rows = node->layout.grid_rows;
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    int gap = node->layout.gap;
    
    int cell_w = (width - (cols - 1) * gap) / cols;
    int cell_h = (height - (rows - 1) * gap) / rows;
    
    if (cell_w < 0) cell_w = 0;
    if (cell_h < 0) cell_h = 0;

    int current_col = 0;
    int current_row = 0;

    for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        AromaNode* child = node->child_nodes[i];
        if (!child || child->is_hidden) continue;
        
        WidgetBase* widget = (WidgetBase*)child->node_widget_ptr;
        if (!widget) continue;

        widget->rect.width = cell_w;
        widget->rect.height = cell_h;
        widget->rect.x = x + (current_col * (cell_w + gap));
        widget->rect.y = y + (current_row * (cell_h + gap));
        
        current_col++;
        if (current_col >= cols) {
            current_col = 0;
            current_row++;
            if (current_row >= rows) break;
        }

        aroma_node_update_layout(child, widget->rect.x, widget->rect.y, widget->rect.width, widget->rect.height);
    }
}

void aroma_node_update_layout(AromaNode* start_node, int parent_x, int parent_y, int parent_width, int parent_height) {
    if (!start_node) return;

    int new_x = parent_x;
    int new_y = parent_y;
    int new_w = parent_width;
    int new_h = parent_height;
    if (start_node->node_widget_ptr) {
        WidgetBase* widget = (WidgetBase*)start_node->node_widget_ptr;
        
        switch (start_node->layout.type) {
            case AROMA_LAYOUT_FILL_PARENT:
                widget->rect.x = parent_x;
                widget->rect.y = parent_y;
                widget->rect.width = parent_width;
                widget->rect.height = parent_height;
                break;
                
            case AROMA_LAYOUT_CENTER:
                widget->rect.x = parent_x + (parent_width - widget->rect.width) / 2;
                widget->rect.y = parent_y + (parent_height - widget->rect.height) / 2;
                break;
                
            case AROMA_LAYOUT_ANCHOR:
                 if (start_node->layout.left >= 0) {
                     widget->rect.x = parent_x + start_node->layout.left;
                 }
                 if (start_node->layout.top >= 0) {
                     widget->rect.y = parent_y + start_node->layout.top;
                 }
                 if (start_node->layout.left >= 0 && start_node->layout.right >= 0) {
                     widget->rect.width = parent_width - start_node->layout.left - start_node->layout.right;
                 } else if (start_node->layout.right >= 0) {
                     widget->rect.x = parent_x + parent_width - start_node->layout.right - widget->rect.width;
                 }
                 
                 if (start_node->layout.top >= 0 && start_node->layout.bottom >= 0) {
                     widget->rect.height = parent_height - start_node->layout.top - start_node->layout.bottom;
                 } else if (start_node->layout.bottom >= 0) {
                     widget->rect.y = parent_y + parent_height - start_node->layout.bottom - widget->rect.height;
                 }
                break;
            default:
                break;
        }
        
        new_x = widget->rect.x;
        new_y = widget->rect.y;
        new_w = widget->rect.width;
        new_h = widget->rect.height;

        start_node->x = new_x;
        start_node->y = new_y;
        start_node->width = new_w;
        start_node->height = new_h;
    }
    
    if (start_node->layout.mode == AROMA_LAYOUT_MODE_FLEX) {
        apply_flex_layout(start_node, new_x, new_y, new_w, new_h);
    } else if (start_node->layout.mode == AROMA_LAYOUT_MODE_GRID) {
        apply_grid_layout(start_node, new_x, new_y, new_w, new_h);
    } else {
        for (int i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
            if (start_node->child_nodes[i]) {
                aroma_node_update_layout(start_node->child_nodes[i], new_x, new_y, new_w, new_h);
            }
        }
    }
}
