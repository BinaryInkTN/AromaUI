#ifndef AROMA_SWITCH_H
#define AROMA_SWITCH_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaWindow AromaWindow;

typedef struct AromaSwitch
{
    AromaRect rect;
    bool state;

    uint32_t color_on;
    uint32_t color_off;
    bool is_hovered;
    float track_radius;
    int toggle_size;
    float toggle_radius;
    uint32_t border_color;
    int toggle_x;

    bool use_theme_colors;

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

void aroma_ui_request_redraw(void* user_data);

#ifdef __cplusplus
}
#endif
#endif
