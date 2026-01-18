#include "aroma_ui.h"
#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "widgets/aroma_button.h"
#include "widgets/aroma_dropdown.h"
#include "widgets/aroma_slider.h"
#include "widgets/aroma_switch.h"
#include "widgets/aroma_textbox.h"
#include "widgets/aroma_window.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void aroma_graphics_load_font_for_window(size_t window_id, AromaFont* font);

static bool g_ui_initialized = false;
static AromaNode* g_main_window = NULL;  
static bool g_auto_render_enabled = true;

typedef struct {
    bool (*handler)(AromaSlider*, int, void*);
    void* user_data;
} UISliderChangeBridge;

static bool __ui_slider_on_change_bridge(AromaNode* node, void* data) {
    UISliderChangeBridge* bridge = (UISliderChangeBridge*)data;
    if (!bridge || !bridge->handler || !node) return false;
    struct AromaSlider* sl = (struct AromaSlider*)node->node_widget_ptr;
    int value = sl ? sl->current_value : 0;
    return bridge->handler((AromaSlider*)node, value, bridge->user_data);
}

bool aroma_ui_init(void) {
    if (g_ui_initialized) {
        LOG_INFO("Aroma UI already initialized");
        return true;
    }

    aroma_memory_system_init();
    __node_system_init();
    aroma_event_system_init();

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->initialize) {
        if (!platform->initialize()) {
            LOG_CRITICAL("Failed to initialize platform backend");
            return false;
        }
        LOG_INFO("Platform backend initialized successfully");
    } else {
        LOG_WARNING("Platform backend not available or has no initialize function");
    }

    AromaTheme default_theme = aroma_theme_create_default();
    aroma_theme_set_global(&default_theme);

    aroma_dirty_list_clear();

    g_ui_initialized = true;
    LOG_INFO("Aroma UI initialized successfully");

    return true;
}

void aroma_ui_set_theme(const AromaTheme* theme) {
    if (theme) {
        aroma_theme_set_global(theme);
        LOG_INFO("Theme updated");
    }
}

AromaTheme aroma_ui_get_theme(void) {
    return aroma_theme_get_global();
}

void aroma_ui_shutdown(void) {
    if (!g_ui_initialized) return;

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->shutdown) {
        platform->shutdown();
        LOG_INFO("Platform backend shutdown");
    }

    aroma_event_system_shutdown();
    __node_system_destroy();

    g_ui_initialized = false;
    g_main_window = NULL;
    LOG_INFO("Aroma UI shutdown complete");
}

bool aroma_ui_is_running(void) {
    if (!g_ui_initialized) return false;

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->run_event_loop) {
        return platform->run_event_loop();
    }

    return false;
}

void aroma_ui_process_events(void) {
    if (!g_ui_initialized) return;

    aroma_event_process_queue();
}

void aroma_ui_render(AromaWindow* window) {
    if (!g_ui_initialized || !window) return;
    AromaNode* window_node = (AromaNode*)window;
    if (!window_node || window_node->node_type != NODE_TYPE_ROOT) return;

    AromaWindow* window_data = (AromaWindow*)window_node->node_widget_ptr;
    if (!window_data) return;

    size_t dirty_count = 0;
    AromaNode** dirty_nodes = aroma_dirty_list_get(&dirty_count);

    if (dirty_count == 0) return;  

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->request_window_update) {
        platform->request_window_update(window_data->window_id);
    }
}

AromaFont* aroma_ui_load_font(const char* path, int size_px) {
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

void aroma_ui_unload_font(AromaFont* font) {
    if (font) {
        aroma_font_destroy(font);
        LOG_INFO("Font unloaded");
    }
}

AromaWindow* aroma_ui_create_window(const char* title, int width, int height) {
    if (!g_ui_initialized) {
        LOG_ERROR("Aroma UI not initialized");
        return NULL;
    }

    if (!title || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid window parameters");
        return NULL;
    }

    AromaNode* window = aroma_window_create(title, 0, 0, width, height);
    if (!window) {
        LOG_ERROR("Failed to create window");
        return NULL;
    }

    aroma_event_set_root(window);

    if (!g_main_window) {
        g_main_window = window;
    }

    LOG_INFO("Window created: title='%s', size=%dx%d", title, width, height);

    aroma_node_invalidate(window);

    return (AromaWindow*)window;
}

void aroma_ui_destroy_window(AromaWindow* window) {
    if (!window) return;

    if (window == g_main_window) {
        g_main_window = NULL;
    }

    aroma_window_destroy((AromaNode*)window);
    LOG_INFO("Window destroyed");
}

void aroma_ui_window_set_background(AromaWindow* window, uint32_t color) {
    if (!window) return;

    LOG_INFO("Window background color set to 0x%06X", color);
}

void aroma_ui_window_set_visible(AromaWindow* window, bool visible) {
    if (!window) return;

    LOG_INFO("Window visibility set to %s", visible ? "visible" : "hidden");
}

AromaButton* aroma_ui_create_button(AromaWindow* parent, const char* label,
                                     int x, int y, int width, int height) {
    fprintf(stderr, "DEBUG_aroma_ui: create_button called\n");
    fflush(stderr);

    if (!parent || !label) {
        LOG_ERROR("Invalid button parameters");
        return NULL;
    }

    fprintf(stderr, "DEBUG_aroma_ui: parent and label valid\n");
    fflush(stderr);

    AromaNode* parent_node = (AromaNode*)parent;
    fprintf(stderr, "DEBUG_aroma_ui: parent_node cast\n");
    fflush(stderr);

    AromaNode* button = aroma_button_create(parent_node, label, x, y, width, height);
    fprintf(stderr, "DEBUG_aroma_ui: button_create returned %p\n", (void*)button);
    fflush(stderr);

    if (!button) {
        LOG_ERROR("Failed to create button");
        return NULL;
    }

    fprintf(stderr, "DEBUG_aroma_ui: button created successfully\n");
    fflush(stderr);

    AromaStyle style = aroma_style_create_from_theme(NULL);
    aroma_button_apply_style(button, &style);

    AromaNode* button_node = (AromaNode*)button;
    struct AromaButton* button_data = (struct AromaButton*)button_node->node_widget_ptr;
    if (button_data) {
        AromaNode* root_node = (AromaNode*)parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    if (g_windows[i].default_font) {
                        aroma_button_set_font(button_node, g_windows[i].default_font);
                    }
                    break;
                }
            }
        }
    }

    aroma_button_setup_events(button, aroma_ui_request_redraw, NULL);

    aroma_node_invalidate(button);
    LOG_INFO("Button created: label='%s'", label);
    return (AromaButton*)button;
}

