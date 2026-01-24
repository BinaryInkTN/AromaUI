#ifndef AROMA_UI_H
#define AROMA_UI_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "aroma_style.h"
#include "aroma_widgets.h"
#include "aroma_drawlist.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaNode AromaNode;
typedef struct AromaWindow AromaWindow;
typedef struct AromaContainer AromaContainer;
typedef struct AromaButton AromaButton;
typedef struct AromaDropdown AromaDropdown;
typedef struct AromaSlider AromaSlider;
typedef struct AromaTextbox AromaTextbox;
typedef struct AromaSwitch AromaSwitch;
typedef struct AromaMenu AromaMenu;
typedef struct AromaFont AromaFont;
typedef struct AromaCheckbox AromaCheckbox;
typedef struct AromaRadioButton AromaRadioButton;
typedef struct AromaProgressBar AromaProgressBar;
typedef struct AromaLabel AromaLabel;
typedef struct AromaDivider AromaDivider;
typedef struct AromaIconButton AromaIconButton;
typedef struct AromaDialog AromaDialog;
typedef struct AromaListView AromaListView;
typedef struct AromaTooltip AromaTooltip;
typedef struct AromaCard AromaCard;
typedef struct AromaChip AromaChip;
typedef struct AromaFAB AromaFAB;
typedef struct AromaSnackbar AromaSnackbar;
typedef struct AromaTabs AromaTabs;
typedef struct AromaSidebar AromaSidebar;
typedef struct AromaDebugOverlay AromaDebugOverlay;

typedef struct {
    AromaWindow* window;
    AromaNode* root_node;
    size_t window_id;
    bool is_active;
    AromaFont* default_font;
} AromaWindowHandle;

#define AROMA_MAX_WINDOWS 16
#define AROMA_CLEAR_NONE UINT32_MAX

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

void aroma_ui_set_immediate_mode(bool enabled);
bool aroma_ui_is_immediate_mode(void);
void aroma_ui_request_redraw(void* user_data);
bool aroma_ui_consume_redraw(void);

AromaDrawList* aroma_ui_begin_frame(size_t window_id);
void aroma_ui_end_frame(size_t window_id);
void aroma_ui_render_dirty_window(size_t window_id, uint32_t clear_color);

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
    if (dirty_count == 0 && !aroma_ui_is_immediate_mode()) return;

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

void aroma_graphics_clear(size_t window_id, uint32_t color);

void aroma_graphics_render_text(size_t window_id, AromaFont* font, const char* text,
                                int x, int y, uint32_t color, float scale);

void aroma_graphics_swap_buffers(size_t window_id);

void aroma_platform_set_window_update_callback(void (*callback)(size_t, void*), void* user_data);

#endif 

