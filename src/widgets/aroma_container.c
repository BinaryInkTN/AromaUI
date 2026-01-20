#include "widgets/aroma_container.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_node.h"
#include <string.h>

typedef struct AromaContainer {
    AromaRect rect;
} AromaContainer;

AromaNode* aroma_container_create(AromaNode* parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid container parameters");
        return NULL;
    }

    AromaContainer* container = (AromaContainer*)aroma_widget_alloc(sizeof(AromaContainer));
    if (!container) {
        LOG_ERROR("Failed to allocate memory for container");
        return NULL;
    }

    memset(container, 0, sizeof(AromaContainer));
    container->rect.x = x;
    container->rect.y = y;
    container->rect.width = width;
    container->rect.height = height;

    AromaNode* node = __add_child_node(NODE_TYPE_CONTAINER, parent, container);
    if (!node) {
        aroma_widget_free(container);
        LOG_ERROR("Failed to create container node");
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_container_draw);
    aroma_node_invalidate(node);

    return node;
}

void aroma_container_set_rect(AromaNode* container_node, int x, int y, int width, int height)
{
    AromaContainer* container = aroma_container_get(container_node);
    if (!container) return;

    container->rect.x = x;
    container->rect.y = y;
    container->rect.width = width;
    container->rect.height = height;
    aroma_node_invalidate(container_node);
}

AromaRect aroma_container_get_rect(AromaNode* container_node)
{
    AromaContainer* container = aroma_container_get(container_node);
    if (!container) {
        AromaRect empty = {0};
        return empty;
    }
    return container->rect;
}

void aroma_container_draw(AromaNode* container_node, size_t window_id)
{
    (void)container_node;
    (void)window_id;
}

void aroma_container_destroy(AromaNode* container_node)
{
    if (!container_node) return;
    if (container_node->node_widget_ptr) {
        aroma_widget_free(container_node->node_widget_ptr);
        container_node->node_widget_ptr = NULL;
    }
    __destroy_node(container_node);
}