void aroma_ui_on_button_click(AromaButton* button,
                               bool (*handler)(AromaButton*, void*),
                               void* user_data) {
    if (!button) return;

    AromaNode* button_node = (AromaNode*)button;
    aroma_button_set_on_click(button_node, (bool (*)(AromaNode*, void*))handler, user_data);
    LOG_INFO("Button click handler registered");
}

void aroma_ui_button_set_label(AromaButton* button, const char* label) {
    if (!button || !label) return;

    AromaNode* button_node = (AromaNode*)button;
    struct AromaButton* button_data = (struct AromaButton*)button_node->node_widget_ptr;
    if (button_data) {
        strncpy(button_data->label, label, AROMA_BUTTON_LABEL_MAX - 1);
        button_data->label[AROMA_BUTTON_LABEL_MAX - 1] = '\0';
        aroma_node_invalidate(button_node);
    }
}

void aroma_ui_button_set_enabled(AromaButton* button, bool enabled) {
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

void aroma_ui_button_set_style(AromaButton* button, const AromaStyle* style) {
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

void aroma_ui_destroy_button(AromaButton* button) {
    if (!button) return;
    aroma_button_destroy((AromaNode*)button);
}

AromaDropdown* aroma_ui_create_dropdown(AromaWindow* parent,
                                         int x, int y, int width, int height) {
    if (!parent) return NULL;

    AromaNode* dropdown = aroma_dropdown_create((AromaNode*)parent, x, y, width, height);
    if (!dropdown) {
        LOG_ERROR("Failed to create dropdown");
        return NULL;
    }

    AromaTheme theme = aroma_ui_get_theme();
    AromaNode* dropdown_node = (AromaNode*)dropdown;
    struct AromaDropdown* dd = (struct AromaDropdown*)dropdown_node->node_widget_ptr;
    if (dd) {
        dd->list_bg_color = theme.colors.surface;
        dd->hover_bg_color = aroma_color_blend(theme.colors.primary_light, theme.colors.surface, 0.3f);
        dd->selected_bg_color = aroma_color_blend(theme.colors.primary_light, theme.colors.surface, 0.15f);
        dd->text_color = theme.colors.text_primary;

        AromaNode* root_node = (AromaNode*)parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    dd->font = g_windows[i].default_font;
                    break;
                }
            }
        }
    }

    aroma_dropdown_setup_events(dropdown_node, aroma_ui_request_redraw, NULL);
    aroma_node_invalidate(dropdown);

    LOG_INFO("Dropdown created");
    return (AromaDropdown*)dropdown;
}

int aroma_ui_dropdown_add_option(AromaDropdown* dropdown, const char* label) {
    if (!dropdown || !label) return -1;

    AromaNode* dropdown_node = (AromaNode*)dropdown;
    aroma_dropdown_add_option(dropdown_node, label);
    aroma_node_invalidate(dropdown_node);
    return 0;
}

int aroma_ui_dropdown_get_selected(AromaDropdown* dropdown) {
    if (!dropdown) return -1;

    AromaNode* dropdown_node = (AromaNode*)dropdown;
    struct AromaDropdown* dd = (struct AromaDropdown*)dropdown_node->node_widget_ptr;
    return dd ? dd->selected_index : -1;
}

void aroma_ui_dropdown_set_selected(AromaDropdown* dropdown, int index) {
    if (!dropdown || index < -1) return;

    AromaNode* dropdown_node = (AromaNode*)dropdown;
    struct AromaDropdown* dd = (struct AromaDropdown*)dropdown_node->node_widget_ptr;
    if (dd && (index == -1 || index < (int)dd->option_count)) {
        dd->selected_index = index;
        aroma_node_invalidate(dropdown_node);
    }
}

