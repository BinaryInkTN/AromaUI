#ifndef AROMA_UI_H
#define AROMA_UI_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "aroma_style.h"
#include "widgets/aroma_window.h"
#include "widgets/aroma_button.h"
#include "widgets/aroma_slider.h"
#include "widgets/aroma_switch.h"
#include "widgets/aroma_textbox.h"
#include "widgets/aroma_dropdown.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaNode AromaNode;
typedef struct AromaWindow AromaWindow;
typedef struct AromaButton AromaButton;
typedef struct AromaDropdown AromaDropdown;
typedef struct AromaSlider AromaSlider;
typedef struct AromaTextbox AromaTextbox;
typedef struct AromaSwitch AromaSwitch;
typedef struct AromaMenu AromaMenu;
typedef struct AromaFont AromaFont;

typedef struct {
    AromaWindow* window;
    AromaNode* root_node;
    size_t window_id;
    bool is_active;
    AromaFont* default_font;
} AromaWindowHandle;

#define AROMA_MAX_WINDOWS 16

extern bool g_ui_initialized;
extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
extern int g_window_count;
extern AromaNode* g_main_window;  

extern AromaNode* g_focused_node;
extern void aroma_graphics_load_font_for_window(size_t window_id, AromaFont* font);

static inline AromaNode* aroma_ui_get_focused_node(void) {
    return g_focused_node;
}

static inline void aroma_ui_set_focused_node(AromaNode* node) {
    g_focused_node = node;
}

static inline void aroma_ui_clear_focused_node(AromaNode* node) {
    if (g_focused_node == node) {
        g_focused_node = NULL;
    }
}

static inline void aroma_ui_request_redraw(void* user_data);
static inline bool aroma_ui_consume_redraw(void);

extern bool aroma_ui_init_impl(void);

static inline bool aroma_ui_init(void) {
    if (g_ui_initialized) {
        LOG_INFO("Aroma UI already initialized");
        return true;
    }
    return aroma_ui_init_impl();
}

static inline void aroma_ui_set_theme(const AromaTheme* theme) {
    if (theme) {
        aroma_theme_set_global(theme);
        LOG_INFO("Theme updated");
    }
}

static inline AromaTheme aroma_ui_get_theme(void) {
    return aroma_theme_get_global();
}

extern void aroma_ui_shutdown_impl(void);

static inline void aroma_ui_shutdown(void) {
    if (!g_ui_initialized) return;
    aroma_ui_shutdown_impl();
}

extern bool aroma_ui_is_running_impl(void);

static inline bool aroma_ui_is_running(void) {
    if (!g_ui_initialized) return false;
    return aroma_ui_is_running_impl();
}

static inline void aroma_ui_process_events(void) {
    if (!g_ui_initialized) return;
    aroma_event_process_queue();
}

extern void aroma_ui_render_impl(struct AromaWindow* window_data);
extern void aroma_ui_render_all_windows_impl(void);

static inline void aroma_ui_render(AromaWindow* window) {
    if (!g_ui_initialized || !window) return;
    AromaNode* window_node = (AromaNode*)window;
    if (!window_node || window_node->node_type != NODE_TYPE_ROOT) return;

    struct AromaWindow* window_data = (struct AromaWindow*)window_node->node_widget_ptr;
    if (!window_data) return;

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count == 0) return;

    aroma_ui_render_impl(window_data);
}

static inline void aroma_ui_render_all(void) {
    if (!g_ui_initialized) return;
    aroma_ui_render_all_windows_impl();
}

static inline AromaFont* aroma_ui_load_font(const char* path, int size_px) {
    if (!path || size_px <= 0) {
        LOG_ERROR("Invalid font parameters");
        return NULL;
    }

    AromaFont* font = aroma_font_create(path, size_px);
    if (font) {
        LOG_INFO("Font loaded: %s (size %d)", path, size_px);
    } else {
        LOG_WARNING("Failed to load font: %s", path);
    }
    return font;
}

static inline void aroma_ui_unload_font(AromaFont* font) {
    if (font) {
        aroma_font_destroy(font);
        LOG_INFO("Font unloaded");
    }
}

static inline void aroma_ui_prepare_font_for_window(size_t window_id, AromaFont* font) {
    if (!font) return;
    aroma_graphics_load_font_for_window(window_id, font);
}

