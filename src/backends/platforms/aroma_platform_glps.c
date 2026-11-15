#ifndef ESP32
#include "aroma_platform_interface.h"
#ifdef AROMA_HAS_VULKAN
#include <vulkan/vulkan.h>
#ifndef GLPS_USE_VULKAN
#define GLPS_USE_VULKAN
#endif
#endif
#include "glps_window_manager.h"
#include <stdbool.h>
#include <ctype.h>
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"
#ifdef GLPS_USE_X11
#include <X11/Xlib.h>
#endif

typedef struct
{
    glps_WindowManager *wm;
    size_t primary_window_id;
    bool has_primary_window;
    double last_mouse_x[AROMA_MAX_WINDOWS];
    double last_mouse_y[AROMA_MAX_WINDOWS];
    bool mouse_button_down[AROMA_MAX_WINDOWS];
    uint8_t mouse_button[AROMA_MAX_WINDOWS];
    bool capslock_active;
    bool ctrl_active;
    bool shift_active;
    bool frame_rendered;
    bool glps_initialized;
    void (*frame_update_callback)(size_t window_id, void *data);
    void *frame_update_data;


    bool window_closed;
    bool window_dragging;
    int drag_start_root_x;
    int drag_start_root_y;
    int drag_window_x;
    int drag_window_y;
    bool window_maximized;
    int restore_x;
    int restore_y;
    int restore_width;
    int restore_height;
} AromaGLPSContext;

static AromaGLPSContext platform_ctx = (AromaGLPSContext){
    .wm = NULL,
    .primary_window_id = 0,
    .has_primary_window = false,
    .capslock_active = false,
    .ctrl_active = false,
    .shift_active = false,
    .frame_rendered = false,
    .glps_initialized = false,
    .frame_update_callback = NULL,
    .frame_update_data = NULL,
    .window_closed = false};

static void shift_window_input_state(size_t removed_id);
static void glps_set_window_maximized(size_t window_id, bool maximized);

static void glps_window_close_callback(size_t window_id, void *data)
{
    (void)data;

    /* Aroma owns node/layout state for each native window.  Remove only the
     * matching root here; GLPS removes its native window immediately after
     * this callback returns. */
    aroma_ui_handle_native_window_close(window_id);
    shift_window_input_state(window_id);
}

static void glps_activate_window(size_t window_id)
{
    if (platform_ctx.wm &&
        window_id < glps_wm_get_window_count(platform_ctx.wm))
    {
        aroma_ui_activate_window(window_id);
    }
}

static double window_mouse_x(size_t window_id)
{
    return window_id < AROMA_MAX_WINDOWS ? platform_ctx.last_mouse_x[window_id]
                                         : 0.0;
}

static double window_mouse_y(size_t window_id)
{
    return window_id < AROMA_MAX_WINDOWS ? platform_ctx.last_mouse_y[window_id]
                                         : 0.0;
}

static bool window_mouse_down(size_t window_id)
{
    return window_id < AROMA_MAX_WINDOWS &&
           platform_ctx.mouse_button_down[window_id];
}

static void shift_window_input_state(size_t removed_id)
{
    if (removed_id >= AROMA_MAX_WINDOWS)
        return;
    for (size_t i = removed_id; i + 1 < AROMA_MAX_WINDOWS; i++)
    {
        platform_ctx.last_mouse_x[i] = platform_ctx.last_mouse_x[i + 1];
        platform_ctx.last_mouse_y[i] = platform_ctx.last_mouse_y[i + 1];
        platform_ctx.mouse_button_down[i] =
            platform_ctx.mouse_button_down[i + 1];
    }
    platform_ctx.last_mouse_x[AROMA_MAX_WINDOWS - 1] = 0.0;
    platform_ctx.last_mouse_y[AROMA_MAX_WINDOWS - 1] = 0.0;
    platform_ctx.mouse_button_down[AROMA_MAX_WINDOWS - 1] = false;
}

