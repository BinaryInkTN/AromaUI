#include "core/aroma_node.h"
#include "core/aroma_logger.h"
#include "core/aroma_event.h"
#include "core/aroma_slab_alloc.h"
#include "aroma_ui.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_uint_fast64_t s_node_id_counter = 1;
static uint64_t s_frame_number = 0;

static AromaNode *s_dirty_nodes[AROMA_MAX_DIRTY_NODES];
static size_t s_dirty_count = 0;

static uint64_t next_node_id(void)
{
    return atomic_fetch_add(&s_node_id_counter, 1);
}

static AromaNode *alloc_node(void)
{
    AromaNode *n = (AromaNode *)__slab_pool_alloc(&global_memory_system.node_pool);
    if (n)
        memset(n, 0, sizeof(AromaNode));

    return n;
}

static void free_node(AromaNode *node)
{
    __slab_pool_free(&global_memory_system.node_pool, node);
}

static bool ensure_child_capacity(AromaNode *parent, uint64_t needed)
{
    if (!parent)
        return false;
    if (needed <= parent->child_capacity)
        return true;

    if (needed > AROMA_MAX_CHILD_NODES)
    {
        LOG_WARNING("Parent %" PRIu64 " requested capacity %" PRIu64 " exceeds max (%d).",
                    parent->node_id, needed, AROMA_MAX_CHILD_NODES);
        return false;
    }

    uint64_t new_cap = parent->child_capacity == 0
                           ? AROMA_CHILD_INITIAL_CAPACITY
                           : parent->child_capacity * 2;
    if (new_cap < needed)
        new_cap = needed;
    if (new_cap > AROMA_MAX_CHILD_NODES)
        new_cap = AROMA_MAX_CHILD_NODES;

    AromaNode **new_arr = (AromaNode **)realloc(parent->child_nodes, new_cap * sizeof(AromaNode *));
    if (!new_arr)
    {
        LOG_CRITICAL("Failed to grow child_nodes for parent %" PRIu64 " to capacity %" PRIu64 ".",
                     parent->node_id, new_cap);
        return false;
    }

    memset(new_arr + parent->child_capacity, 0,
           (new_cap - parent->child_capacity) * sizeof(AromaNode *));

    parent->child_nodes = new_arr;
    parent->child_capacity = new_cap;
    return true;
}

static void dirty_list_remove(AromaNode *node)
{
    for (size_t i = 0; i < s_dirty_count; i++)
    {
        if (s_dirty_nodes[i] == node)
        {
            s_dirty_nodes[i] = s_dirty_nodes[--s_dirty_count];
            s_dirty_nodes[s_dirty_count] = NULL;
            return;
        }
    }
}

static const char *node_type_str(AromaNodeType t)
{
    static const char *const names[] = {"ROOT", "CONTAINER", "WIDGET"};
    return (unsigned)t < 3 ? names[t] : "UNKNOWN";
}

static void print_tree_r(const AromaNode *node, int depth)
{
    if (!node)
        return;
    for (int i = 0; i < depth; i++)
        fputs("  ", stdout);
    printf("├─ [ID:%" PRIu64 " | %s | children:%" PRIu64 "]\n",
           node->node_id, node_type_str(node->node_type), node->child_count);
    for (uint64_t i = 0; i < node->child_count; i++)
        print_tree_r(node->child_nodes[i], depth + 1);
}

void __node_system_init(void)
{
    aroma_memory_system_init();
    atomic_store(&s_node_id_counter, 1);
    aroma_dirty_list_init();
    LOG_INFO("Node system initialised.");
}

void __node_system_destroy(void)
{
    atomic_store(&s_node_id_counter, 1);
    LOG_INFO("Node system destroyed.");
}

void __reset_node_id_counter(void) { atomic_store(&s_node_id_counter, 1); }
uint64_t __get_current_node_id_counter(void) { return atomic_load(&s_node_id_counter); }
uint64_t __generate_node_id(void) { return next_node_id(); }

