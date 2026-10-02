#ifndef AROMA_NODE_H
#define AROMA_NODE_H
#include "aroma_common.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AROMA_MAX_CHILD_NODES 128
#define AROMA_NODE_ID_INVALID 0
#define AROMA_MAX_DIRTY_NODES 1024
#define AROMA_CHILD_INITIAL_CAPACITY 4

typedef struct  AromaNode AromaNode;

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

typedef struct   {
    
    AromaLayoutType type;
    int left;
    int top;
    int right;
    int bottom;
    float width_percent;
    float height_percent;
    
    
    float flex_grow;
    float flex_shrink;
    int flex_basis;

    
    AromaLayoutMode mode;
    AromaFlexDirection flex_direction;
    AromaJustifyContent justify_content;
    AromaAlignItems align_items;
    int gap;
    
    
    int grid_cols;
    int grid_rows;
    int grid_row_gap;
    int grid_col_gap;

    
    int _cache_x;
    int _cache_y;
} AromaLayout;

struct  AromaNode
{
    AromaNodeType node_type;
    uint64_t node_id;
    int32_t z_index;
    float opacity;

    AromaNode* parent_node;
    AromaNode** child_nodes;       
    void *node_widget_ptr;
    AromaNodeDrawFn draw_cb;
    void (*destroy_cb)(struct AromaNode* node);

    uint64_t child_count;
    uint64_t child_capacity;       
    uint64_t dirty_frame;

    bool is_dirty;
    bool subtree_dirty;
    bool is_hidden;
    bool propagate_dirty;
    
    
    uint8_t _padding[4];
    
    AromaLayout layout;
};

#define AROMA_NODE_AS(node, Type) ((Type*)((node) ? (node)->node_widget_ptr : NULL))

static inline void* aroma_node_get_widget_ptr(AromaNode* node)
{
    return node ? node->node_widget_ptr : NULL;
}

static inline AromaRect* aroma_node_get_rect(AromaNode* node)
{
    return node ? (AromaRect*)node->node_widget_ptr : NULL;
}
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

AromaNode* aroma_node_get_window(AromaNode* node);

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
bool aroma_dirty_list_add(AromaNode* node);
bool aroma_dirty_list_has_entries(void);

uint64_t aroma_frame_number(void);
void aroma_frame_advance(void);
#ifdef __cplusplus
}
#endif
#endif