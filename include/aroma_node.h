/**
 * @file aroma_node.h
 * @brief Scene graph node and layout system.
 *
 * Defines the base unit of the UI tree (AromaNode), along with layout properties
 * (Flexbox, Grid, Anchoring) and dirty state management for efficient rendering.
 */

#ifndef AROMA_NODE_H
#define AROMA_NODE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of direct children a node can have (fixed size for now). */
#define AROMA_MAX_CHILD_NODES 64
/** @brief Sentinel value for an invalid node ID. */
#define AROMA_NODE_ID_INVALID 0
/** @brief Maximum number of dirty nodes tracked per frame. */
#define AROMA_MAX_DIRTY_NODES 1024

typedef struct AromaNode AromaNode;

/**
 * @brief Callback function type for drawing a node.
 * @param node The node to draw.
 * @param window_id The ID of the window being drawn to.
 */
typedef void (*AromaNodeDrawFn)(AromaNode* node, size_t window_id);

/** @brief Node types in the scene graph. */
typedef enum AromaNodeType {
    NODE_TYPE_ROOT,      /**< Root window or scene node. */
    NODE_TYPE_CONTAINER, /**< Container node (no visual, just holds children). */
    NODE_TYPE_WIDGET     /**< Visible widget node. */
} AromaNodeType;

/** @brief Methods for sizing/positioning an element within its parent. */
typedef enum {
    AROMA_LAYOUT_NONE,        /**< Absolute positioning. */
    AROMA_LAYOUT_FILL_PARENT, /**< Fill available space. */
    AROMA_LAYOUT_CENTER,      /**< Center within parent. */
    AROMA_LAYOUT_ANCHOR       /**< Anchor to specific sides. */
} AromaLayoutType;

/** @brief Layout strategy for children containing nodes (Flex/Grid). */
typedef enum {
    AROMA_LAYOUT_MODE_NONE, /**< No automatic layout of children. */
    AROMA_LAYOUT_MODE_FLEX, /**< Flexbox-like layout. */
    AROMA_LAYOUT_MODE_GRID  /**< Grid layout. */
} AromaLayoutMode;

/** @brief Flexbox direction. */
typedef enum {
    AROMA_FLEX_ROW,    /**< Children arranged horizontally. */
    AROMA_FLEX_COLUMN  /**< Children arranged vertically. */
} AromaFlexDirection;

/** @brief Alignment along the main axis. */
typedef enum {
    AROMA_JUSTIFY_START,         /**< Pack to start. */
    AROMA_JUSTIFY_CENTER,        /**< Pack to center. */
    AROMA_JUSTIFY_END,           /**< Pack to end. */
    AROMA_JUSTIFY_SPACE_BETWEEN, /**< Distribute evenly, start/end flush. */
    AROMA_JUSTIFY_SPACE_AROUND,  /**< Distribute evenly with space at ends. */
    AROMA_JUSTIFY_SPACE_EVENLY   /**< Distribute evenly with equal space. */
} AromaJustifyContent;

/** @brief Alignment along the cross axis. */
typedef enum {
    AROMA_ALIGN_START,   /**< Align to start of cross axis. */
    AROMA_ALIGN_CENTER,  /**< Align to center of cross axis. */
    AROMA_ALIGN_END,     /**< Align to end of cross axis. */
    AROMA_ALIGN_STRETCH  /**< Stretch to fill cross axis. */
} AromaAlignItems;

/**
 * @brief Layout properties for a node.
 * 
 * Contains both "self" layout properties (how this node is placed) and
 * "container" properties (how this node places its children).
 */
typedef struct {
    // Self Layout
    AromaLayoutType type;   /**< Positioning strategy. */
    int left;              /**< Left offset/anchor. */
    int top;               /**< Top offset/anchor. */
    int right;             /**< Right offset/anchor. */
    int bottom;            /**< Bottom offset/anchor. */
    float width_percent;   /**< Width as percentage of parent. */
    float height_percent;  /**< Height as percentage of parent. */
    
    // Flex Item props
    float flex_grow;       /**< Flex grow factor. */
    float flex_shrink;     /**< Flex shrink factor. */
    int flex_basis;        /**< Flex basis size. */

    // Container Layout
    AromaLayoutMode mode;  /**< Layout strategy for children. */
    AromaFlexDirection flex_direction; /**< Row or Column. */
    AromaJustifyContent justify_content; /**< Main axis alignment. */
    AromaAlignItems align_items;         /**< Cross axis alignment. */
    int gap;                            /**< Gap between items. */
    
    // Grid props
    int grid_cols;         /**< Number of grid columns. */
    int grid_rows;         /**< Number of grid rows. */
    int grid_row_gap;      /**< Vertical gap in grid. */
    int grid_col_gap;      /**< Horizontal gap in grid. */
} AromaLayout;

