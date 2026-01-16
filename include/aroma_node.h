#ifndef AROMA_NODE_H
#define AROMA_NODE_H
#include <stdint.h>

#define AROMA_MAX_CHILD_NODES 4
#define AROMA_NODE_ID_INVALID 0

typedef struct AromaNode AromaNode;

typedef enum AromaNodeType {
    NODE_TYPE_ROOT,
    NODE_TYPE_CONTAINER,
    NODE_TYPE_WIDGET
} AromaNodeType;

typedef struct AromaNode
{
    AromaNodeType node_type;           /* Type of the node */
    uint64_t node_id;                  /* Unique identifier for the node (Randomly generated) */
    AromaNode* parent_node;            /* Pointer to the parent node */
    AromaNode* child_nodes[AROMA_MAX_CHILD_NODES]; /* Pointer to an array of child nodes */
    void *node_widget_ptr;             /* Pointer to the widget associated with the node */
    uint64_t child_count;              /* Number of child nodes */
} AromaNode;


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
uint64_t __get_current_node_id_counter(void);
#endif