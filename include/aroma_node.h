#ifndef AROMA_NODE_H
#define AROMA_NODE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AROMA_MAX_CHILD_NODES 16
#define AROMA_NODE_ID_INVALID 0
#define AROMA_MAX_DIRTY_NODES 256

typedef struct AromaNode AromaNode;

typedef void (*AromaNodeDrawFn)(AromaNode* node, size_t window_id);

typedef enum AromaNodeType {
    NODE_TYPE_ROOT,
    NODE_TYPE_CONTAINER,
    NODE_TYPE_WIDGET
} AromaNodeType;

typedef enum {
    AROMA_LAYOUT_NONE,
    AROMA_LAYOUT_FILL_PARENT,
    AROMA_LAYOUT_CENTER,
    AROMA_LAYOUT_ANCHOR
} AromaLayoutType;

typedef enum {
    AROMA_LAYOUT_MODE_NONE,
    AROMA_LAYOUT_MODE_FLEX,
    AROMA_LAYOUT_MODE_GRID
} AromaLayoutMode;

typedef enum {
    AROMA_FLEX_ROW,
    AROMA_FLEX_COLUMN
} AromaFlexDirection;

typedef enum {
    AROMA_JUSTIFY_START,
    AROMA_JUSTIFY_CENTER,
    AROMA_JUSTIFY_END,
    AROMA_JUSTIFY_SPACE_BETWEEN,
    AROMA_JUSTIFY_SPACE_AROUND,
    AROMA_JUSTIFY_SPACE_EVENLY
} AromaJustifyContent;

typedef enum {
    AROMA_ALIGN_START,
    AROMA_ALIGN_CENTER,
    AROMA_ALIGN_END,
    AROMA_ALIGN_STRETCH
} AromaAlignItems;

typedef struct {
    // Self Layout
    AromaLayoutType type;
    int left;
    int top;
    int right;
    int bottom;
    float width_percent;
    float height_percent;
    
    // Flex Item props
    float flex_grow;
    float flex_shrink;
    int flex_basis;

    // Container Layout
    AromaLayoutMode mode;
    AromaFlexDirection flex_direction;
    AromaJustifyContent justify_content;
    AromaAlignItems align_items;
    int gap;
    
    // Grid props
    int grid_cols;
    int grid_rows;
    // Simple fixed column/row size for now, or use automated sizing
    int grid_row_gap;
    int grid_col_gap;
} AromaLayout;

typedef struct AromaNode
{
    AromaNodeType node_type;
    uint64_t node_id;
    int32_t z_index;
    AromaNode* parent_node;
    AromaNode* child_nodes[AROMA_MAX_CHILD_NODES];
    void *node_widget_ptr;
    AromaNodeDrawFn draw_cb;
    uint64_t child_count;
    bool is_dirty;
    bool is_hidden;
    bool propagate_dirty;
    AromaLayout layout;
} AromaNode;

#define AROMA_NODE_AS(node, Type) ((Type*)((node) ? (node)->node_widget_ptr : NULL))

void __node_system_init(void);
void __node_system_destroy(void);
AromaNode* __create_node(AromaNodeType node_type, AromaNode* parent_node, void *node_widget_ptr);
AromaNode* __add_child_node(AromaNodeType node_type, AromaNode* parent_node, void *node_widget_ptr);
AromaNode* __remove_child_node(AromaNode* parent_node, uint64_t node_id);
void __destroy_node(AromaNode* node);
void __destroy_node_tree(AromaNode* root_node);
AromaNode* __find_node_by_id(AromaNode* root, uint64_t node_id);

uint64_t __generate_node_id(void);
void __reset_node_id_counter(void);

void aroma_node_set_layout_anchor(AromaNode* node, int left, int top, int right, int bottom);
void aroma_node_set_layout_fill(AromaNode* node);
void aroma_node_set_layout_center(AromaNode* node);

// Flexbox & Grid helpers
void aroma_node_set_layout_mode(AromaNode* node, AromaLayoutMode mode);
void aroma_node_set_flex_direction(AromaNode* node, AromaFlexDirection dir);
void aroma_node_set_justify_content(AromaNode* node, AromaJustifyContent justify);
void aroma_node_set_align_items(AromaNode* node, AromaAlignItems align);
void aroma_node_set_flex_grow(AromaNode* node, float grow);
void aroma_node_set_gap(AromaNode* node, int gap);
void aroma_node_set_grid_cols(AromaNode* node, int cols);
void aroma_node_set_grid_rows(AromaNode* node, int rows);

void aroma_node_update_layout(AromaNode* root, int parent_x, int parent_y, int parent_width, int parent_height);

uint64_t __get_current_node_id_counter(void);

void __print_node_tree(AromaNode* root_node);
void __print_node_info(AromaNode* node);

void aroma_node_set_z_index(AromaNode* node, int32_t z_index);
int32_t aroma_node_get_z_index(AromaNode* node);

void aroma_node_invalidate(AromaNode* node);
void aroma_node_invalidate_tree(AromaNode* root);
bool aroma_node_is_dirty(AromaNode* node);
void aroma_node_mark_clean(AromaNode* node);
void aroma_node_set_draw_cb(AromaNode* node, AromaNodeDrawFn draw_cb);
AromaNodeDrawFn aroma_node_get_draw_cb(AromaNode* node);
void aroma_node_set_hidden(AromaNode* node, bool hidden);
bool aroma_node_is_hidden(AromaNode* node);

void aroma_dirty_list_init(void);
void aroma_dirty_list_clear(void);
AromaNode** aroma_dirty_list_get(size_t* count);
void aroma_dirty_list_add(AromaNode* node);
#ifdef __cplusplus
}
#endif
#endif