/**
 * @brief Base node structure for the scene graph.
 */
struct AromaNode
{
    AromaNodeType node_type;    /**< Type of node. */
    uint64_t node_id;           /**< Unique ID. */
    int32_t z_index;            /**< Drawing order (higher is on top). */
    AromaNode* parent_node;     /**< Pointer to parent. */
    AromaNode* child_nodes[AROMA_MAX_CHILD_NODES]; /**< Array of children. */
    void *node_widget_ptr;      /**< Pointer to specific widget data struct. */
    AromaNodeDrawFn draw_cb;    /**< Custom drawing callback. */
    uint64_t child_count;       /**< Current number of children. */
    bool is_dirty;              /**< True if node itself needs redrawing. */
    bool subtree_dirty;         /**< True if any descendant needs redrawing. */
    bool is_hidden;             /**< True if node is strictly invisible. */
    bool propagate_dirty;       /**< True if dirty state propagates subtree_dirty up. */
    uint64_t dirty_frame;       /**< Frame number when node was last invalidated. */
    AromaLayout layout;         /**< Layout configuration. */
};

/** @brief Helper macro to cast a node's user pointer to a specific type. */
#define AROMA_NODE_AS(node, Type) ((Type*)((node) ? (node)->node_widget_ptr : NULL))

/** @internal Initialize the node system (called internally). */
void __node_system_init(void);
/** @internal Destroy the node system. */
void __node_system_destroy(void);

/**
 * @internal Create a new node.
 * Uses internal allocators. Use widget-specific creation functions instead.
 */
AromaNode* __create_node(AromaNodeType node_type, AromaNode* parent_node, void *node_widget_ptr);

/** @internal Add a child node (internal helper). */
AromaNode* __add_child_node(AromaNodeType node_type, AromaNode* parent_node, void *node_widget_ptr);

/** @internal Remove a child node by ID. */
AromaNode* __remove_child_node(AromaNode* parent_node, uint64_t node_id);

/** @internal Destroy a single node. */
void __destroy_node(AromaNode* node);

/** @internal Destroy a node and all its descendants. */
void __destroy_node_tree(AromaNode* root_node);

/** @internal Find a node by ID recursively. */
AromaNode* __find_node_by_id(AromaNode* root, uint64_t node_id);

/** @internal Generate a new unique Node ID. */
uint64_t __generate_node_id(void);
/** @internal Reset ID counter. */
void __reset_node_id_counter(void);

// Layout Setters

/**
 * @brief Configure node to use anchor layout.
 * @param node Target node.
 * @param left Left offset.
 * @param top Top offset.
 * @param right Right offset.
 * @param bottom Bottom offset.
 */
void aroma_node_set_layout_anchor(AromaNode* node, int left, int top, int right, int bottom);

/**
 * @brief Configure node to fill its parent.
 * @param node Target node.
 */
void aroma_node_set_layout_fill(AromaNode* node);

/**
 * @brief Configure node to center itself in parent.
 * @param node Target node.
 */
void aroma_node_set_layout_center(AromaNode* node);

// Flexbox & Grid helpers

/**
 * @brief Set the layout mode for valid children.
 * @param node Target container node.
 * @param mode Fhe mode (FLEX, GRID, NONE).
 */
void aroma_node_set_layout_mode(AromaNode* node, AromaLayoutMode mode);

/**
 * @brief Set flexbox direction.
 * @param node Target container node.
 * @param dir ROW or COLUMN.
 */
void aroma_node_set_flex_direction(AromaNode* node, AromaFlexDirection dir);

/**
 * @brief Set main axis alignment.
 * @param node Target container node.
 * @param justify Alignment strategy.
 */
void aroma_node_set_justify_content(AromaNode* node, AromaJustifyContent justify);