AromaNode *__create_node(AromaNodeType node_type,
                         AromaNode *parent_node,
                         void *node_widget_ptr)
{
    if (node_type > NODE_TYPE_WIDGET)
    {
        LOG_ERROR("Invalid node type: %d", node_type);
        return NULL;
    }

    if (node_type == NODE_TYPE_ROOT && parent_node)
    {
        LOG_ERROR("Root node cannot have a parent.");
        return NULL;
    }
    if (node_type != NODE_TYPE_ROOT && !parent_node)
    {
        LOG_ERROR("Non-root node requires a parent.");
        return NULL;
    }

    if (node_type != NODE_TYPE_ROOT && !node_widget_ptr)
    {
        LOG_ERROR("Non-root node requires a widget pointer.");
        return NULL;
    }

    if (parent_node && parent_node->child_count >= AROMA_MAX_CHILD_NODES)
    {
        LOG_WARNING("Parent %" PRIu64 " is at child capacity (%d).",
                    parent_node->node_id, AROMA_MAX_CHILD_NODES);
        return NULL;
    }

    AromaNode *n = alloc_node();
    if (!n)
    {
        LOG_CRITICAL("Out of memory allocating AromaNode.");
        return NULL;
    }

    n->node_id = next_node_id();
    n->node_type = node_type;
    n->parent_node = parent_node;
    n->node_widget_ptr = node_widget_ptr;
    n->opacity = 1.0f;
    n->propagate_dirty = true;

    LOG_INFO("Created node ID:%" PRIu64 " type:%s", n->node_id, node_type_str(node_type));
    return n;
}

AromaNode *__add_child_node(AromaNodeType node_type,
                            AromaNode *parent_node,
                            void *node_widget_ptr)
{
    if (!parent_node)
    {
        LOG_ERROR("__add_child_node: parent is NULL.");
        return NULL;
    }

    if (!ensure_child_capacity(parent_node, parent_node->child_count + 1))
    {
        LOG_ERROR("Failed to grow child array for parent %" PRIu64 ".", parent_node->node_id);
        return NULL;
    }

    AromaNode *child = __create_node(node_type, parent_node, node_widget_ptr);
    if (!child)
        return NULL;

    parent_node->child_nodes[parent_node->child_count++] = child;
    LOG_INFO("Node %" PRIu64 " added as child of %" PRIu64 ".",
             child->node_id, parent_node->node_id);
    return child;
}

AromaNode *__remove_child_node(AromaNode *parent_node, uint64_t node_id)
{
    if (!parent_node)
    {
        LOG_ERROR("__remove_child_node: parent is NULL.");
        return NULL;
    }

    for (uint64_t i = 0; i < parent_node->child_count; i++)
    {
        if (!parent_node->child_nodes[i])
            continue;
        if (parent_node->child_nodes[i]->node_id != node_id)
            continue;

        AromaNode *removed = parent_node->child_nodes[i];

        int64_t last = (int64_t)parent_node->child_count - 1;
        for (int64_t j = (int64_t)i; j < last; j++)
            parent_node->child_nodes[j] = parent_node->child_nodes[j + 1];

        parent_node->child_nodes[--parent_node->child_count] = NULL;

        LOG_INFO("Removed node %" PRIu64 " from parent %" PRIu64 ".",
                 node_id, parent_node->node_id);
        return removed;
    }

    LOG_WARNING("Node %" PRIu64 " not found in parent %" PRIu64 ".",
                node_id, parent_node->node_id);
    return NULL;
}

void __destroy_node(AromaNode *node)
{
    if (!node)
        return;

    if (node->destroy_cb)
    {
        void (*cb)(struct AromaNode *) = node->destroy_cb;
        node->destroy_cb = NULL;
        cb(node);
    }

    if (node->parent_node)
        __remove_child_node(node->parent_node, node->node_id);

    uint64_t count = node->child_count;
    for (uint64_t i = 0; i < count; i++)
    {
        AromaNode *child = node->child_nodes[i];
        if (!child)
            continue;
        child->parent_node = NULL;
        __destroy_node(child);
    }

    if (node->node_widget_ptr)
        aroma_widget_free(node->node_widget_ptr);

    free(node->child_nodes);
    node->child_nodes = NULL;
    node->child_count = 0;
    node->child_capacity = 0;

    dirty_list_remove(node);

    uint64_t id = node->node_id;
    free_node(node);
    LOG_INFO("Destroyed node %" PRIu64 ".", id);
}

void __destroy_node_tree(AromaNode *root_node)
{
    if (!root_node)
        return;
    __destroy_node(root_node);
    LOG_INFO("Destroyed node tree.");
}

AromaNode *__find_node_by_id(AromaNode *root, uint64_t node_id)
{
    if (!root)
        return NULL;
    if (root->node_id == node_id)
        return root;
    for (uint64_t i = 0; i < root->child_count; i++)
    {
        AromaNode *found = __find_node_by_id(root->child_nodes[i], node_id);
        if (found)
            return found;
    }
    return NULL;
}

