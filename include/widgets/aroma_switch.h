#ifndef AROMA_SWITCH_H
#define AROMA_SWITCH_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaSwitch
{
    AromaRect rect;
    bool state;  

    uint32_t color_on;
    uint32_t color_off;
    bool is_hovered;
    bool (*on_change)(AromaNode* node, void* user_data);
    void* user_data;
} AromaSwitch;

AromaNode* aroma_switch_create(AromaNode* parent, int x, int y, int width, int height, bool initial_state);

void aroma_switch_set_state(AromaNode* node, bool state);

bool aroma_switch_get_state(AromaNode* node);

void aroma_switch_set_on_change(AromaNode* node, bool (*callback)(AromaNode*, void*), void* user_data);

void aroma_switch_draw(AromaNode* node, size_t window_id);

void aroma_switch_destroy(AromaNode* node);

bool aroma_switch_setup_events(AromaNode* switch_node, void (*on_redraw_callback)(void*), void* user_data);

#endif

