#ifndef AROMA_SIDEBAR_H
#define AROMA_SIDEBAR_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

#define AROMA_SIDEBAR_MAX_ITEMS 16
#define AROMA_SIDEBAR_LABEL_MAX 32

typedef struct AromaSidebar AromaSidebar;

AromaNode* aroma_sidebar_create(AromaNode* parent, int x, int y, int width, int height,
                                const char** labels, int count);

void aroma_sidebar_set_selected(AromaNode* sidebar_node, int index);
int aroma_sidebar_get_selected(AromaNode* sidebar_node);

void aroma_sidebar_set_on_select(AromaNode* sidebar_node,
                                 void (*callback)(AromaNode*, int, void*),
                                 void* user_data);

void aroma_sidebar_set_font(AromaNode* sidebar_node, AromaFont* font);

void aroma_sidebar_set_content(AromaNode* sidebar_node, int index, AromaNode** content_nodes, int content_count);

bool aroma_sidebar_setup_events(AromaNode* sidebar_node, void (*on_redraw_callback)(void*), void* user_data);

void aroma_sidebar_draw(AromaNode* sidebar_node, size_t window_id);
void aroma_sidebar_destroy(AromaNode* sidebar_node);

#endif