static bool queue_mouse_event(size_t window_id, AromaEventType type,
                              double mouse_x, double mouse_y, uint8_t button)
{
    AromaNode *root = aroma_ui_window_root_for_id(window_id);
    if (!root)
    {
        root = aroma_event_get_root();
    }
    if (!root)
    {
        return false;
    }

    AromaNode *target = aroma_event_hit_test(root, (int)mouse_x, (int)mouse_y);
    uint64_t target_id = target ? target->node_id : root->node_id;

    AromaEvent *event = aroma_event_create_mouse(type, target_id, (int)mouse_x, (int)mouse_y, button);
    if (!event)
    {
        return false;
    }

    event->data.mouse.delta_x = (int)(mouse_x - window_mouse_x(window_id));
    event->data.mouse.delta_y = (int)(mouse_y - window_mouse_y(window_id));

    return aroma_event_queue(event);
}

static bool queue_key_event(AromaEventType type, uint32_t key_value, uint16_t modifiers)
{
    AromaNode *root = aroma_event_get_root();
    if (!root)
    {
        return false;
    }

    AromaNode *target = aroma_ui_get_focused_node();
    if (!target)
    {
        target = root;
    }

    AromaEvent *event = aroma_event_create_key(type, target->node_id, key_value, modifiers);
    if (!event)
    {
        return false;
    }

    return aroma_event_queue(event);
}

static void glps_mouse_move_callback(size_t window_id, double mouse_x, double mouse_y, void *data)
{
    (void)data;
    glps_activate_window(window_id);
#ifdef GLPS_USE_X11
    if (platform_ctx.window_dragging && window_id == platform_ctx.primary_window_id)
    {
        Display *display = (Display *)glps_wm_get_display(platform_ctx.wm);
        Window native_window = (Window)glps_wm_window_get_native_ptr(
            platform_ctx.wm, window_id);
        Window child;
        int root_x;
        int root_y;
        int win_x;
        int win_y;
        unsigned int mask;
        if (display && native_window &&
            XQueryPointer(display, native_window, &child, &child, &root_x,
                          &root_y, &win_x, &win_y, &mask))
        {
            XMoveWindow(display, native_window,
                        platform_ctx.drag_window_x +
                            root_x - platform_ctx.drag_start_root_x,
                        platform_ctx.drag_window_y +
                            root_y - platform_ctx.drag_start_root_y);
            XFlush(display);
            if (root_y <= 2 && !platform_ctx.window_maximized)
                glps_set_window_maximized(window_id, true);
        }
    }
#else
    (void)mouse_x;
    (void)mouse_y;
#endif
    bool moved = (mouse_x != window_mouse_x(window_id)) ||
                 (mouse_y != window_mouse_y(window_id));
    if (moved)
    {
        queue_mouse_event(window_id, EVENT_TYPE_MOUSE_MOVE, mouse_x, mouse_y, 0);
    }

    aroma_event_handle_pointer_move((int)mouse_x, (int)mouse_y,
                                    window_mouse_down(window_id));

    if (window_id < AROMA_MAX_WINDOWS)
    {
        platform_ctx.last_mouse_x[window_id] = mouse_x;
        platform_ctx.last_mouse_y[window_id] = mouse_y;
    }
}

static uint8_t glps_pointer_button(size_t window_id, bool state)
{
#ifdef GLPS_USE_X11
    if (platform_ctx.wm)
    {
        Display *display = (Display *)glps_wm_get_display(platform_ctx.wm);
        Window native_window = (Window)glps_wm_window_get_native_ptr(
            platform_ctx.wm, window_id);
        Window root;
        Window child;
        int root_x;
        int root_y;
        int win_x;
        int win_y;
        unsigned int mask;
        if (display && native_window &&
            XQueryPointer(display, native_window, &root, &child, &root_x,
                          &root_y, &win_x, &win_y, &mask))
        {
            if (state)
            {
                if (mask & Button3Mask)
                    return 3;
                if (mask & Button2Mask)
                    return 2;
                if (mask & Button1Mask)
                    return 1;
            }
            else if (window_id < AROMA_MAX_WINDOWS)
                return platform_ctx.mouse_button[window_id];
        }
    }
#else
    (void)window_id;
    (void)state;
#endif
    return 1;
}

