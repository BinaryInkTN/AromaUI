#include "aroma_node.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static atomic_uint_fast64_t global_node_id_counter = 1;

static AromaNode* g_dirty_nodes[AROMA_MAX_DIRTY_NODES];
static size_t g_dirty_count = 0;

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
    aroma_dirty_list_init();
    LOG_INFO("Node system initialized with multi-cache memory system.");
}

void __node_system_destroy(void) {
    __reset_node_id_counter();
    LOG_INFO("Node system destroyed.");
}

AromaNode* __create_node(AromaNodeType node_type, AromaNode* parent_node, void* node_widget_ptr) {
    fprintf(stderr, "DEBUG_node: __create_node called node_type=%d parent=%p widget=%p\n", node_type, (void*)parent_node, node_widget_ptr);

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

    fprintf(stderr, "DEBUG_node: about to alloc node\n");
    AromaNode* new_node = (AromaNode*)__slab_pool_alloc(&global_memory_system.node_pool);
    fprintf(stderr, "DEBUG_node: alloc returned %p\n", (void*)new_node);
    if (!new_node) {
        LOG_CRITICAL("Failed to allocate memory for new node.");
        return NULL;
    }

    memset(new_node, 0, sizeof(AromaNode));
    new_node->node_id = __generate_node_id();
    new_node->node_type = node_type;
    new_node->z_index = 0;
    new_node->parent_node = parent_node;
    new_node->node_widget_ptr = node_widget_ptr;
    new_node->child_count = 0;
    new_node->is_dirty = false;  
    new_node->is_hidden = false;
    new_node->propagate_dirty = true;

    for (uint64_t i = 0; i < AROMA_MAX_CHILD_NODES; i++) {
        new_node->child_nodes[i] = NULL;
    }

    LOG_INFO("Created node ID: %llu, type: %d", new_node->node_id, node_type);
    return new_node;
}

AromaNode* __add_child_node(AromaNodeType node_type, AromaNode* parent_node, void* node_widget_ptr) {
    fprintf(stderr, "DEBUG_node: __add_child_node called type=%d parent_node=%p node_widget_ptr=%p\n", node_type, (void*)parent_node, node_widget_ptr);
    fprintf(stderr, "DEBUG_node: parent_node address check: %p, is_null=%d\n", (void*)parent_node, parent_node == NULL);
    if (!parent_node) {
        fprintf(stderr, "DEBUG_node: parent_node is NULL, returning\n");
        LOG_ERROR("Parent node is NULL.");
        return NULL;
    }

    if (parent_node->child_count >= AROMA_MAX_CHILD_NODES) {
        LOG_WARNING("Parent node has reached maximum child nodes.");
        return NULL;
    }

    fprintf(stderr, "DEBUG_node: calling __create_node\n");
    AromaNode* new_node = __create_node(node_type, parent_node, node_widget_ptr);
    fprintf(stderr, "DEBUG_node: __create_node returned %p\n", (void*)new_node);
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

static const char* __node_type_to_string(AromaNodeType type) {
    switch (type) {
        case NODE_TYPE_ROOT:
            return "ROOT";
        case NODE_TYPE_CONTAINER:
            return "CONTAINER";
        case NODE_TYPE_WIDGET:
            return "WIDGET";
        default:
            return "UNKNOWN";
    }
}

void __print_node_info(AromaNode* node) {
    if (!node) {
        printf("[NULL NODE]\n");
        return;
    }
        printf("Node ID: %llu | Type: %s | z:%d | Children: %llu | Widget: %p\n",
           node->node_id,
           __node_type_to_string(node->node_type),
            node->z_index,
           node->child_count,
           node->node_widget_ptr);
}

static void __print_node_tree_recursive(AromaNode* node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    printf("├─ [ID: %llu | Type: %s | Children: %llu]\n",
           node->node_id,
           __node_type_to_string(node->node_type),
           node->child_count);

    for (uint64_t i = 0; i < node->child_count; i++) {
        if (node->child_nodes[i]) {
            __print_node_tree_recursive(node->child_nodes[i], depth + 1);
        }
    }
}

void __print_node_tree(AromaNode* root_node) {
    if (!root_node) {
        printf("[ERROR] Scene graph root is NULL\n");
        return;
    }

    printf("\n========== AROMA SCENE GRAPH TREE ==========\n");
    printf("Root Node ID: %llu\n", root_node->node_id);
    printf("\nHierarchy:\n");
    __print_node_tree_recursive(root_node, 0);
    printf("==========================================\n\n");
}

void aroma_node_set_z_index(AromaNode* node, int32_t z_index) {
    if (!node) return;
    node->z_index = z_index;
}

int32_t aroma_node_get_z_index(AromaNode* node) {
    if (!node) return 0;
    return node->z_index;
}

void aroma_node_invalidate(AromaNode* node) {
    if (!node || node->is_dirty) return;

    node->is_dirty = true;
    aroma_dirty_list_add(node);

    if (node->propagate_dirty && node->parent_node) {
        aroma_node_invalidate(node->parent_node);
    }
}

void aroma_node_invalidate_tree(AromaNode* root) {
    if (!root) return;

    aroma_node_invalidate(root);

    for (uint64_t i = 0; i < root->child_count; i++) {
        if (root->child_nodes[i]) {
            aroma_node_invalidate_tree(root->child_nodes[i]);
        }
    }
}

bool aroma_node_is_dirty(AromaNode* node) {
    return node ? node->is_dirty : false;
}

void aroma_node_mark_clean(AromaNode* node) {
    if (node) {
        node->is_dirty = false;
    }
}

void aroma_node_set_hidden(AromaNode* node, bool hidden) {
    if (!node) return;
    if (node->is_hidden != hidden) {
        node->is_hidden = hidden;
        if (node->parent_node) {
            aroma_node_invalidate(node->parent_node);
        }
    }
}

bool aroma_node_is_hidden(AromaNode* node) {
    return node ? node->is_hidden : true;
}

void aroma_dirty_list_init(void) {
    g_dirty_count = 0;
    memset(g_dirty_nodes, 0, sizeof(g_dirty_nodes));
}

void aroma_dirty_list_clear(void) {
    for (size_t i = 0; i < g_dirty_count; i++) {
        if (g_dirty_nodes[i]) {
            g_dirty_nodes[i]->is_dirty = false;
        }
    }
    g_dirty_count = 0;
}

AromaNode** aroma_dirty_list_get(size_t* count) {
    if (count) *count = g_dirty_count;
    return g_dirty_nodes;
}

void aroma_dirty_list_add(AromaNode* node) {
    if (!node || g_dirty_count >= AROMA_MAX_DIRTY_NODES) return;

    for (size_t i = 0; i < g_dirty_count; i++) {
        if (g_dirty_nodes[i] == node) {
            return;
        }
    }

    g_dirty_nodes[g_dirty_count++] = node;
}