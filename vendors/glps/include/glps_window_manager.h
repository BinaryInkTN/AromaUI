/**
 * @file glps_window_manager.h
 * @brief Header file for the GLPS Window Manager.
 *
 * This module provides an abstraction for creating and managing windows,
 * handling rendering contexts, input events, clipboard, drag & drop, and
 * platform-specific operations for OpenGL and Vulkan.
 */

#ifndef GLPS_WINDOW_MANAGER_H
#define GLPS_WINDOW_MANAGER_H

#include "glps_common.h"

/**
 * @brief Initializes the GLPS Window Manager.
 *
 * @return Pointer to the initialized GLPS Window Manager instance.
 */
glps_WindowManager *glps_wm_init(void);

/**
 * @brief Retrieves the platform identifier used by GLPS.
 *
 * @return Platform ID as a uint8_t.
 */
uint8_t glps_wm_get_platform(void);

/**
 * @brief Gets the native window pointer for a given window.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 * @return Pointer to the native window object (platform-specific).
 */
void *glps_wm_window_get_native_ptr(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Creates a new window.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param title Title of the new window.
 * @param x x of the window in pixels.
 * @param y y of the window in pixels.
 * @param width Width of the window in pixels.
 * @param height Height of the window in pixels.
 * @return ID of the created window.
 */
size_t glps_wm_window_create(glps_WindowManager *wm, const char *title,
                             int x, int y, int width, int height);

/**
 * @brief Sets whether a window is resizable.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param state True if window should be resizable, false otherwise.
 * @param window_id ID of the window.
 */
void glps_wm_window_is_resizable(glps_WindowManager *wm, bool state, size_t window_id);

/**
 * @brief Retrieves the dimensions of a window.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 * @param width Pointer to store window width.
 * @param height Pointer to store window height.
 */
void glps_wm_window_get_dimensions(glps_WindowManager *wm, size_t window_id,
                                   int *width, int *height);

/**
 * @brief Sets a callback for window resize events.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_resize_callback Function called on resize.
 * @param data User data passed to the callback.
 */
void glps_wm_window_set_resize_callback(
    glps_WindowManager *wm,
    void (*window_resize_callback)(size_t window_id, int width, int height,
                                   void *data),
    void *data);

/**
 * @brief Sets a callback for frame updates.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_frame_update_callback Function called each frame.
 * @param data User data passed to the callback.
 */
void glps_wm_window_set_frame_update_callback(
    glps_WindowManager *wm,
    void (*window_frame_update_callback)(size_t window_id, void *data),
    void *data);

/**
 * @brief Enables or disables window blur.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 * @param enable True to enable blur, false to disable.
 * @param blur_radius Blur radius in pixels.
 */
void glps_wm_set_window_blur(glps_WindowManager *wm, size_t window_id, bool enable, int blur_radius);

/**
 * @brief Sets window opacity.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 * @param opacity Opacity value (0.0 to 1.0).
 */
void glps_wm_set_window_opacity(glps_WindowManager *wm, size_t window_id, float opacity);

/**
 * @brief Makes the window background transparent.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 */
void glps_wm_set_window_background_transparent(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Sets a callback for window close events.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_close_callback Function called when window closes.
 * @param data User data passed to the callback.
 */
void glps_wm_window_set_close_callback(
    glps_WindowManager *wm,
    void (*window_close_callback)(size_t window_id, void *data), void *data);

/**
 * @brief Sets the OpenGL context of a window as the current context.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 */
void glps_wm_set_window_ctx_curr(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Swaps the front and back buffers for a window.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 */
void glps_wm_swap_buffers(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Sets the swap interval for buffer swaps.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param swap_interval Number of vertical refreshes between swaps.
 */
void glps_wm_swap_interval(glps_WindowManager *wm, unsigned int swap_interval);
/**
 * @brief Updates a window (polls events, refreshes).
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 */
void glps_wm_window_update(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Destroys a window.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @param window_id ID of the window.
 */
void glps_wm_window_destroy(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Cleans up and destroys the GLPS Window Manager.
 *
 * @param wm Pointer to the GLPS Window Manager.
 */
void glps_wm_destroy(glps_WindowManager *wm);

/**
 * @brief Returns the total number of windows.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @return Total window count.
 */
size_t glps_wm_get_window_count(glps_WindowManager *wm);
ssize_t glps_wm_window_create_ex(
    glps_WindowManager *wm,
    const char *title,
    int x, int y,
    int width, int height,
    GLPS_WindowFlags flags);

void glps_wm_move_window(
    glps_WindowManager *wm,
    size_t window_id,
    int x, int y
);
/**
 * @brief Checks if any window should close.
 *
 * @param wm Pointer to the GLPS Window Manager.
 * @return True if the window should close, false otherwise.
 */
bool glps_wm_should_close(glps_WindowManager *wm);

/* ======= Keyboard Events ======= */

/**
 * @brief Sets the callback for keyboard focus gain events.
 */
void glps_wm_set_keyboard_enter_callback(
    glps_WindowManager *wm,
    void (*keyboard_enter_callback)(size_t window_id, void *data), void *data);

/**
 * @brief Sets the callback for keyboard focus loss events.
 */
void glps_wm_set_keyboard_leave_callback(
    glps_WindowManager *wm,
    void (*keyboard_leave_callback)(size_t window_id, void *data), void *data);

/**
 * @brief Sets the callback for key press/release events.
 *
 * @note Some keys may not return proper values on certain platforms.
 */
void glps_wm_set_keyboard_callback(glps_WindowManager *wm,
                                   void (*keyboard_callback)(size_t window_id,
                                                             bool state,
                                                             const char *value,
                                                             unsigned long keycode,
                                                             void *data),
                                   void *data);

/* ======= Mouse/Trackpad Events ======= */

/**
 * @brief Sets the callback for mouse enter events.
 */
void glps_wm_set_mouse_enter_callback(
    glps_WindowManager *wm,
    void (*mouse_enter_callback)(size_t window_id, double mouse_x,
                                 double mouse_y, void *data),
    void *data);

/**
 * @brief Sets the callback for mouse leave events.
 */
void glps_wm_set_mouse_leave_callback(
    glps_WindowManager *wm,
    void (*mouse_leave_callback)(size_t window_id, void *data), void *data);

/**
 * @brief Sets the callback for mouse movement events.
 */
void glps_wm_set_mouse_move_callback(
    glps_WindowManager *wm,
    void (*mouse_move_callback)(size_t window_id, double mouse_x,
                                double mouse_y, void *data),
    void *data);

/**
 * @brief Sets the callback for mouse button events.
 */
void glps_wm_set_mouse_click_callback(
    glps_WindowManager *wm,
    void (*mouse_click_callback)(size_t window_id, bool state, void *data),
    void *data);

/**
 * @brief Sets the callback for mouse scroll events.
 */
void glps_wm_set_scroll_callback(
    glps_WindowManager *wm,
    void (*mouse_scroll_callback)(size_t window_id, GLPS_SCROLL_AXES axe,
                                  GLPS_SCROLL_SOURCE source, double value,
                                  int discrete, bool is_stopped, void *data),
    void *data);

/* ======= Touchscreen Events ======= */

/**
 * @brief Sets the callback for touch input events.
 */
void glps_wm_set_touch_callback(
    glps_WindowManager *wm,
    void (*touch_callback)(size_t window_id, int id, double touch_x,
                           double touch_y, bool state, double major,
                           double minor, double orientation, void *data),
    void *data);

/* ======= Clipboard ======= */

/**
 * @brief Attaches data to the clipboard.
 */
void glps_wm_attach_to_clipboard(glps_WindowManager *wm, char *mime,
                                 char *data);

/**
 * @brief Retrieves data from the clipboard.
 */
void glps_wm_get_from_clipboard(glps_WindowManager *wm, char *data,
                                size_t data_size);

/**
 * @brief Changes the mouse cursor type.
 */
void glps_wm_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE cursor_type);

/* ======= Drag & Drop ======= */

/**
 * @brief Starts a drag & drop operation.
 */
void glps_wm_start_drag_n_drop(
    glps_WindowManager *wm, size_t origin_window_id,
    void (*drag_n_drop_callback)(size_t origin_window_id, char *mime,
                                 char *buff, int x, int y, void *data),
    void *data);

/* ======= Utilities ======= */

/**
 * @brief Returns the FPS of a window.
 */
double glps_wm_get_fps(glps_WindowManager *wm, size_t window_id);

/**
 * @brief Returns the address of an OpenGL/Vulkan procedure.
 */
void *glps_get_proc_addr(const char *name);
/**
 * @brief Returns the X11 Display pointer.
 */
void *glps_wm_get_display(glps_WindowManager *wm);

#ifdef GLPS_USE_VULKAN

/**
 * @brief Creates a Vulkan surface for a window.
 */
void glps_wm_vk_create_surface(glps_WindowManager *wm, size_t window_id, VkInstance *instance, VkSurfaceKHR *surface);

/**
 * @brief Retrieves available Vulkan instance extensions with proper memory management
 *
 * @return glps_VulkanExtensionArray Array of extension names. Caller must free with glps_wm_vk_free_extensions_arr()
 *
 * @note This function is thread-safe as Vulkan enumeration functions are required to be thread-safe
 * @note Returns empty array (count=0, names=NULL) on failure
 */
glps_VulkanExtensionArray glps_wm_vk_get_extensions_arr(void);
/**
 * @brief Frees memory allocated by glps_wm_vk_get_extensions_arr()
 *
 * @param extensions Pointer to extensions array to free
 *
 * @note Safe to call with NULL or empty array
 * @note After calling, the extensions array should not be used
 */
void glps_wm_vk_free_extensions_arr(glps_VulkanExtensionArray *extensions);
/**
 * @brief Checks if required extensions are available
 *
 * @param required_extensions Array of required extension names
 * @param required_count Number of required extensions
 * @param missing_extensions Output array for missing extensions (optional, can be NULL)
 * @param missing_count Output count of missing extensions (optional, can be NULL)
 *
 * @return true if all required extensions are available, false otherwise
 *
 * @note If missing_extensions is provided, it must be at least required_count in size
 * @note Caller is responsible for allocating missing_extensions array if used
 */
bool glps_wm_vk_check_extensions_supported(
    const char **required_extensions,
    uint32_t required_count,
    const char **missing_extensions,
    uint32_t *missing_count);
#endif

/**
 * @brief Toggles window decorations (titlebar, borders, etc.).
 */
void glps_wm_toggle_window_decorations(glps_WindowManager *wm, bool state, size_t window_id);

#endif // GLPS_WINDOW_MANAGER_H
