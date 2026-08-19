#ifndef GLPS_X11_H
#define GLPS_X11_H

#include "glps_common.h"

void glps_x11_init(glps_WindowManager *wm);

ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int x, int y, int width, int height);

void glps_x11_destroy(glps_WindowManager *wm);
void glps_x11_get_window_dimensions(glps_WindowManager *wm, size_t window_id,
                                    int *width, int *height);

void glps_x11_attach_to_clipboard(glps_WindowManager *wm, char *mime,
                                  char *data);

void glps_x11_get_from_clipboard(glps_WindowManager *wm, char *data,
                                 size_t data_size);

bool glps_x11_should_close(glps_WindowManager *wm);
void glps_x11_window_update(glps_WindowManager *wm, size_t window_id);
void glps_x11_window_is_resizable(glps_WindowManager *wm, bool state, size_t window_id);
void glps_x11_toggle_window_decorations(glps_WindowManager *wm, bool state, size_t window_id);
void glps_x11_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE user_cursor);
void glps_x11_set_window_blur(glps_WindowManager *wm, size_t window_id, bool enable, int blur_radius);
void glps_x11_set_window_opacity(glps_WindowManager *wm, size_t window_id, float opacity);
void glps_x11_set_window_background_transparent(glps_WindowManager *wm, size_t window_id);
Display *glps_x11_get_display(glps_WindowManager *wm);
ssize_t glps_x11_window_create_ex(
    glps_WindowManager *wm,
    const char *title,
    int x, int y,
    int width, int height,
    GLPS_WindowFlags flags
);
void glps_x11_move_window(
    glps_WindowManager *wm,
    size_t window_id,
    int x, int y
);
#ifdef GLPS_USE_VULKAN
void glps_x11_vk_create_surface(glps_WindowManager *wm, size_t window_id, VkInstance *instance, VkSurfaceKHR *surface);
#endif

#endif