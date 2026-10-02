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

typedef struct
{
    glps_WindowManager *wm;
    size_t primary_window_id;
    bool has_primary_window;
    double last_mouse_x;
    double last_mouse_y;
    bool mouse_button_down;
    bool capslock_active;
    bool ctrl_active;
    bool frame_rendered;
    bool glps_initialized;
    void (*frame_update_callback)(size_t window_id, void *data);
    void *frame_update_data;
} AromaGLPSContext;

static AromaGLPSContext platform_ctx = (AromaGLPSContext){
    .wm = NULL,
    .primary_window_id = 0,
    .has_primary_window = false,
    .last_mouse_x = 0.0,
    .last_mouse_y = 0.0,
    .mouse_button_down = false,
    .capslock_active = false,
    .ctrl_active = false,
    .frame_rendered = false,
    .glps_initialized = false,
    .frame_update_callback = NULL,
    .frame_update_data = NULL};

static bool queue_mouse_event(AromaEventType type, double mouse_x, double mouse_y, uint8_t button)
{
    AromaNode *root = aroma_event_get_root();
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

    event->data.mouse.delta_x = (int)(mouse_x - platform_ctx.last_mouse_x);
    event->data.mouse.delta_y = (int)(mouse_y - platform_ctx.last_mouse_y);

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
    (void)window_id;
    (void)data;
    bool moved = (mouse_x != platform_ctx.last_mouse_x) || (mouse_y != platform_ctx.last_mouse_y);
    if (moved)
    {
        queue_mouse_event(EVENT_TYPE_MOUSE_MOVE, mouse_x, mouse_y, 0);
    }

    aroma_event_handle_pointer_move((int)mouse_x, (int)mouse_y, platform_ctx.mouse_button_down);

    platform_ctx.last_mouse_x = mouse_x;
    platform_ctx.last_mouse_y = mouse_y;
}

static void glps_mouse_click_callback(size_t window_id, bool state, void *data)
{
    (void)window_id;
    (void)data;

    platform_ctx.mouse_button_down = state;
    AromaEventType type = state ? EVENT_TYPE_MOUSE_CLICK : EVENT_TYPE_MOUSE_RELEASE;
    queue_mouse_event(type, platform_ctx.last_mouse_x, platform_ctx.last_mouse_y, 0);

    if (!state)
    {
        aroma_event_handle_pointer_move((int)platform_ctx.last_mouse_x, (int)platform_ctx.last_mouse_y, false);
    }
}
static void glps_keyboard_callback(size_t window_id, bool state, const char *value,
                                   unsigned long keycode, void *data)
{
    (void)window_id;
    (void)data;

    if (!state)
    {
        if (keycode == 37 || keycode == 105)
        {
            platform_ctx.ctrl_active = false;
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

    uint16_t modifiers = 0;
    if (platform_ctx.capslock_active)
        modifiers |= AROMA_KEY_MOD_CAPSLOCK;
    if (platform_ctx.ctrl_active)
        modifiers |= AROMA_KEY_MOD_CTRL;

    if (value && value[0] != '\0')
    {
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
    (void)window_id;
    (void)source;
    (void)discrete;
    (void)is_stopped;
    (void)data;

    if (value == 0)
        return;

    float scroll_x = (axe == GLPS_SCROLL_H_AXIS) ? (float)value : 0.0f;
    float scroll_y = (axe == GLPS_SCROLL_V_AXIS) ? (float)value : 0.0f;

    int mx = (int)platform_ctx.last_mouse_x;
    int my = (int)platform_ctx.last_mouse_y;

    AromaNode *root = aroma_event_get_root();
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
    (void)window_id;
    (void)id;
    (void)major;
    (void)minor;
    (void)orientation;
    (void)data;
    fprintf(stderr, "glps_touch_callback ENTRY: state=%d touch_x=%.1f touch_y=%.1f\n", state, touch_x, touch_y);

    bool moved = (touch_x != platform_ctx.last_mouse_x) || (touch_y != platform_ctx.last_mouse_y);

    if (state)
    {
        if (!platform_ctx.mouse_button_down)
        {
            platform_ctx.last_mouse_x = touch_x;
            platform_ctx.last_mouse_y = touch_y;
            platform_ctx.mouse_button_down = true;
            queue_mouse_event(EVENT_TYPE_MOUSE_CLICK, touch_x, touch_y, 0);
            aroma_event_handle_pointer_move((int)touch_x, (int)touch_y, true);
        }
        else if (moved)
        {

            aroma_event_handle_touch(id, (int)touch_x, (int)touch_y, 1);
            queue_mouse_event(EVENT_TYPE_MOUSE_MOVE, touch_x, touch_y, 0);
            aroma_event_handle_pointer_move((int)touch_x, (int)touch_y, true);
            platform_ctx.last_mouse_x = touch_x;
            platform_ctx.last_mouse_y = touch_y;
        }
    }
    else
    {
        platform_ctx.last_mouse_x = touch_x;
        platform_ctx.last_mouse_y = touch_y;
        platform_ctx.mouse_button_down = false;
        queue_mouse_event(EVENT_TYPE_MOUSE_RELEASE, touch_x, touch_y, 0);
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
    glps_wm_set_window_ctx_curr(platform_ctx.wm, window_id);
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
    return !glps_wm_should_close(platform_ctx.wm);
}

void swap_buffers(size_t window_id)
{
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
        return;
    }

    glps_wm_destroy(platform_ctx.wm);
    platform_ctx.wm = NULL;
    platform_ctx.primary_window_id = 0;
    platform_ctx.has_primary_window = false;
    platform_ctx.glps_initialized = false;
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

AromaPlatformInterface aroma_platform_glps = {
    .initialize = initialize,
    .create_window = create_window,
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
};

#endif