static void glps_mouse_click_callback(size_t window_id, bool state, void *data)
{
    (void)data;
    glps_activate_window(window_id);

    if (window_id < AROMA_MAX_WINDOWS)
    {
        platform_ctx.mouse_button_down[window_id] = state;
        if (state)
            platform_ctx.mouse_button[window_id] =
                glps_pointer_button(window_id, true);
    }
    AromaEventType type = state ? EVENT_TYPE_MOUSE_CLICK : EVENT_TYPE_MOUSE_RELEASE;
    queue_mouse_event(window_id, type, window_mouse_x(window_id),
                      window_mouse_y(window_id),
                      window_id < AROMA_MAX_WINDOWS
                          ? platform_ctx.mouse_button[window_id]
                          : 1);

    if (!state)
    {
        platform_ctx.window_dragging = false;
        aroma_event_handle_pointer_move((int)window_mouse_x(window_id),
                                        (int)window_mouse_y(window_id), false);
    }
}
static void glps_keyboard_callback(size_t window_id, bool state, const char *value,
                                   unsigned long keycode, void *data)
{
    (void)data;
    glps_activate_window(window_id);

    if (!state)
    {
        if (keycode == 37 || keycode == 105)
        {
            platform_ctx.ctrl_active = false;
        }
        if (keycode == 50 || keycode == 62)
        {
            platform_ctx.shift_active = false;
        }
        return;
    }

    if (keycode == 0xFFE5 || keycode == 66)
    {
        platform_ctx.capslock_active = !platform_ctx.capslock_active;

        uint16_t modifiers = 0;
        if (platform_ctx.capslock_active)
            modifiers |= AROMA_KEY_MOD_CAPSLOCK;
        if (platform_ctx.ctrl_active)
            modifiers |= AROMA_KEY_MOD_CTRL;

        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFFE5, modifiers);
        return;
    }

    if (keycode == 37 || keycode == 105)
    {
        platform_ctx.ctrl_active = true;
        return;
    }

    if (keycode == 50 || keycode == 62)
    {
        platform_ctx.shift_active = true;
        return;
    }

    uint16_t modifiers = 0;
    if (platform_ctx.capslock_active)
        modifiers |= AROMA_KEY_MOD_CAPSLOCK;
    if (platform_ctx.ctrl_active)
        modifiers |= AROMA_KEY_MOD_CTRL;
    if (platform_ctx.shift_active)
        modifiers |= AROMA_KEY_MOD_SHIFT;

    if (value && value[0] != '\0')
    {
        if (strcmp(value, "Escape") == 0)
        {

            if (aroma_ui_handle_back_press())
                return;
            queue_key_event(EVENT_TYPE_KEY_PRESS, 27, modifiers);
            return;
        }
        if (strcmp(value, "Left") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF51, modifiers);
            return;
        }
        if (strcmp(value, "Right") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF53, modifiers);
            return;
        }
        if (strcmp(value, "Up") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF52, modifiers);
            return;
        }
        if (strcmp(value, "Down") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF54, modifiers);
            return;
        }
        if (strcmp(value, "Home") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF50, modifiers);
            return;
        }
        if (strcmp(value, "End") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF57, modifiers);
            return;
        }
        if (strcmp(value, "Delete") == 0 || strcmp(value, "Del") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFFFF, modifiers);
            return;
        }
        if (strcmp(value, "Backspace") == 0)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 8, modifiers);
            return;
        }
        if (strcmp(value, "Return") == 0 || strcmp(value, "Enter") == 0 || keycode == 0xFF0D)
        {
            queue_key_event(EVENT_TYPE_KEY_PRESS, 10, modifiers);
            return;
        }

        /* Wayland reports non-printable keys by their xkb keysym name (the
         * keycode argument is an evdev scancode, not a keysym), so map the
         * names used by the event API. */
        static const struct
        {
            const char *name;
            uint32_t key;
        } named_keys[] = {
            {"Page_Up", 0xFF55},   {"PageUp", 0xFF55},
            {"Prior", 0xFF55},     {"Page_Down", 0xFF56},
            {"PageDown", 0xFF56},  {"Next", 0xFF56},
            {"F1", 0xFFBE},        {"F2", 0xFFBF},
            {"F3", 0xFFC0},        {"F4", 0xFFC1},
            {"F5", 0xFFC2},        {"F6", 0xFFC3},
            {"F7", 0xFFC4},        {"F8", 0xFFC5},
            {"F9", 0xFFC6},        {"F10", 0xFFC7},
            {"F11", 0xFFC8},       {"F12", 0xFFC9},
            {"Tab", 9},            {"ISO_Left_Tab", 9},
            {"BackSpace", 8},      {"KP_Enter", 10},
            {"Insert", 0xFF63},    {"KP_Insert", 0xFF63},
            {"KP_Delete", 0xFFFF}, {"KP_Home", 0xFF50},
            {"KP_End", 0xFF57},    {"KP_Left", 0xFF51},
            {"KP_Right", 0xFF53},  {"KP_Up", 0xFF52},
            {"KP_Down", 0xFF54},   {"KP_Prior", 0xFF55},
            {"KP_Page_Up", 0xFF55}, {"KP_Next", 0xFF56},
            {"KP_Page_Down", 0xFF56},
        };
        for (size_t i = 0; i < sizeof(named_keys) / sizeof(named_keys[0]); i++)
        {
            if (strcmp(value, named_keys[i].name) == 0)
            {
                queue_key_event(EVENT_TYPE_KEY_PRESS, named_keys[i].key,
                                modifiers);
                return;
            }
        }
    }

    /* Fallback: use X11 keycode (KeySym) when string name is empty,
     * which happens for non-printable keys (arrows, etc.) on X11. */
    if (keycode == 0xFF1B || keycode == 9)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 27, modifiers);
        return;
    }
    if (keycode == 0xFF51 || keycode == 113)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF51, modifiers);
        return;
    }
    if (keycode == 0xFF53 || keycode == 114)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF53, modifiers);
        return;
    }
    if (keycode == 0xFF52 || keycode == 111)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF52, modifiers);
        return;
    }
    if (keycode == 0xFF54 || keycode == 116)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF54, modifiers);
        return;
    }
    if (keycode == 0xFF50 || keycode == 110)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF50, modifiers);
        return;
    }
    if (keycode == 0xFF57 || keycode == 115)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFF57, modifiers);
        return;
    }
    if (keycode == 0xFF08 || keycode == 22 || keycode == 8)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 8, modifiers);
        return;
    }
    if (keycode == 0xFFFF || keycode == 119 || keycode == 127)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 0xFFFF, modifiers);
        return;
    }
    /* Return/Enter: X11 reports either the keysym (0xFF0D) or the raw
     * keycode (typically 36) when the printable string is empty. */
    if (keycode == 0xFF0D || keycode == 36)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, 10, modifiers);
        return;
    }

    /* Function keys arrive as X11 keysyms on platforms where the printable
     * string is empty.  Forward them so application-level shortcuts work. */
    if (keycode >= 0xFFBE && keycode <= 0xFFC9)
    {
        queue_key_event(EVENT_TYPE_KEY_PRESS, (uint32_t)keycode, modifiers);
        return;
    }

    if (value && value[0] != '\0')
    {
        uint32_t key_value = (unsigned char)value[0];

        if (strlen(value) == 1)
        {
            if (platform_ctx.capslock_active)
            {
                if (key_value >= 'a' && key_value <= 'z')
                {
                    key_value = key_value - 'a' + 'A';
                }
            }

            if (platform_ctx.ctrl_active)
            {
                if (key_value >= 'a' && key_value <= 'z')
                {
                    key_value = key_value - 'a' + 1;
                }
                else if (key_value >= 'A' && key_value <= 'Z')
                {
                    key_value = key_value - 'A' + 1;
                }
            }

            queue_key_event(EVENT_TYPE_KEY_PRESS, key_value, modifiers);
        }
    }
}

