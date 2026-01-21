/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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
