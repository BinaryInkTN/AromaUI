#ifndef AROMA_DROPDOWN_H
#define AROMA_DROPDOWN_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_logger.h"

#define AROMA_DROPDOWN_MAX_OPTIONS 32
#define AROMA_DROPDOWN_OPTION_MAX 128

typedef struct AromaDropdown
{
    AromaRect rect;
    char** options;
    int option_count;
    int selected_index;
    int hover_index;
    bool is_expanded;
    bool is_hovered;
    void (*on_selection_changed)(int index, const char* option, void* user_data);
    void* user_data;
    AromaFont* font;
    uint32_t text_color;
    uint32_t list_bg_color;
    uint32_t hover_bg_color;
    uint32_t selected_bg_color;
    uint32_t border_color;
    float corner_radius;
    struct {
        struct AromaNode* node;
        void (*handler)(struct AromaDropdown*, const char*, int, void*);
        void* user_data;
    } bridge;
} AromaDropdown;

typedef struct AromaWindow AromaWindow;

AromaNode* aroma_dropdown_create(AromaNode* parent, int x, int y, int width, int height);

void aroma_dropdown_add_option(AromaNode* dropdown_node, const char* option);

void aroma_dropdown_set_on_change(AromaNode* dropdown_node, 
    void (*on_change)(int index, const char* option, void* user_data), 
    void* user_data);

void aroma_dropdown_setup_events(AromaNode* dropdown_node, void (*on_redraw_callback)(void*), void* user_data);

void aroma_dropdown_draw(AromaNode* dropdown_node, size_t window_id);
void aroma_dropdown_render_overlays(size_t window_id);
bool aroma_dropdown_overlay_hit_test(int x, int y, AromaNode** out_node);
void aroma_dropdown_set_font(AromaNode* dropdown_node, AromaFont* font);
void aroma_dropdown_set_text_color(AromaNode* dropdown_node, uint32_t text_color);

void aroma_dropdown_destroy(AromaNode* dropdown_node);

void aroma_ui_request_redraw(void* user_data);

static inline void __ui_dropdown_on_change_bridge(int index, const char* option, void* data) {
    struct AromaDropdown* dd = (struct AromaDropdown*)data;
    if (!dd || !dd->bridge.handler || !dd->bridge.node) return;
    dd->bridge.handler((AromaDropdown*)dd->bridge.node, option, index, dd->bridge.user_data);
}

static inline AromaDropdown* aroma_ui_create_dropdown(AromaWindow* parent,
                                          int x, int y, int width, int height) {
    if (!parent) return NULL;

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* dropdown = aroma_dropdown_create(parent_node, x, y, width, height);
    if (!dropdown) {
        LOG_ERROR("Failed to create dropdown");
        return NULL;
    }

    aroma_dropdown_setup_events(dropdown, aroma_ui_request_redraw, NULL);
    aroma_node_invalidate(dropdown);

    LOG_INFO("Dropdown created");
    return (AromaDropdown*)dropdown;
}

static inline void aroma_ui_dropdown_add_option(AromaDropdown* dropdown, const char* option) {
    if (!dropdown || !option) return;

    AromaNode* dropdown_node = (AromaNode*)dropdown;
    aroma_dropdown_add_option(dropdown_node, option);
    aroma_node_invalidate(dropdown_node);
}

static inline void aroma_ui_on_dropdown_change(AromaDropdown* dropdown,
                                  void (*handler)(AromaDropdown*, const char*, int, void*),
                                  void* user_data) {
    if (!dropdown) return;

    AromaNode* dropdown_node = (AromaNode*)dropdown;
    struct AromaDropdown* dd = (struct AromaDropdown*)dropdown_node->node_widget_ptr;
    if (!dd) return;

    dd->bridge.node = dropdown_node;
    dd->bridge.handler = handler;
    dd->bridge.user_data = user_data;

    aroma_dropdown_set_on_change(dropdown_node, __ui_dropdown_on_change_bridge, dd);
}

static inline void aroma_ui_destroy_dropdown(AromaDropdown* dropdown) {
    if (!dropdown) return;
    aroma_dropdown_destroy((AromaNode*)dropdown);
}

static inline void aroma_ui_render_dropdown_overlays(size_t window_id) {
    aroma_dropdown_render_overlays(window_id);
}

static inline void aroma_ui_dropdown_set_font(AromaDropdown* dropdown, AromaFont* font) {
    if (!dropdown) return;
    aroma_dropdown_set_font((AromaNode*)dropdown, font);
}

static inline void aroma_ui_dropdown_set_text_color(AromaDropdown* dropdown, uint32_t color) {
    if (!dropdown) return;
    aroma_dropdown_set_text_color((AromaNode*)dropdown, color);
}

#endif