/**
 * @brief Set cross axis alignment.
 * @param node Target container node.
 * @param align Alignment strategy.
 */
void aroma_node_set_align_items(AromaNode* node, AromaAlignItems align);

/**
 * @brief Set flex grow factor for this node (as a child).
 * @param node Target node.
 * @param grow Grow factor.
 */
void aroma_node_set_flex_grow(AromaNode* node, float grow);

/**
 * @brief Set gap between children.
 * @param node Target container node.
 * @param gap Gap in pixels.
 */
void aroma_node_set_gap(AromaNode* node, int gap);

/**
 * @brief Set number of grid columns.
 * @param node Target container node.
 * @param cols Number of columns.
 */
void aroma_node_set_grid_cols(AromaNode* node, int cols);

/**
 * @brief Set number of grid rows.
 * @param node Target container node.
 * @param rows Number of rows.
 */
void aroma_node_set_grid_rows(AromaNode* node, int rows);

/**
 * @brief Update layout calculations for the tree.
 * @param root Root of subtree to update.
 * @param parent_x Parent absolute X.
 * @param parent_y Parent absolute Y.
 * @param parent_width Parent width.
 * @param parent_height Parent height.
 */
void aroma_node_update_layout(AromaNode* root, int parent_x, int parent_y, int parent_width, int parent_height);

/** @internal Get current ID counter value. */
uint64_t __get_current_node_id_counter(void);

/** @internal Print tree structure to log. */
void __print_node_tree(AromaNode* root_node);
/** @internal Print node info to log. */
void __print_node_info(AromaNode* node);

/**
 * @brief Set the Z-index (draw order).
 * @param node Target node.
 * @param z_index New Z-index.
 */
void aroma_node_set_z_index(AromaNode* node, int32_t z_index);

/**
 * @brief Get the Z-index.
 * @param node Target node.
 * @return Z-index.
 */
int32_t aroma_node_get_z_index(AromaNode* node);

/**
 * @brief Get the root window node for a given node.
 * @param node Any node in the hierarchy.
 * @return The root window node, or NULL if not found.
 */
AromaNode* aroma_node_get_window(AromaNode* node);

/**
 * @brief Mark a node as needing redraw.
 * @param node Target node.
 */
void aroma_node_invalidate(AromaNode* node);

/**
 * @brief Mark an entire subtree as needing redraw.
 * @param root Root of subtree.
 */
void aroma_node_invalidate_tree(AromaNode* root);

/**
 * @brief Check if node is marked dirty.
 * @param node Target node.
 * @return true if dirty.
 */
bool aroma_node_is_dirty(AromaNode* node);

/**
 * @brief Mark node as clean (no redraw needed).
 * @param node Target node.
 */
void aroma_node_mark_clean(AromaNode* node);

/**
 * @brief Set the custom draw callback.
 * @param node Target node.
 * @param draw_cb Callback function.
 */
void aroma_node_set_draw_cb(AromaNode* node, AromaNodeDrawFn draw_cb);

/**
 * @brief Get the current draw callback.
 * @param node Target node.
 * @return Callback function pointer.
 */
AromaNodeDrawFn aroma_node_get_draw_cb(AromaNode* node);

/**
 * @brief Set node visibility.
 * @param node Target node.
 * @param hidden True to hide, false to show.
 */
void aroma_node_set_hidden(AromaNode* node, bool hidden);

/**
 * @brief Check if node is hidden.
 * @param node Target node.
 * @return true if hidden.
 */
bool aroma_node_is_hidden(AromaNode* node);

/** @internal Initialize dirty list tracker. */
void aroma_dirty_list_init(void);
/** @internal Clear dirty list and reset subtree_dirty flags. */
void aroma_dirty_list_clear(void);
/** @internal Get list of dirty nodes. */
AromaNode** aroma_dirty_list_get(size_t* count);
/** @internal Add node to dirty list. */
void aroma_dirty_list_add(AromaNode* node);
/** @internal Check if any node is dirty (O(1)). */
bool aroma_dirty_list_has_entries(void);

/** @brief Get the current global frame number. */
uint64_t aroma_frame_number(void);
/** @internal Increment the global frame counter. */
void aroma_frame_advance(void);
#ifdef __cplusplus
}
#endif
#endif