extern AromaWindow* aroma_ui_create_window_impl(const char* title, int width, int height);

static inline AromaWindow* aroma_ui_create_window(const char* title, int width, int height) {
    if (!g_ui_initialized) {
        LOG_ERROR("Aroma UI not initialized");
        return NULL;
    }

    if (!title || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid window parameters");
        return NULL;
    }

    return aroma_ui_create_window_impl(title, width, height);
}

extern void aroma_ui_destroy_window_impl(AromaWindow* window);

static inline void aroma_ui_destroy_window(AromaWindow* window) {
    if (!window) return;
    aroma_ui_destroy_window_impl(window);
}

static inline void aroma_ui_window_set_background(AromaWindow* window, uint32_t color) {
    if (!window) return;
    LOG_INFO("Window background color set to 0x%06X", color);
}

static inline void aroma_ui_window_set_visible(AromaWindow* window, bool visible) {
    if (!window) return;
    LOG_INFO("Window visibility set to %s", visible ? "visible" : "hidden");
}

static inline int aroma_ui_window_count(void) {
    return g_window_count;
}

static inline AromaWindow* aroma_ui_get_window_at(int index) {
    if (index < 0 || index >= g_window_count) return NULL;
    return g_windows[index].window;
}

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

typedef struct {
    bool (*handler)(AromaSlider*, int, void*);
    void* user_data;
} UISliderChangeBridge;

static inline bool __ui_slider_on_change_bridge(AromaNode* node, void* data) {
    UISliderChangeBridge* bridge = (UISliderChangeBridge*)data;
    if (!bridge || !bridge->handler || !node) return false;
    struct AromaSlider* sl = (struct AromaSlider*)node->node_widget_ptr;
    int value = sl ? sl->current_value : 0;
    return bridge->handler((AromaSlider*)node, value, bridge->user_data);
}

static inline AromaSlider* aroma_ui_create_slider(AromaWindow* parent,
                                     int x, int y, int width, int height) {
    if (!parent) return NULL;

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* slider = aroma_slider_create(parent_node, x, y, width, height, 0, 100, 50);
    if (!slider) {
        LOG_ERROR("Failed to create slider");
        return NULL;
    }
    aroma_slider_setup_events(slider, aroma_ui_request_redraw, NULL);
    aroma_node_invalidate(slider);

    LOG_INFO("Slider created");
    return (AromaSlider*)slider;
}

static inline void aroma_ui_slider_set_value(AromaSlider* slider, float value) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    int int_value = (int)value;
    if (int_value < 0) int_value = 0;
    if (int_value > 100) int_value = 100;
    aroma_slider_set_value(slider_node, int_value);
    aroma_node_invalidate(slider_node);
}

static inline float aroma_ui_slider_get_value(AromaSlider* slider) {
    if (!slider) return 0.0f;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    return sl ? (float)sl->current_value : 0.0f;
}

static inline void aroma_ui_slider_set_range(AromaSlider* slider, int min, int max) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    if (sl) {
        sl->min_value = min;
        sl->max_value = max;
        if (sl->current_value < min) sl->current_value = min;
        if (sl->current_value > max) sl->current_value = max;
        aroma_node_invalidate(slider_node);
    }
}

static inline void aroma_ui_on_slider_change(AromaSlider* slider,
                                bool (*handler)(AromaSlider*, int, void*),
                                void* user_data) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    if (sl) {
        UISliderChangeBridge* bridge = (UISliderChangeBridge*)sl->user_data;
        if (!bridge) {
            bridge = (UISliderChangeBridge*)malloc(sizeof(UISliderChangeBridge));
            if (!bridge) return;
            sl->user_data = bridge;
        }
        bridge->handler = handler;
        bridge->user_data = user_data;
        sl->on_change = __ui_slider_on_change_bridge;
    }
}

static inline void aroma_ui_destroy_slider(AromaSlider* slider) {
    if (!slider) return;
    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = slider_node ? (struct AromaSlider*)slider_node->node_widget_ptr : NULL;
    if (sl && sl->user_data) {
        free(sl->user_data);
        sl->user_data = NULL;
    }
    aroma_slider_destroy(slider_node);
}