static void glps_scroll_callback(size_t window_id, GLPS_SCROLL_AXES axe,
                                 GLPS_SCROLL_SOURCE source, double value,
                                 int discrete, bool is_stopped, void *data)
{
    (void)source;
    (void)discrete;
    (void)is_stopped;
    (void)data;
    glps_activate_window(window_id);

    if (value == 0)
        return;

    float scroll_x = (axe == GLPS_SCROLL_H_AXIS) ? (float)value : 0.0f;
    float scroll_y = (axe == GLPS_SCROLL_V_AXIS) ? (float)value : 0.0f;

    int mx = (int)window_mouse_x(window_id);
    int my = (int)window_mouse_y(window_id);

    AromaNode *root = aroma_ui_window_root_for_id(window_id);
    if (!root)
        root = aroma_event_get_root();
    if (!root)
        return;

    AromaNode *target = aroma_event_hit_test(root, mx, my);
    uint64_t node_id = target ? target->node_id : root->node_id;

    AromaEvent *ev = aroma_event_create_scroll(node_id, mx, my, scroll_x, scroll_y);
    if (ev)
        aroma_event_queue(ev);
}

static void glps_touch_callback(size_t window_id, int id, double touch_x,
                                double touch_y, bool state, double major,
                                double minor, double orientation, void *data)
{
    (void)id;
    (void)major;
    (void)minor;
    (void)orientation;
    (void)data;
    glps_activate_window(window_id);

    bool moved = (touch_x != window_mouse_x(window_id)) ||
                 (touch_y != window_mouse_y(window_id));
    bool down = window_mouse_down(window_id);

    if (window_id < AROMA_MAX_WINDOWS)
    {
        platform_ctx.last_mouse_x[window_id] = touch_x;
        platform_ctx.last_mouse_y[window_id] = touch_y;
    }

    if (state)
    {
        if (!down)
        {
            if (window_id < AROMA_MAX_WINDOWS)
                platform_ctx.mouse_button_down[window_id] = true;
            queue_mouse_event(window_id, EVENT_TYPE_MOUSE_CLICK, touch_x, touch_y, 0);
            aroma_event_handle_pointer_move((int)touch_x, (int)touch_y, true);
        }
        else if (moved)
        {
            aroma_event_handle_touch(id, (int)touch_x, (int)touch_y, 1);
            queue_mouse_event(window_id, EVENT_TYPE_MOUSE_MOVE, touch_x, touch_y, 0);
            aroma_event_handle_pointer_move((int)touch_x, (int)touch_y, true);
        }
        else
        {
            aroma_event_handle_touch(id, (int)touch_x, (int)touch_y, 1);
        }
    }
    else
    {
        if (window_id < AROMA_MAX_WINDOWS)
            platform_ctx.mouse_button_down[window_id] = false;
        queue_mouse_event(window_id, EVENT_TYPE_MOUSE_RELEASE, touch_x, touch_y, 0);
        aroma_event_handle_pointer_move((int)touch_x, (int)touch_y, false);
    }
}

