#ifndef AROMA_CONTAINER_H
#define AROMA_CONTAINER_H

#include "aroma_common.h"
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaContainer AromaContainer;

AromaNode* aroma_container_create(AromaNode* parent, int x, int y, int width, int height);
void aroma_container_set_rect(AromaNode* container_node, int x, int y, int width, int height);
AromaRect aroma_container_get_rect(AromaNode* container_node);
void aroma_container_draw(AromaNode* container_node, size_t window_id);
void aroma_container_destroy(AromaNode* container_node);

static inline AromaContainer* aroma_container_get(AromaNode* node) {
    return AROMA_NODE_AS(node, AromaContainer);
}
#ifdef __cplusplus
}
#endif
#endif
