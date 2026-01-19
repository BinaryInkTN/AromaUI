#ifndef AROMA_SWITCH_H
#define AROMA_SWITCH_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaWindow AromaWindow;

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

void aroma_ui_request_redraw(void* user_data);

static inline AromaSwitch* aroma_ui_create_switch(AromaWindow* parent, const char* label,
                                     int x, int y, int width, int height) {
    if (!parent || !label) return NULL;

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* switch_widget = aroma_switch_create(parent_node, x, y, width, height, false);
    if (!switch_widget) {
        LOG_ERROR("Failed to create switch");
        return NULL;
    }
    aroma_switch_setup_events(switch_widget, aroma_ui_request_redraw, NULL);
    aroma_node_invalidate(switch_widget);

    LOG_INFO("Switch created: label='%s'", label);
    return (AromaSwitch*)switch_widget;
}

static inline void aroma_ui_switch_set_state(AromaSwitch* sw, bool is_on) {
    if (!sw) return;
    aroma_switch_set_state((AromaNode*)sw, is_on);
    aroma_node_invalidate((AromaNode*)sw);
}

static inline bool aroma_ui_switch_get_state(AromaSwitch* sw) {
    if (!sw) return false;
    return aroma_switch_get_state((AromaNode*)sw);
}

static inline void aroma_ui_on_switch_change(AromaSwitch* sw,
                                bool (*handler)(AromaSwitch*, bool, void*),
                                void* user_data) {
    if (!sw) return;
    aroma_switch_set_on_change((AromaNode*)sw, (bool (*)(AromaNode*, void*))handler, user_data);
}

static inline void aroma_ui_destroy_switch(AromaSwitch* sw) {
    if (!sw) return;
    aroma_switch_destroy((AromaNode*)sw);
}

#endif