static bool ensure_glps_initialized(void)
{
    if (platform_ctx.glps_initialized)
        return true;

    platform_ctx.wm = glps_wm_init();
    if (!platform_ctx.wm)
    {
        LOG_CRITICAL("Failed to initialize GLPS' window manager");
        return false;
    }

    glps_wm_set_mouse_move_callback(platform_ctx.wm, glps_mouse_move_callback, NULL);
    glps_wm_set_mouse_click_callback(platform_ctx.wm, glps_mouse_click_callback, NULL);
    glps_wm_set_scroll_callback(platform_ctx.wm, glps_scroll_callback, NULL);
    glps_wm_set_keyboard_callback(platform_ctx.wm, glps_keyboard_callback, NULL);
    glps_wm_set_touch_callback(platform_ctx.wm, glps_touch_callback, NULL);
    glps_wm_window_set_close_callback(platform_ctx.wm, glps_window_close_callback, NULL);

    if (platform_ctx.frame_update_callback)
    {
        glps_wm_window_set_frame_update_callback(platform_ctx.wm,
            platform_ctx.frame_update_callback,
            platform_ctx.frame_update_data);
    }

    platform_ctx.glps_initialized = true;
    platform_ctx.frame_rendered = false;

    return true;
}

int initialize()
{
    platform_ctx.frame_rendered = false;
    return 1;
}