void aroma_ui_on_dropdown_change(AromaDropdown* dropdown,
                                  bool (*handler)(AromaDropdown*, const char*, int, void*),
                                  void* user_data) {
    if (!dropdown) return;

    AromaNode* dropdown_node = (AromaNode*)dropdown;
    struct AromaDropdown* dd = (struct AromaDropdown*)dropdown_node->node_widget_ptr;
    if (dd) {
        dd->on_change = (bool (*)(AromaNode*, const char*, int, void*))handler;
        dd->user_data = user_data;
    }
}

void aroma_ui_destroy_dropdown(AromaDropdown* dropdown) {
    if (!dropdown) return;
    aroma_dropdown_destroy((AromaNode*)dropdown);
}

AromaSlider* aroma_ui_create_slider(AromaWindow* parent,
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

void aroma_ui_slider_set_value(AromaSlider* slider, float value) {
    if (!slider) return;

    AromaNode* slider_node = (AromaNode*)slider;
    int int_value = (int)value;
    if (int_value < 0) int_value = 0;
    if (int_value > 100) int_value = 100;
    aroma_slider_set_value(slider_node, int_value);
    aroma_node_invalidate(slider_node);
}

float aroma_ui_slider_get_value(AromaSlider* slider) {
    if (!slider) return 0.0f;

    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = (struct AromaSlider*)slider_node->node_widget_ptr;
    return sl ? (float)sl->current_value : 0.0f;
}

void aroma_ui_slider_set_range(AromaSlider* slider, int min, int max) {
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

void aroma_ui_on_slider_change(AromaSlider* slider,
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

void aroma_ui_destroy_slider(AromaSlider* slider) {
    if (!slider) return;
    AromaNode* slider_node = (AromaNode*)slider;
    struct AromaSlider* sl = slider_node ? (struct AromaSlider*)slider_node->node_widget_ptr : NULL;
    if (sl && sl->user_data) {
        free(sl->user_data);
        sl->user_data = NULL;
    }
    aroma_slider_destroy(slider_node);
}

AromaTextbox* aroma_ui_create_textbox(AromaWindow* parent, const char* placeholder,
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

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* textbox_data = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    if (textbox_data) {
        AromaNode* root_node = (AromaNode*)parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    if (g_windows[i].default_font) {
                        aroma_textbox_set_font(textbox_node, g_windows[i].default_font);
                    }
                    break;
                }
            }
        }
    }

    aroma_textbox_setup_events(textbox, aroma_ui_request_redraw, NULL, NULL);
    aroma_node_invalidate(textbox);

    LOG_INFO("Textbox created with placeholder: %s", placeholder ? placeholder : "");
    return (AromaTextbox*)textbox;
}

void aroma_ui_textbox_set_text(AromaTextbox* textbox, const char* text) {
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

const char* aroma_ui_textbox_get_text(AromaTextbox* textbox) {
    if (!textbox) return "";

    AromaNode* textbox_node = (AromaNode*)textbox;
    struct AromaTextbox* tb = (struct AromaTextbox*)textbox_node->node_widget_ptr;
    return tb ? tb->text : "";
}

void aroma_ui_on_textbox_change(AromaTextbox* textbox,
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

void aroma_ui_destroy_textbox(AromaTextbox* textbox) {
    if (!textbox) return;
    aroma_textbox_destroy((AromaNode*)textbox);
}

AromaSwitch* aroma_ui_create_switch(AromaWindow* parent, const char* label,
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

void aroma_ui_switch_set_state(AromaSwitch* sw, bool is_on) {
    if (!sw) return;

    AromaNode* switch_node = (AromaNode*)sw;
    struct AromaSwitch* swi = (struct AromaSwitch*)switch_node->node_widget_ptr;
    if (swi) {
        swi->state = is_on;
        aroma_node_invalidate(switch_node);
    }
}

bool aroma_ui_switch_get_state(AromaSwitch* sw) {
    if (!sw) return false;

    AromaNode* switch_node = (AromaNode*)sw;
    struct AromaSwitch* swi = (struct AromaSwitch*)switch_node->node_widget_ptr;
    return swi ? swi->state : false;
}

void aroma_ui_on_switch_change(AromaSwitch* sw,
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

void aroma_ui_destroy_switch(AromaSwitch* sw) {
    if (!sw) return;
    aroma_switch_destroy((AromaNode*)sw);
}

void aroma_ui_prepare_font_for_window(size_t window_id, AromaFont* font) {
    if (!font) return;
    aroma_graphics_load_font_for_window(window_id, font);
    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].is_active && g_windows[i].window_id == window_id) {
            g_windows[i].default_font = font;
            break;
        }
    }
}

void aroma_ui_request_redraw(void* user_data) {
    (void)user_data;

    if (g_main_window) {
        aroma_node_invalidate(g_main_window);
    }
}

bool aroma_ui_consume_redraw(void) {

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    return dirty_count > 0;
}

