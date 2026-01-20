#ifndef AROMA_NODE_H
#define AROMA_NODE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
#endif