size_t create_window(const char *title, int x, int y, int width, int height)
{
    if (!ensure_glps_initialized())
        return 0;

    platform_ctx.window_closed = false;
    size_t window_id = glps_wm_window_create(platform_ctx.wm, title, x, y, width, height);

    if (!platform_ctx.has_primary_window)
    {
        aroma_backend_abi.get_graphics_interface()->setup_shared_window_resources();
    }

    aroma_backend_abi.get_graphics_interface()->setup_separate_window_resources(window_id);

    if (!platform_ctx.has_primary_window)
    {
        platform_ctx.primary_window_id = window_id;
        platform_ctx.has_primary_window = true;
    }

    return window_id;
}

void make_context_current(size_t window_id)
{
    if (!ensure_glps_initialized())
        return;
    if (platform_ctx.window_closed)
        return;
    if (window_id >= glps_wm_get_window_count(platform_ctx.wm))
    {
        LOG_ERROR("make_context_current: window %zu no longer exists", window_id);
        return;
    }
    glps_wm_set_window_ctx_curr(platform_ctx.wm, window_id);
}

static void destroy_window(size_t window_id)
{
    if (!ensure_glps_initialized())
        return;

    if (window_id >= glps_wm_get_window_count(platform_ctx.wm))
        return;

    glps_wm_window_destroy(platform_ctx.wm, window_id);
    shift_window_input_state(window_id);

    /* GLPS renumbers remaining windows after destruction. */
    platform_ctx.primary_window_id = 0;
    platform_ctx.has_primary_window =
        glps_wm_get_window_count(platform_ctx.wm) > 0;
}

void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    if (!ensure_glps_initialized())
        return;
    glps_wm_window_get_dimensions(platform_ctx.wm, window_id, window_width, window_height);
}

void set_window_update_callback(void (*callback)(size_t window_id, void *data), void *data)
{
    platform_ctx.frame_update_callback = callback;
    platform_ctx.frame_update_data = data;

    if (platform_ctx.glps_initialized && platform_ctx.wm)
    {
        glps_wm_window_set_frame_update_callback(platform_ctx.wm, callback, data);
    }
}

void request_window_update(size_t window_id)
{
    if (platform_ctx.window_closed)
        return;
    if (platform_ctx.frame_update_callback)
    {
        platform_ctx.frame_update_callback(window_id, platform_ctx.frame_update_data);
    }
}

bool run_event_loop()
{
    if (!platform_ctx.wm)
    {
        LOG_ERROR("Window manager not initialized. Cannot run event loop.");
        return false;
    }
    if (platform_ctx.window_closed)
        return false;
    if (glps_wm_should_close(platform_ctx.wm))
        return false;


    return glps_wm_get_window_count(platform_ctx.wm) > 0;
}

void swap_buffers(size_t window_id)
{

    if (platform_ctx.window_closed)
        return;
#ifdef AROMA_HAS_VULKAN

    AromaGraphicsBackendType type = aroma_backend_abi.get_graphics_backend_type
                                        ? aroma_backend_abi.get_graphics_backend_type()
                                        : GRAPHICS_BACKEND_GLES3;
    if (type == GRAPHICS_BACKEND_VULKAN)
    {
        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->graphics_flush)
            gfx->graphics_flush();
        return;
    }
