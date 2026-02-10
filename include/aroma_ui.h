#ifndef AROMA_UI_H
#define AROMA_UI_H

/**
 * @file aroma_ui.h
 * @brief Core UI definitions and functions for AromaUI.
 *
 * This file contains the main API for initializing the library,
 * creating windows, managing the event loop, and helper functions
 * to create widgets.
 */

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "aroma_style.h"
#include "aroma_widgets.h"
#include "aroma_material_icons.h"
#include "aroma_drawlist.h"
#include "aroma_material_font.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef ESP32
#include <Arduino.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
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
typedef struct AromaIcon AromaIcon;

/**
 * @struct AromaWindowHandle
 * @brief Internal handle for managing window state.
 */
typedef struct {
    /** @brief Pointer to the window structure. */
    AromaWindow* window;
    /** @brief Pointer to the root node of the window. */
    AromaNode* root_node;
    /** @brief Unique window identifier. */
    size_t window_id;
    /** @brief Whether the window is currently active. */
    bool is_active;
    /** @brief Default font associated with the window. */
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

/**
 * @brief Get the currently focused node.
 * @return Pointer to the focused node, or NULL if none.
 */
static inline AromaNode* aroma_ui_get_focused_node(void) {
    return g_focused_node;
}

/**
 * @brief Set the focused node.
 * @param node Pointer to the node to focus.
 */
static inline void aroma_ui_set_focused_node(AromaNode* node) {
    g_focused_node = node;
}

/**
 * @brief Clear focus if the given node is focused.
 * @param node The node to clear focus from.
 */
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

/**
 * @brief Initialize the AromaUI library.
 * @return true if initialization was successful, false otherwise.
 */
static inline bool aroma_ui_init(void) {
    if (g_ui_initialized) {
        LOG_INFO("Aroma UI already initialized");
        return true;
    }
    return aroma_ui_init_impl();
}

/**
 * @brief Set the global theme for UI.
 * @param theme Pointer to the theme structure.
 */
static inline void aroma_ui_set_theme(const AromaTheme* theme) {
    if (theme) {
        aroma_theme_set_global(theme);
        LOG_INFO("Theme updated");
    }
}

/**
 * @brief Get the current global theme.
 * @return The current AromaTheme structure.
 */
static inline AromaTheme aroma_ui_get_theme(void) {
    return aroma_theme_get_global();
}

extern void aroma_ui_shutdown_impl(void);

/**
 * @brief Shutdown the AromaUI library and release resources.
 */
static inline void aroma_ui_shutdown(void) {
    if (!g_ui_initialized) return;
    aroma_ui_shutdown_impl();
}

extern bool aroma_ui_is_running_impl(void);

/**
 * @brief Check if the UI event loop is running.
 * @return true if running, false if shutdown requested.
 */
static inline bool aroma_ui_is_running(void) {
    if (!g_ui_initialized) return false;
    return aroma_ui_is_running_impl();
}

extern void aroma_ui_process_events_impl(void);

/**
 * @brief Process pending events.
 * 
 * This function handles input events, timers, and other system events.
 * It should be called repeatedly in the main loop.
 */
static inline void aroma_ui_process_events(void) {
    if (!g_ui_initialized) return;
    aroma_ui_process_events_impl();
}

extern void aroma_ui_render_impl(struct AromaWindow* window_data);
extern void aroma_ui_render_all_windows_impl(void);

/**
 * @brief Render a specific window.
 * @param window Pointer to the window to render.
 */
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

/**
 * @brief Render all active windows.
 */
static inline void aroma_ui_render_all(void) {
    if (!g_ui_initialized) return;
    aroma_ui_render_all_windows_impl();
}

/**
 * @brief Load a font from a file path.
 * @param path Path to the font file (e.g. .ttf, .otf).
 * @param size_px Font size in pixels.
 * @return Pointer to the loaded font, or NULL on failure.
 */
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

/**
 * @brief Unload and destroy a font.
 * @param font Pointer to the font to unload.
 */
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
extern void aroma_ui_open_url_impl(const char* url);



/**
 * @brief Create a new window.
 * 
 * @param title The window title (used by OS window manager).
 * @param width The width of the window content area.
 * @param height The height of the window content area.
 * @return Pointer to the created AromaWindow, or NULL on failure.
 */
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

/**
 * @brief Destroy a window and free resources.
 * @param window Pointer to the window to destroy.
 */
static inline void aroma_ui_destroy_window(AromaWindow* window) {
    if (!window) return;
    aroma_ui_destroy_window_impl(window);
}

/**
 * @brief Set the background color of a window.
 * @param window Pointer to the window.
 * @param color Color in 0xRRGGBB format.
 */
static inline void aroma_ui_window_set_background(AromaWindow* window, uint32_t color) {
    if (!window) return;
    LOG_INFO("Window background color set to 0x%06X", color);
}

/**
 * @brief Set window visibility.
 * @param window Pointer to the window.
 * @param visible true to show, false to hide.
 */
static inline void aroma_ui_window_set_visible(AromaWindow* window, bool visible) {
    if (!window) return;
    LOG_INFO("Window visibility set to %s", visible ? "visible" : "hidden");
}

/**
 * @brief Get the total number of managed windows.
 * @return Number of windows.
 */
static inline int aroma_ui_window_count(void) {
    return g_window_count;
}

/**
 * @brief Get a window by index.
 * @param index Index of the window (0 to count-1).
 * @return Pointer to AromaWindow, or NULL if index invalid.
 */
static inline AromaWindow* aroma_ui_get_window_at(int index) {
    if (index < 0 || index >= g_window_count) return NULL;
    return g_windows[index].window;
}

/**
 * @brief Open a URL in the default browser.
 * @param url The URL to open.
 */
static inline void aroma_ui_open_url(const char* url) {
    if (!g_ui_initialized || !url) return;
    aroma_ui_open_url_impl(url);
}

void aroma_graphics_clear(size_t window_id, uint32_t color);

void aroma_graphics_render_text(size_t window_id, AromaFont* font, const char* text,
                                int x, int y, uint32_t color, float scale);

void aroma_graphics_swap_buffers(size_t window_id);

/**
 * @enum AromaIntentAction
 * @brief Android Intent Actions.
 */
typedef enum {
    /** @brief android.intent.action.VIEW */
    AROMA_INTENT_VIEW,
    /** @brief android.intent.action.SEND */
    AROMA_INTENT_SEND,
    /** @brief android.intent.action.EDIT */
    AROMA_INTENT_EDIT,
    /** @brief android.intent.action.DIAL */
    AROMA_INTENT_DIAL,
    /** @brief android.intent.action.CALL */
    AROMA_INTENT_CALL
} AromaIntentAction;

/**
 * @struct AromaIntentExtra
 * @brief Key-value pair for Intent extras.
 */
typedef struct {
    /** @brief Extra key string. */
    const char* key;
    /** @brief Extra value string. */
    const char* string_value;
} AromaIntentExtra;

// Android specific: Generic Intent
/**
 * @brief Send an Android Intent.
 * 
 * @param action The intent action to perform.
 * @param uri The data URI (e.g. "https://...", "tel:...").
 * @param type The MIME type (optional, can be NULL).
 * @param extras Array of extra key-value pairs (optional, can be NULL).
 * @param extra_count Number of extras.
 */
static inline void aroma_ui_android_intent(AromaIntentAction action, const char* uri, const char* type, const AromaIntentExtra* extras, int extra_count) {
    if (!g_ui_initialized) return;
    extern void aroma_ui_android_intent_impl(int action, const char* uri, const char* type, const AromaIntentExtra* extras, int extra_count);
    aroma_ui_android_intent_impl((int)action, uri, type, extras, extra_count);
}

void aroma_platform_set_window_update_callback(void (*callback)(size_t, void*), void* user_data);

/**
 * @brief Helper to create a button widget.
 * 
 * @param parent Parent node.
 * @param text Button label text.
 * @param x X position relative to parent.
 * @param y Y position relative to parent.
 * @param width Button width.
 * @param height Button height.
 * @param on_click Callback function for click event.
 * @param user_data User data passed to callback.
 * @param font Font to use for label (optional).
 * @return Pointer to the created button node.
 */
static inline AromaNode* aroma_ui_button(
    AromaNode* parent,
    const char* text,
    int x, int y, int width, int height,
    bool (*on_click)(AromaNode*, void*),
    void* user_data,
    AromaFont* font
) {
    if (!parent) return NULL;
    AromaNode* btn = aroma_button_create(parent, text, x, y, width, height);
    if (btn) {
        if (on_click) {
            aroma_button_set_on_click(btn, on_click, user_data);
        }
        aroma_button_setup_events(btn, aroma_ui_request_redraw, NULL);
        if (font) {
            aroma_button_set_font(btn, font);
        }
    }
    return btn;
}

/**
 * @brief Helper to create a button widget with an icon.
 * 
 * @param parent Parent node.
 * @param text Button label text.
 * @param x X position relative to parent.
 * @param y Y position relative to parent.
 * @param width Button width.
 * @param height Button height.
 * @param on_click Callback function for click event.
 * @param user_data User data passed to callback.
 * @param font Font to use for label (optional).
 * @param icon_code Icon codepoint (e.g. AROMA_ICON_HOME).
 * @param icon_font Font containing the icons.
 * @return Pointer to the created button node.
 */
static inline AromaNode* aroma_ui_button_with_icon(
    AromaNode* parent,
    const char* text,
    int x, int y, int width, int height,
    bool (*on_click)(AromaNode*, void*),
    void* user_data,
    AromaFont* font,
    const char* icon_code,
    AromaFont* icon_font
) {
    AromaNode* btn = aroma_ui_button(parent, text, x, y, width, height, on_click, user_data, font);
    if (btn && icon_code && icon_font) {
        aroma_button_set_icon(btn, icon_code, icon_font);
    }
    return btn;
}

/**
 * @brief Helper to create a label widget.
 * 
 * @param parent Parent node.
 * @param text Label text.
 * @param x X position.
 * @param y Y position.
 * @param style Label style (e.g. LABEL_STYLE_BODY).
 * @param font Font to use.
 * @return Pointer to the created label node.
 */
static inline AromaNode* aroma_ui_label(
    AromaNode* parent,
    const char* text,
    int x, int y,
    AromaLabelStyle style,
    AromaFont* font
) {
    if (!parent) return NULL;
    AromaNode* label = aroma_label_create(parent, text, x, y, style);
    if (label && font) {
        aroma_label_set_font(label, font);
    }
    return label;
}

static inline AromaNode* aroma_ui_container(
    AromaNode* parent,
    int x, int y, int width, int height,
    AromaLayoutMode layout_mode,
    AromaFlexDirection flex_dir, // Optional: AROMA_FLEX_ROW/COLUMN
    AromaJustifyContent justify, // Optional: AROMA_JUSTIFY_...
    AromaAlignItems align        // Optional: AROMA_ALIGN_...
) {
    AromaNode* cont = aroma_container_create(parent, x, y, width, height);
    if (cont && layout_mode != AROMA_LAYOUT_MODE_NONE) {
        aroma_node_set_layout_mode(cont, layout_mode);
        aroma_node_set_flex_direction(cont, flex_dir);
        aroma_node_set_justify_content(cont, justify);
        aroma_node_set_align_items(cont, align);
    }
    return cont;
}

static inline AromaNode* aroma_ui_image(
    AromaNode* parent,
    const char* path,
    int x, int y, int width, int height
) {
    return aroma_image_create(parent, path, x, y, width, height);
}

static inline AromaNode* aroma_ui_image_mem(
    AromaNode* parent,
    unsigned char* data,
    size_t len,
    int x, int y, int width, int height
) {
    return aroma_image_create_from_memory(parent, data, len, x, y, width, height);
}

/**
 * @brief Helper to create an icon widget.
 * 
 * @param parent Parent node.
 * @param icon_code Icon codepoint string (e.g. AROMA_ICON_HOME).
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param size Icon size (width/height).
 * @param color Icon color.
 * @param font Font containing the icons (e.g. Material Icons font).
 * @return Pointer to the new icon node.
 */
static inline AromaNode* aroma_ui_icon(
    AromaNode* parent,
    const char* icon_code,
    int x, int y, int size,
    uint32_t color,
    AromaFont* font
) {
    AromaNode* node = aroma_icon_create(parent, x, y, size);
    if (node) {
        if (icon_code && font) {
            aroma_icon_set_text(node, icon_code, font);
        }
        aroma_icon_set_color(node, color);
    }
    return node;
}

/**
 * @brief Create a checkbox helper.
 * 
 * @param parent Parent node (usually a container or window).
 * @param label Checkbox text label.
 * @param x X-coordinate relative to parent.
 * @param y Y-coordinate relative to parent.
 * @param width Width of the checkbox area.
 * @param height Height of the checkbox area.
 * @param callback Function called when checked state changes.
 * @param user_data User pointer passed to the callback.
 * @param font Font to use for the label.
 * @return Pointer to the new checkbox node.
 */
static inline AromaNode* aroma_ui_checkbox(
    AromaNode* parent,
    const char* label,
    int x, int y, int width, int height,
    void (*callback)(bool, void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* cb = aroma_checkbox_create(parent, label, x, y, width, height);
    if (cb) {
        if (callback) aroma_checkbox_set_callback(cb, callback, user_data);
        if (font) aroma_checkbox_set_font(cb, font);
        aroma_checkbox_setup_events(cb, aroma_ui_request_redraw, NULL);
    }
    return cb;
}

/**
 * @brief Create a radio button helper.
 * 
 * @param parent Parent node.
 * @param label Radio button text label.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param group_id ID of the radio group this button belongs to.
 * @param callback Callback on selection.
 * @param user_data User data.
 * @param font Font for the label.
 * @return Pointer to the new radio button node.
 */
static inline AromaNode* aroma_ui_radiobutton(
    AromaNode* parent,
    const char* label,
    int x, int y, int width, int height,
    int group_id,
    void (*callback)(void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* rb = aroma_radiobutton_create(parent, label, x, y, width, height, group_id);
    if (rb) {
        if (callback) aroma_radiobutton_set_callback(rb, callback, user_data);
        if (font) aroma_radiobutton_set_font(rb, font);
        aroma_radio_button_setup_events(rb, aroma_ui_request_redraw, NULL);
    }
    return rb;
}

/**
 * @brief Create a switch helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param initial_state Initial On/Off state.
 * @param on_change Callback when state changes.
 * @param user_data User data.
 * @return Pointer to the new switch node.
 */
static inline AromaNode* aroma_ui_switch(
    AromaNode* parent,
    int x, int y, int width, int height,
    bool initial_state,
    bool (*on_change)(AromaNode*, void*),
    void* user_data
) {
    AromaNode* sw = aroma_switch_create(parent, x, y, width, height, initial_state);
    if (sw) {
        if (on_change) aroma_switch_set_on_change(sw, on_change, user_data);
        aroma_switch_setup_events(sw, aroma_ui_request_redraw, NULL);
    }
    return sw;
}

/**
 * @brief Create a slider helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param min Minimum string value.
 * @param max Maximum string value.
 * @param value Initial value.
 * @param on_change Callback when value changes.
 * @param user_data User data.
 * @return Pointer to the new slider node.
 */
static inline AromaNode* aroma_ui_slider(
    AromaNode* parent,
    int x, int y, int width, int height,
    int min, int max, int value,
    bool (*on_change)(AromaNode*, void*),
    void* user_data
) {
    AromaNode* sl = aroma_slider_create(parent, x, y, width, height, min, max, value);
    if (sl) {
        if (on_change) aroma_slider_set_on_change(sl, on_change, user_data);
        aroma_slider_setup_events(sl, aroma_ui_request_redraw, NULL);
    }
    return sl;
}

/**
 * @brief Create a textbox helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param placeholder Placeholder text when empty.
 * @param on_text_changed Callback when text changes.
 * @param user_data User data.
 * @param font Font to use.
 * @return Pointer to the new textbox node.
 */
static inline AromaNode* aroma_ui_textbox(
    AromaNode* parent,
    int x, int y, int width, int height,
    const char* placeholder,
    bool (*on_text_changed)(AromaNode*, const char*, void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* tb = aroma_textbox_create(parent, x, y, width, height);
    if (tb) {
        if (placeholder) aroma_textbox_set_placeholder(tb, placeholder);
        if (on_text_changed) aroma_textbox_set_on_text_changed(tb, on_text_changed, user_data);
        if (font) aroma_textbox_set_font(tb, font);
        aroma_textbox_setup_events(tb, aroma_ui_request_redraw, NULL, NULL);
    }
    return tb;
}


/**
 * @brief Create a Floating Action Button (FAB) helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param size FAB size (e.g. AROMA_FAB_SIZE_NORMAL).
 * @param icon_text Icon text (if using symbol font) or label.
 * @param callback Click callback.
 * @param user_data User data.
 * @param font Font to use.
 * @return Pointer to the new FAB node.
 */
static inline AromaNode* aroma_ui_fab(
    AromaNode* parent,
    int x, int y,
    AromaFABSize size,
    const char* icon_text,
    void (*callback)(void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* fab = aroma_fab_create(parent, x, y, size, icon_text);
    if (fab) {
        if (callback) aroma_fab_set_click_callback(fab, callback, user_data);
        if (font) aroma_fab_set_font(fab, font);
    }
    return fab;
}

/**
 * @brief Create an icon button helper.
 * 
 * @param parent Parent node.
 * @param icon Icon text/character.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param size Button size (width/height).
 * @param variant Visual variant (e.g. STANDARD, FILLED).
 * @param callback Click callback.
 * @param user_data User data.
 * @param font Font to use (usually an icon font).
 * @return Pointer to the new icon button node.
 */
static inline AromaNode* aroma_ui_iconbutton(
    AromaNode* parent,
    const char* icon,
    int x, int y,
    int size,
    AromaIconButtonVariant variant,
    void (*callback)(void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* btn = aroma_iconbutton_create(parent, icon, x, y, size, variant);
    if (btn) {
        if (callback) aroma_iconbutton_set_callback(btn, callback, user_data);
        if (font) aroma_iconbutton_set_font(btn, font);
    }
    return btn;
}

/**
 * @brief Create a chip helper.
 * 
 * @param parent Parent node.
 * @param label Chip label text.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param type Chip type/style.
 * @param callback Click callback.
 * @param user_data User data.
 * @param font Font to use.
 * @return Pointer to the new chip node.
 */
static inline AromaNode* aroma_ui_chip(
    AromaNode* parent,
    const char* label,
    int x, int y,
    AromaChipType type,
    void (*callback)(void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* chip = aroma_chip_create(parent, x, y, label, type);
    if (chip) {
        if (callback) aroma_chip_set_callback(chip, callback, user_data);
        if (font) aroma_chip_set_font(chip, font);
    }
    return chip;
}

static inline AromaNode* aroma_ui_chip_with_icon(
    AromaNode* parent,
    const char* label,
    int x, int y,
    AromaChipType type,
    void (*callback)(void*),
    void* user_data,
    AromaFont* font,
    const char* icon_code,
    AromaFont* icon_font
) {
    AromaNode* chip = aroma_ui_chip(parent, label, x, y, type, callback, user_data, font);
    if (chip && icon_code && icon_font) {
        aroma_chip_set_icon(chip, icon_code, icon_font);
    }
    return chip;
}

/**
 * @brief Create a card helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param type Card type (e.g. ELEVATED, OUTLINED).
 * @return Pointer to the new card node.
 */
static inline AromaNode* aroma_ui_card(
    AromaNode* parent,
    int x, int y, int width, int height,
    AromaCardType type
) {
    return aroma_card_create(parent, x, y, width, height, type);
}

/**
 * @brief Create a progress bar helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param type Progress bar type (e.g. DETERMINATE, INDETERMINATE).
 * @param progress Initial progress (0.0 to 1.0).
 * @return Pointer to the new progress bar node.
 */
static inline AromaNode* aroma_ui_progressbar(
    AromaNode* parent,
    int x, int y, int width, int height,
    AromaProgressType type,
    float progress
) {
    AromaNode* pb = aroma_progressbar_create(parent, x, y, width, height, type);
    if (pb) {
        aroma_progressbar_set_progress(pb, progress);
    }
    return pb;
}


/**
 * @brief Create a divider helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param length Length of the divider (width for horizontal, height for vertical).
 * @param orientation HORIZONTAL or VERTICAL.
 * @return Pointer to the new divider node.
 */
static inline AromaNode* aroma_ui_divider(
    AromaNode* parent,
    int x, int y, int length,
    AromaDividerOrientation orientation
) {
    return aroma_divider_create(parent, x, y, length, orientation);
}

/**
 * @brief Create a list view helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param callback Callback when an item is clicked.
 * @param user_data User data.
 * @param font Font to use for items.
 * @return Pointer to the new list view node.
 */
static inline AromaNode* aroma_ui_listview(
    AromaNode* parent,
    int x, int y, int width, int height,
    void (*callback)(int, void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* lv = aroma_listview_create(parent, x, y, width, height);
    if (lv) {
        if (callback) aroma_listview_set_callback(lv, callback, user_data);
        if (font) aroma_listview_set_font(lv, font);
    }
    return lv;
}

/**
 * @brief Create a menu helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param font Font to use for menu items.
 * @return Pointer to the new menu node.
 */
static inline AromaNode* aroma_ui_menu(
    AromaNode* parent,
    int x, int y,
    AromaFont* font
) {
    AromaNode* menu = aroma_menu_create(parent, x, y);
    if (menu && font) {
        aroma_menu_set_font(menu, font);
    }
    return menu;
}

/**
 * @brief Create a dialog helper.
 * 
 * @param parent Parent node.
 * @param title Dialog title.
 * @param message Dialog message content.
 * @param width Dialog width.
 * @param height Dialog height.
 * @param type Dialog type (e.g. ALERT, CONFIRM).
 * @param font Font to use.
 * @return Pointer to the new dialog node.
 */
static inline AromaNode* aroma_ui_dialog(
    AromaNode* parent,
    const char* title,
    const char* message,
    int width, int height,
    AromaDialogType type,
    AromaFont* font
) {
    AromaNode* dlg = aroma_dialog_create(parent, title, message, width, height, type);
    if (dlg && font) {
        aroma_dialog_set_font(dlg, font);
    }
    return dlg;
}

/**
 * @brief Create a tabs helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param labels Array of tab labels.
 * @param count Number of tabs.
 * @param on_change Callback when active tab changes.
 * @param user_data User data.
 * @param font Font to use.
 * @return Pointer to the new tabs node.
 */
static inline AromaNode* aroma_ui_tabs(
    AromaNode* parent,
    int x, int y, int width, int height,
    const char** labels, int count,
    void (*on_change)(AromaNode*, int, void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* tabs = aroma_tabs_create(parent, x, y, width, height, labels, count);
    if (tabs) {
        if (on_change) aroma_tabs_set_on_change(tabs, on_change, user_data);
        if (font) aroma_tabs_set_font(tabs, font);
        aroma_tabs_setup_events(tabs, aroma_ui_request_redraw, NULL);
    }
    return tabs;
}

static inline AromaNode* aroma_ui_tabs_with_icons(
    AromaNode* parent,
    int x, int y, int width, int height,
    const char** labels, 
    const char** icons,
    int count,
    void (*on_change)(AromaNode*, int, void*),
    void* user_data,
    AromaFont* font,
    AromaFont* icon_font
) {
    AromaNode* tabs = aroma_ui_tabs(parent, x, y, width, height, labels, count, on_change, user_data, font);
    if (tabs && icons && icon_font) {
        for (int i = 0; i < count; i++) {
            if (icons[i]) {
                aroma_tabs_set_icon(tabs, i, icons[i], icon_font);
            }
        }
    }
    return tabs;
}

/**
 * @brief Create a snackbar helper.
 * 
 * @param parent Parent node (usually the root window).
 * @param message Message to display.
 * @param duration_ms Duration in milliseconds.
 * @param font Font to use.
 * @return Pointer to the new snackbar node.
 */
static inline AromaNode* aroma_ui_snackbar(
    AromaNode* parent,
    const char* message,
    int duration_ms,
    AromaFont* font
) {
    AromaNode* snk = aroma_snackbar_create(parent, message, duration_ms);
    if (snk && font) {
        aroma_snackbar_set_font(snk, font);
    }
    return snk;
}

/**
 * @brief Create a tooltip helper.
 * 
 * @param parent Parent node (the node the tooltip is attached to).
 * @param text Tooltip text.
 * @param x X-coordinate relative to screen/window.
 * @param y Y-coordinate relative to screen/window.
 * @param pos Preferred position (e.g. TOP, BOTTOM).
 * @param font Font to use.
 * @return Pointer to the new tooltip node.
 */
static inline AromaNode* aroma_ui_tooltip(
    AromaNode* parent,
    const char* text,
    int x, int y,
    AromaTooltipPosition pos,
    AromaFont* font
) {
    AromaNode* tt = aroma_tooltip_create(parent, text, x, y, pos);
    if (tt && font) {
        aroma_tooltip_set_font(tt, font);
    }
    return tt;
}


/**
 * @brief Create a sidebar helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Sidebar width.
 * @param height Sidebar height.
 * @param labels Array of item labels.
 * @param count Number of items.
 * @param on_select Callback when an item is selected.
 * @param user_data User data.
 * @param font Font to use.
 * @return Pointer to the new sidebar node.
 */
static inline AromaNode* aroma_ui_sidebar(
    AromaNode* parent,
    int x, int y, int width, int height,
    const char** labels, int count,
    void (*on_select)(AromaNode*, int, void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* sb = aroma_sidebar_create(parent, x, y, width, height, labels, count);
    if (sb) {
        if (on_select) aroma_sidebar_set_on_select(sb, on_select, user_data);
        if (font) aroma_sidebar_set_font(sb, font);
        aroma_sidebar_setup_events(sb, aroma_ui_request_redraw, NULL);
    }
    return sb;
}

static inline AromaNode* aroma_ui_sidebar_with_icons(
    AromaNode* parent,
    int x, int y, int width, int height,
    const char** labels,
    const char** icons,
    int count,
    void (*on_select)(AromaNode*, int, void*),
    void* user_data,
    AromaFont* font,
    AromaFont* icon_font
) {
    AromaNode* sb = aroma_ui_sidebar(parent, x, y, width, height, labels, count, on_select, user_data, font);
    if (sb && icons && icon_font) {
        for (int i = 0; i < count; i++) {
            if (icons[i]) {
                aroma_sidebar_set_icon(sb, i, icons[i], icon_font);
            }
        }
    }
    return sb;
}

/**
 * @brief Enable or disable the startup splash screen.
 * @param enabled True to show splash, false to disable.
 */
void aroma_splash(bool enabled);

/**
 * @brief Create a dropdown helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width.
 * @param height Height.
 * @param options Array of option strings.
 * @param option_count Number of options.
 * @param on_selection_changed Callback when selection changes.
 * @param user_data User data.
 * @param font Font to use.
 * @return Pointer to the new dropdown node.
 */
static inline AromaNode* aroma_ui_dropdown(
    AromaNode* parent,
    int x, int y, int width, int height,
    char** options,
    int option_count,
    void (*on_selection_changed)(int, const char*, void*),
    void* user_data,
    AromaFont* font
) {
    AromaNode* dd = aroma_dropdown_create(parent, x, y, width, height);
    if (dd) {
        if (options && option_count > 0) {
            for (int i = 0; i < option_count; i++) {
                aroma_dropdown_add_option(dd, options[i]);
            }
        }
        if (on_selection_changed) aroma_dropdown_set_on_change(dd, on_selection_changed, user_data);
        if (font) aroma_dropdown_set_font(dd, font);
        aroma_dropdown_setup_events(dd, aroma_ui_request_redraw, NULL);
    }
    return dd;
}

/**
 * @brief Create a debug overlay helper.
 * 
 * @param parent Parent node.
 * @param x X-coordinate.
 * @param y Y-coordinate.
 * @param width Width of the overlay.
 * @param font Font to use for debug text.
 * @return Pointer to the new debug overlay node.
 */
static inline AromaNode* aroma_ui_debug_overlay(
    AromaNode* parent,
    int x, int y, int width,
    AromaFont* font
) {
    AromaNode* overlay = aroma_debug_overlay_create(parent, x, y, width);
    if (overlay && font) {
        aroma_debug_overlay_set_font(overlay, font);
    }
    return overlay;
}

/**
 * @brief Create a generic top-level window (not part of the node tree structure usually).
 * 
 * @param title Window title.
 * @param width Window width.
 * @param height Window height.
 * @param fullscreen True to request fullscreen mode.
 * @return Pointer to the new window node.
 */
static inline AromaNode* aroma_ui_window(
    const char* title,
    int width, int height,
    bool fullscreen
) {
    AromaNode* win = aroma_window_create(title, 0, 0, width, height);
    if (win && fullscreen) {
        aroma_window_set_fullscreen(win, true);
    }
    return win;
}


#ifdef __ANDROID__
// Forward struct for Android App state
struct android_app; 

/**
 * @brief Set the Android app state for JNI interfacing.
 * @param state Pointer to the android_app struct.
 */
void aroma_android_set_app(struct android_app* state);
#endif


#ifdef __cplusplus
}
#endif
#endif