void __print_node_info(AromaNode *node)
{
    if (!node)
    {
        puts("[NULL NODE]");
        return;
    }
    printf("ID:%" PRIu64 " | %s | z:%d | children:%" PRIu64 " | widget:%p\n",
           node->node_id, node_type_str(node->node_type),
           node->z_index, node->child_count, (void *)node->node_widget_ptr);
}

void __print_node_tree(AromaNode *root_node)
{
    if (!root_node)
    {
        puts("[ERROR] root is NULL");
        return;
    }
    puts("\n========== AROMA SCENE GRAPH ==========");
    printf("Root ID: %" PRIu64 "\n\n", root_node->node_id);
    print_tree_r(root_node, 0);
    puts("=======================================\n");
}

void aroma_node_set_z_index(AromaNode *node, int32_t z)
{
    if (node)
        node->z_index = z;
}
int32_t aroma_node_get_z_index(AromaNode *node) { return node ? node->z_index : 0; }

AromaNode *aroma_node_get_window(AromaNode *node)
{
    if (!node)
        return NULL;
    while (node->parent_node)
        node = node->parent_node;
    return node;
}

void aroma_node_set_draw_cb(AromaNode *node, AromaNodeDrawFn cb)
{
    if (node)
        node->draw_cb = cb;
}
AromaNodeDrawFn aroma_node_get_draw_cb(AromaNode *node) { return node ? node->draw_cb : NULL; }

void aroma_node_set_hidden(AromaNode *node, bool hidden)
{
    if (!node || node->is_hidden == hidden)
        return;
    node->is_hidden = hidden;
    if (node->parent_node)
        aroma_node_invalidate(node->parent_node);
}

bool aroma_node_is_hidden(AromaNode *node) { return node ? node->is_hidden : true; }

void aroma_dirty_list_init(void)
{
    s_dirty_count = 0;
    s_frame_number = 0;
    memset(s_dirty_nodes, 0, sizeof(s_dirty_nodes));
}

bool aroma_dirty_list_add(AromaNode *node)
{
    if (!node)
        return false;
    if (s_dirty_count >= AROMA_MAX_DIRTY_NODES)
    {
        LOG_WARNING("Dirty list full — dropping node %" PRIu64 ".", node->node_id);
        return false;
    }
    s_dirty_nodes[s_dirty_count++] = node;
    return true;
}

void aroma_dirty_list_clear(void)
{
    for (size_t i = 0; i < s_dirty_count; i++)
    {
        AromaNode *n = s_dirty_nodes[i];
        if (!n)
            continue;

        n->is_dirty = false;
        n->subtree_dirty = false;

        AromaNode *p = n->parent_node;
        while (p)
        {
            if (!p->subtree_dirty)
                break;
            p->subtree_dirty = false;
            p = p->parent_node;
        }

        s_dirty_nodes[i] = NULL;
    }
    s_dirty_count = 0;
}

AromaNode **aroma_dirty_list_get(size_t *count)
{
    if (count)
        *count = s_dirty_count;
    return s_dirty_nodes;
}

bool aroma_dirty_list_has_entries(void) { return s_dirty_count > 0; }

void aroma_node_invalidate(AromaNode *node)
{
    if (!node || node->is_dirty)
        return;

    if (!aroma_dirty_list_add(node))
        return;

    node->is_dirty = true;
    node->dirty_frame = s_frame_number;

    if (!node->propagate_dirty)
        return;
    AromaNode *p = node->parent_node;
    while (p)
    {
        if (p->subtree_dirty)
            break;
        p->subtree_dirty = true;
        p = p->parent_node;
    }
}

void aroma_node_invalidate_tree(AromaNode *root)
{
    if (!root)
        return;
    aroma_node_invalidate(root);
    for (uint64_t i = 0; i < root->child_count; i++)
        aroma_node_invalidate_tree(root->child_nodes[i]);
}

bool aroma_node_is_dirty(AromaNode *node) { return node && node->is_dirty; }

void aroma_node_mark_clean(AromaNode *node)
{
    if (!node)
        return;
    node->is_dirty = false;
    node->dirty_frame = s_frame_number;
}

uint64_t aroma_frame_number(void) { return s_frame_number; }
void aroma_frame_advance(void) { s_frame_number++; }