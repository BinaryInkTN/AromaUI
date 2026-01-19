#ifndef AROMA_DEBUG_OVERLAY_H
#define AROMA_DEBUG_OVERLAY_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaDebugOverlay AromaDebugOverlay;

AromaNode* aroma_debug_overlay_create(AromaNode* parent, int x, int y);

void aroma_debug_overlay_set_font(AromaNode* overlay_node, AromaFont* font);
void aroma_debug_overlay_set_visible(AromaNode* overlay_node, bool visible);

void aroma_debug_overlay_draw(AromaNode* overlay_node, size_t window_id);
void aroma_debug_overlay_destroy(AromaNode* overlay_node);

#endif
