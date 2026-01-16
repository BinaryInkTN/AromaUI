#include "aroma_node.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include <stdatomic.h>
#include <string.h>

static atomic_uint_fast64_t global_node_id_counter = 1; 

uint64_t __generate_node_id(void) {
    return atomic_fetch_add(&global_node_id_counter, 1);
}

void __reset_node_id_counter(void) {
    atomic_store(&global_node_id_counter, 1);
}

uint64_t __get_current_node_id_counter(void) {
    return atomic_load(&global_node_id_counter);
}

void  __node_system_init(void) {
    aroma_memory_system_init();
    __reset_node_id_counter();
    LOG_INFO("Node system initialized with multi-cache memory system.");
}

void __node_system_destroy(void) {
    __reset_node_id_counter();
    LOG_INFO("Node system destroyed.");
}

AromaNode* __create_node(AromaNodeType node_type, AromaNode* parent_node, void* node_widget_ptr) {
    if (node_type == NODE_TYPE_ROOT && parent_node != NULL) {
        LOG_ERROR("Root node cannot have a parent.");
        return NULL;
    }

    if (node_type != NODE_TYPE_ROOT && (!parent_node || !node_widget_ptr)) {
        LOG_ERROR("Invalid parameters to create node.");
        return NULL;
    }

    if (node_type > NODE_TYPE_WIDGET) {
        LOG_ERROR("Invalid node type: %d", node_type);
        return NULL;
    }

    if (parent_node && parent_node->child_count >= AROMA_MAX_CHILD_NODES) {
        LOG_WARNING("Parent node has reached maximum child nodes (%d).", AROMA_MAX_CHILD_NODES);
        return NULL;
    }

    AromaNode* new_node = (AromaNode*)__slab_pool_alloc(&global_memory_system.node_pool);
    if (!new_node) {
        LOG_CRITICAL("Failed to allocate memory for new node.");
        return NULL;
    }

    memset(new_node, 0, sizeof(AromaNode));
    new_node->node_id = __generate_node_id();
    new_node->node_type = node_type;
    new_node->parent_node = parent_node;
    new_node->node_widget_ptr = node_widget_ptr;
    new_node->child_count = 0;

    for (uint64_t i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        new_node->child_nodes[i] = NULL;
    }

    LOG_INFO("Created node ID: %llu, type: %d", new_node->node_id, node_type);
    return new_node;
}

AromaNode* __add_child_node(AromaNodeType node_type, AromaNode* parent_node, void* node_widget_ptr) {
    if (!parent_node) {
        LOG_ERROR("Parent node is NULL.");
        return NULL;
    }

    if (parent_node->child_count >= AROMA_MAX_CHILD_NODES) {
        LOG_WARNING("Parent node has reached maximum child nodes.");
        return NULL;
    }

    AromaNode* new_node = __create_node(node_type, parent_node, node_widget_ptr);
    if (!new_node) {
        LOG_ERROR("Failed to create child node.");
        return NULL;
    }

    parent_node->child_nodes[parent_node->child_count++] = new_node;
    LOG_INFO("Added child node ID: %llu to parent ID: %llu", 
              new_node->node_id, parent_node->node_id);

    return new_node;
}

AromaNode* __remove_child_node(AromaNode* parent_node, uint64_t node_id) {
    if (!parent_node) {
        LOG_ERROR("Parent node is NULL.");
        return NULL;
    }

    for (uint64_t i = 0; i < parent_node->child_count; ++i) {
        if (parent_node->child_nodes[i] && parent_node->child_nodes[i]->node_id == node_id) {
            AromaNode* removed_node = parent_node->child_nodes[i];

            for (uint64_t j = i; j < parent_node->child_count - 1; ++j) {
                parent_node->child_nodes[j] = parent_node->child_nodes[j + 1];
            }

            parent_node->child_nodes[parent_node->child_count - 1] = NULL;
            parent_node->child_count--;

            LOG_INFO("Removed child node ID: %llu from parent ID: %llu", 
                      node_id, parent_node->node_id);
            return removed_node;
        }
    }

    LOG_WARNING("Child node with ID %llu not found in parent ID %llu.", 
                node_id, parent_node->node_id);
    return NULL;
}

void __destroy_node(AromaNode* node) {
    if (!node) {
        LOG_WARNING("Attempted to destroy NULL node.");
        return;
    }

    for (uint64_t i = 0; i < node->child_count; i++) {
        if (node->child_nodes[i]) {
            __destroy_node(node->child_nodes[i]);
        }
    }

    if (node->node_widget_ptr) {
        aroma_widget_free(node->node_widget_ptr);
    }

    __slab_pool_free(&global_memory_system.node_pool, node);

    LOG_INFO("Destroyed node ID: %llu", node->node_id);
}

void __destroy_node_tree(AromaNode* root_node) {
    if (!root_node) {
        LOG_WARNING("Attempted to destroy NULL root node.");
        return;
    }

    __destroy_node(root_node);
    LOG_INFO("Destroyed entire node tree");
}

AromaNode* __find_node_by_id(AromaNode* root, uint64_t node_id) {
    if (!root) return NULL;

    if (root->node_id == node_id) {
        return root;
    }

    for (uint64_t i = 0; i < root->child_count; i++) {
        AromaNode* found = __find_node_by_id(root->child_nodes[i], node_id);
        if (found) return found;
    }

    return NULL;
}