#endif
    glps_wm_swap_buffers(platform_ctx.wm, window_id);
}

void shutdown()
{
    if (!platform_ctx.wm)
    {
        LOG_ERROR("Window manager not initialized. Cannot shutdown.");
        return;
    }

    size_t window_count = glps_wm_get_window_count(platform_ctx.wm);
    if (window_count == 0)
    {
        LOG_WARNING("TODO: Window manager has no active windows; skipping GLPS destroy to avoid shutdown crash.");
        platform_ctx.wm = NULL;
        platform_ctx.primary_window_id = 0;
        platform_ctx.has_primary_window = false;
        platform_ctx.glps_initialized = false;
        platform_ctx.window_closed = false;
        return;
    }

    glps_wm_destroy(platform_ctx.wm);
    platform_ctx.wm = NULL;
    platform_ctx.primary_window_id = 0;
    platform_ctx.has_primary_window = false;
    platform_ctx.glps_initialized = false;
    platform_ctx.window_closed = false;
}

#ifdef AROMA_HAS_VULKAN
static bool glps_create_vulkan_surface(size_t window_id, void *vk_instance, void **vk_surface_out)
{
    if (!platform_ctx.wm || !vk_instance || !vk_surface_out)
        return false;
}

static const char *glps_vulkan_extensions[] = {
    "VK_KHR_surface",
#if defined(GLPS_USE_WAYLAND)
    "VK_KHR_wayland_surface",
#elif defined(GLPS_USE_X11)
    "VK_KHR_xlib_surface",
#endif
};

static const char **glps_get_vulkan_instance_extensions(uint32_t *count_out)
{
    *count_out = sizeof(glps_vulkan_extensions) / sizeof(glps_vulkan_extensions[0]);
    return glps_vulkan_extensions;
}
#endif

static void *glps_get_native_window_ptr(size_t window_id)
{
    if (!ensure_glps_initialized())
        return NULL;
    return glps_wm_window_get_native_ptr(platform_ctx.wm, window_id);
}

static void *glps_get_display()
{
    if (!ensure_glps_initialized())
        return NULL;
    return glps_wm_get_display(platform_ctx.wm);
}

static void glps_set_clipboard_text(const char *text)
{
    if (!platform_ctx.wm || !text)
        return;
    glps_wm_attach_to_clipboard(platform_ctx.wm, "text/plain", (char *)text);
}

static bool glps_get_clipboard_text(char *buffer, size_t buffer_size)
{
    if (!platform_ctx.wm || !buffer || buffer_size == 0)
        return false;
    glps_wm_get_from_clipboard(platform_ctx.wm, buffer, buffer_size);
    return buffer[0] != '\0';
}

static void glps_set_window_opacity(size_t window_id, float opacity)
{
    if (!platform_ctx.wm)
        return;
    glps_wm_set_window_opacity(platform_ctx.wm, window_id, opacity);
}

static void glps_set_window_decorated(size_t window_id, bool decorated)
{
    if (!platform_ctx.wm)
        return;
#ifdef GLPS_USE_X11
    Display *display = (Display *)glps_wm_get_display(platform_ctx.wm);
    Window native_window = (Window)glps_wm_window_get_native_ptr(
        platform_ctx.wm, window_id);
    if (display && native_window)
    {
        Atom hints_atom = XInternAtom(display, "_MOTIF_WM_HINTS", False);
        struct
        {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long input_mode;
            unsigned long status;
        } hints = {2, 0, decorated ? 1UL : 0UL, 0, 0};
        XChangeProperty(display, native_window, hints_atom, hints_atom, 32,
                        PropModeReplace, (unsigned char *)&hints, 5);
        XFlush(display);
        return;
    }
#endif
    glps_wm_toggle_window_decorations(platform_ctx.wm, decorated, window_id);
}