static inline AromaTextbox* aroma_ui_create_textbox(AromaWindow* parent, const char* placeholder,
                                       int x, int y, int width, int height) {
    if (!parent) return NULL;

    AromaNode* parent_node = (AromaNode*)parent;
    AromaNode* textbox = aroma_textbox_create(parent_node, x, y, width, height);
    if (!textbox) {
        LOG_ERROR("Failed to create textbox");
        return NULL;
    }

    if (placeholder) {
        aroma_textbox_set_placeholder(textbox, placeholder);
    }

    aroma_textbox_setup_events(textbox, aroma_ui_request_redraw, NULL, NULL);
    aroma_node_invalidate(textbox);

    LOG_INFO("Textbox created with placeholder: %s", placeholder ? placeholder : "");
    return (AromaTextbox*)textbox;
}

static inline void aroma_ui_textbox_set_text(AromaTextbox* textbox, const char* text) {
    if (!textbox || !text) return;

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    if (tb) {
        strncpy(tb->text, text, AROMA_TEXTBOX_MAX_LENGTH - 1);
        tb->text[AROMA_TEXTBOX_MAX_LENGTH - 1] = '\0';
        tb->text_length = strlen(tb->text);
        aroma_node_invalidate(textbox_node);
    }
}

static inline const char* aroma_ui_textbox_get_text(AromaTextbox* textbox) {
    if (!textbox) return "";

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    return tb ? tb->text : "";
}

static inline void aroma_ui_on_textbox_change(AromaTextbox* textbox,
                                 bool (*handler)(AromaTextbox*, const char*, void*),
                                 void* user_data) {
    if (!textbox) return;

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    if (tb) {
        tb->on_text_changed = (bool (*)(AromaNode*, const char*, void*))handler;
        tb->user_data = user_data;
    }
}

static inline void aroma_ui_destroy_textbox(AromaTextbox* textbox) {
    if (!textbox) return;
    aroma_textbox_destroy((AromaNode*)textbox);
}

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

    AromaNode* switch_node = (AromaNode*)sw;
    struct AromaSwitch* swi = (struct AromaSwitch*)switch_node->node_widget_ptr;
    if (swi) {
        swi->state = is_on;
        aroma_node_invalidate(switch_node);
    }
}

static inline bool aroma_ui_switch_get_state(AromaSwitch* sw) {
    if (!sw) return false;

    AromaNode* switch_node = (AromaNode*)sw;
    struct AromaSwitch* swi = (struct AromaSwitch*)switch_node->node_widget_ptr;
    return swi ? swi->state : false;
}

static inline void aroma_ui_on_switch_change(AromaSwitch* sw,
                                bool (*handler)(AromaSwitch*, bool, void*),
                                void* user_data) {
    if (!sw) return;

    AromaNode* switch_node = (AromaNode*)sw;
    struct AromaSwitch* switch_data = (struct AromaSwitch*)switch_node->node_widget_ptr;
    if (switch_data) {
        switch_data->on_change = (bool (*)(AromaNode*, void*))handler;
        switch_data->user_data = user_data;
    }
}

static inline void aroma_ui_destroy_switch(AromaSwitch* sw) {
    if (!sw) return;
    aroma_switch_destroy((AromaNode*)sw);
}

static inline void aroma_ui_request_redraw(void* user_data);
static inline bool aroma_ui_consume_redraw(void);

static inline void aroma_ui_request_redraw(void* user_data) {
    (void)user_data;
    if (g_main_window) {
        aroma_node_invalidate(g_main_window);
    }
}

static inline bool aroma_ui_consume_redraw(void) {
    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    return dirty_count > 0;
}

void aroma_graphics_clear(size_t window_id, uint32_t color);

void aroma_graphics_render_text(size_t window_id, AromaFont* font, const char* text,
                                int x, int y, uint32_t color);

void aroma_graphics_swap_buffers(size_t window_id);

void aroma_button_draw(AromaNode* button, size_t window_id);

void aroma_textbox_draw(AromaNode* textbox, size_t window_id);

void aroma_dropdown_draw(AromaNode* dropdown, size_t window_id);

void aroma_slider_draw(AromaNode* slider, size_t window_id);

void aroma_switch_draw(AromaNode* switch_node, size_t window_id);

void aroma_platform_set_window_update_callback(void (*callback)(size_t, void*), void* user_data);

#endif 

