#ifndef AROMA_CONTAINER_H
#define AROMA_CONTAINER_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaContainer AromaContainer;

typedef enum {
    AROMA_SCROLL_VERTICAL   = 0x01,
    AROMA_SCROLL_HORIZONTAL = 0x02,
    AROMA_SCROLL_BOTH       = 0x03
} AromaScrollDirection;


AromaNode* aroma_container_create(AromaNode* parent, int x, int y, int width, int height);
void aroma_container_destroy(AromaNode* container_node);


void aroma_container_set_rect(AromaNode* container_node, int x, int y, int width, int height);
AromaRect aroma_container_get_rect(AromaNode* container_node);


void aroma_container_set_debug_bg(AromaNode* container_node, uint32_t color);


void aroma_container_set_scrollable(AromaNode* node, bool scrollable);

void aroma_container_set_content_size(AromaNode* node, int content_width, int content_height);

void aroma_container_set_scroll_direction(AromaNode* node, AromaScrollDirection direction);

void aroma_container_get_scroll(AromaNode* node, int* scroll_x, int* scroll_y);

void aroma_container_set_scroll(AromaNode* node, int scroll_x, int scroll_y);

void aroma_container_scroll_by(AromaNode* node, int dx, int dy);

void aroma_container_set_scroll_speed(AromaNode* node, float speed);

void aroma_container_show_scrollbar(AromaNode* node, bool show);

void aroma_container_set_scrollbar_color(AromaNode* node, uint32_t color);


void aroma_container_draw(AromaNode* container_node, size_t window_id);


bool aroma_container_is_scrollable(AromaNode* node);

void aroma_container_get_content_size(AromaNode* node, int* out_w, int* out_h);

void aroma_container_update_auto_content_size(AromaNode* node);

static inline AromaContainer* aroma_container_get(AromaNode* node) {
    if (!node || node->node_type != NODE_TYPE_CONTAINER) {
        return NULL;
    }
    return AROMA_NODE_AS(node, AromaContainer);
}
 
#ifdef __cplusplus
}
#endif
#endif
