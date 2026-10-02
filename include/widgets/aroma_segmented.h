#ifndef AROMA_SEGMENTED_H
#define AROMA_SEGMENTED_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AROMA_SEGMENTED_MAX 8
#define AROMA_SEGMENT_LABEL_MAX 32

typedef struct  AromaSegmented AromaSegmented;

AromaNode* aroma_segmented_create(AromaNode* parent, int x, int y, int width, int height,
                                  const char** labels, int count);

void aroma_segmented_set_selected(AromaNode* seg_node, int index);
int aroma_segmented_get_selected(AromaNode* seg_node);

void aroma_segmented_set_on_change(AromaNode* seg_node,
                                   void (*callback)(AromaNode*, int, void*),
                                   void* user_data);

void aroma_segmented_set_font(AromaNode* seg_node, AromaFont* font);

bool aroma_segmented_setup_events(AromaNode* seg_node, void (*on_redraw_callback)(void*), void* user_data);

void aroma_segmented_draw(AromaNode* seg_node, size_t window_id);
void aroma_segmented_destroy(AromaNode* seg_node);
#ifdef __cplusplus
}
#endif
#endif