static void glps_begin_window_drag(size_t window_id)
{
    if (!platform_ctx.wm)
        return;
    platform_ctx.window_dragging = true;
#ifdef GLPS_USE_X11
    Display *display = (Display *)glps_wm_get_display(platform_ctx.wm);
    Window native_window = (Window)glps_wm_window_get_native_ptr(
        platform_ctx.wm, window_id);
    if (display && native_window)
    {
        Window child;
        int root_x;
        int root_y;
        int win_x;
        int win_y;
        unsigned int mask;
        XWindowAttributes attributes;
        if (XGetWindowAttributes(display, native_window, &attributes) &&
            XQueryPointer(display, native_window, &child, &child, &root_x,
                          &root_y, &win_x, &win_y, &mask))
        {
            (void)attributes;
            Window root = DefaultRootWindow(display);
            XTranslateCoordinates(display, native_window, root, 0, 0, &win_x,
                                  &win_y, &child);
            platform_ctx.drag_start_root_x = root_x;
            platform_ctx.drag_start_root_y = root_y;
            platform_ctx.drag_window_x = win_x;
            platform_ctx.drag_window_y = win_y;
        }
    }
#endif
}

static void glps_set_window_maximized(size_t window_id, bool maximized)
{
#ifdef GLPS_USE_X11
    if (!platform_ctx.wm)
        return;
    Display *display = (Display *)glps_wm_get_display(platform_ctx.wm);
    Window native_window = (Window)glps_wm_window_get_native_ptr(
        platform_ctx.wm, window_id);
    if (!display || !native_window)
        return;

    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom maximized_h = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ",
                                   False);
    Atom maximized_v = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT",
                                   False);
    Window root = DefaultRootWindow(display);
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.message_type = wm_state;
    event.xclient.display = display;
    event.xclient.window = native_window;
    event.xclient.format = 32;
    event.xclient.data.l[0] = maximized ? 1 : 0;
    event.xclient.data.l[1] = (long)maximized_h;
    event.xclient.data.l[2] = (long)maximized_v;
    event.xclient.data.l[3] = 1;
    XSendEvent(display, root, False,
                SubstructureRedirectMask | SubstructureNotifyMask, &event);
    platform_ctx.window_maximized = maximized;
    XFlush(display);
#else
    (void)window_id;
    (void)maximized;
#endif
}

static void glps_toggle_window_maximized(size_t window_id)
{
    glps_set_window_maximized(window_id, !platform_ctx.window_maximized);
}

AromaPlatformInterface aroma_platform_glps = {
    .initialize = initialize,
    .create_window = create_window,
    .destroy_window = destroy_window,
    .window_ids_shift_on_destroy = true,
    .make_context_current = make_context_current,
    .get_window_size = get_window_size,
    .set_window_update_callback = set_window_update_callback,
    .request_window_update = request_window_update,
    .run_event_loop = run_event_loop,
    .swap_buffers = swap_buffers,
    .shutdown = shutdown,
    .set_android_app = NULL,
#ifdef AROMA_HAS_VULKAN
    .create_vulkan_surface = glps_create_vulkan_surface,
    .get_vulkan_instance_extensions = glps_get_vulkan_instance_extensions,
#else
    .create_vulkan_surface = NULL,
    .get_vulkan_instance_extensions = NULL,
#endif

    .get_native_window_ptr = glps_get_native_window_ptr,
    .get_native_display_ptr = glps_get_display,
    .set_clipboard_text = glps_set_clipboard_text,
    .get_clipboard_text = glps_get_clipboard_text,
    .set_window_opacity = glps_set_window_opacity,
    .set_window_decorated = glps_set_window_decorated,
    .begin_window_drag = glps_begin_window_drag,
    .set_window_maximized = glps_set_window_maximized,
    .toggle_window_maximized = glps_toggle_window_maximized,
};

#endif