#ifndef AROMA_BUTTON_H
#define AROMA_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_style.h"
#include <stdbool.h>
#include <string.h>

typedef struct AromaWindow AromaWindow;

#define AROMA_BUTTON_LABEL_MAX 64

typedef enum AromaButtonState {
    BUTTON_STATE_IDLE,
    BUTTON_STATE_HOVER,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_RELEASED
} AromaButtonState;
typedef struct AromaButton {
    AromaRect rect;
    char label[AROMA_BUTTON_LABEL_MAX];
    AromaButtonState state;

    uint32_t idle_color;
    uint32_t hover_color;
    uint32_t pressed_color;
    uint32_t text_color;
    bool use_theme_colors;
    float corner_radius;
    uint32_t shadow_color;
    float text_scale;
    float text_width;
    float text_x;
    float text_y;
    float line_height;

    bool (*on_click)(AromaNode*, void*);
    bool (*on_hover)(AromaNode*, void*);
    void* user_data;

    AromaFont* font;
    AromaRect text_bounds;
    bool layout_dirty;
} AromaButton;

AromaNode* aroma_button_create(AromaNode* parent, const char* label, int x, int y, int width, int height);

void aroma_button_set_on_click(AromaNode* button_node, bool (*on_click)(AromaNode*, void*), void* user_data);

void aroma_button_set_on_hover(AromaNode* button_node, bool (*on_hover)(AromaNode*, void*), void* user_data);

void aroma_button_set_colors(AromaNode* button_node, uint32_t idle_color, uint32_t hover_color, uint32_t pressed_color, uint32_t text_color);

void aroma_button_set_font(AromaNode* button_node, AromaFont* font);

void aroma_button_apply_style(AromaNode* button_node, const struct AromaStyle* style);

bool aroma_button_handle_mouse_event(AromaNode* button_node, int mouse_x, int mouse_y, bool is_clicked);

void aroma_button_draw(AromaNode* button_node, size_t window_id);

void aroma_button_destroy(AromaNode* button_node);

bool aroma_button_setup_events(AromaNode* button_node, void (*on_redraw_callback)(void*), void* user_data);

void aroma_ui_request_redraw(void* user_data);

static inline AromaButton* aroma_ui_create_button(AromaWindow* parent, const char* label,
                                     int x, int y, int width, int height) {
    if (!parent || !label) {
        LOG_ERROR("Invalid button parameters");
        return NULL;
    }

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* button = aroma_button_create(parent_node, label, x, y, width, height);

    if (!button) {
        LOG_ERROR("Failed to create button");
        return NULL;
    }

    aroma_button_setup_events(button, aroma_ui_request_redraw, NULL);
    aroma_node_invalidate(button);
    LOG_INFO("Button created: label='%s'", label);
    return (AromaButton*)button;
}

static inline void aroma_ui_on_button_click(AromaButton* button,
                               bool (*handler)(AromaButton*, void*),
                               void* user_data) {
    if (!button) return;

    AromaNode* button_node = (AromaNode*)button;
    aroma_button_set_on_click(button_node, (bool (*)(AromaNode*, void*))handler, user_data);
    LOG_INFO("Button click handler registered");
}

static inline void aroma_ui_button_set_label(AromaButton* button, const char* label) {
    if (!button || !label) return;

    AromaNode* button_node = (AromaNode*)button;
    struct AromaButton* button_data = (struct AromaButton*)button_node->node_widget_ptr;
    if (button_data) {
        strncpy(button_data->label, label, AROMA_BUTTON_LABEL_MAX - 1);
        button_data->label[AROMA_BUTTON_LABEL_MAX - 1] = '\0';
        aroma_node_invalidate(button_node);
    }
}

static inline void aroma_ui_button_set_enabled(AromaButton* button, bool enabled) {
    if (!button) return;

    AromaNode* button_node = (AromaNode*)button;
    struct AromaButton* button_data = (struct AromaButton*)button_node->node_widget_ptr;
    if (button_data) {
        if (!enabled) {
            button_data->idle_color = aroma_color_rgb(200, 200, 200);
        }
        aroma_node_invalidate(button_node);
    }
}

static inline void aroma_ui_button_set_style(AromaButton* button, const AromaStyle* style) {
    if (!button || !style) return;

    AromaNode* button_node = (AromaNode*)button;
    struct AromaButton* button_data = (struct AromaButton*)button_node->node_widget_ptr;
    if (button_data) {
        button_data->idle_color = style->idle_color;
        button_data->hover_color = style->hover_color;
        button_data->pressed_color = style->active_color;
        button_data->text_color = style->text_color;
        aroma_node_invalidate(button_node);
    }
}

static inline void aroma_ui_destroy_button(AromaButton* button) {
    if (!button) return;
    aroma_button_destroy((AromaNode*)button);
}

#endif
