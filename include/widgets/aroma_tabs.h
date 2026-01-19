#ifndef AROMA_TABS_H
#define AROMA_TABS_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

#define AROMA_TABS_MAX 8
#define AROMA_TAB_LABEL_MAX 32

typedef struct AromaTabs AromaTabs;

AromaNode* aroma_tabs_create(AromaNode* parent, int x, int y, int width, int height,
                             const char** labels, int count);

void aroma_tabs_set_selected(AromaNode* tabs_node, int index);
int aroma_tabs_get_selected(AromaNode* tabs_node);

void aroma_tabs_set_on_change(AromaNode* tabs_node,
                              void (*callback)(AromaNode*, int, void*),
                              void* user_data);

void aroma_tabs_set_font(AromaNode* tabs_node, AromaFont* font);

void aroma_tabs_set_content(AromaNode* tabs_node, int index, AromaNode** content_nodes, int content_count);

bool aroma_tabs_setup_events(AromaNode* tabs_node, void (*on_redraw_callback)(void*), void* user_data);

void aroma_tabs_draw(AromaNode* tabs_node, size_t window_id);
void aroma_tabs_destroy(AromaNode* tabs_node);

#endif
