#ifndef ESP32
#include "aroma_platform_interface.h"

#ifdef AROMA_HAS_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif

#include <stdio.h>
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <ctype.h>
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"

extern const unsigned char* glGetString(unsigned int name);

typedef struct
{
    GLFWwindow *primary_window;
    double last_mouse_x;
    double last_mouse_y;
    bool mouse_button_down;
    void (*frame_callback)(size_t, void*);
    void *frame_callback_data;
} AromaGLFContext;

static AromaGLFContext platform_ctx = {
    .primary_window = NULL,
    .last_mouse_x = 0.0,
    .last_mouse_y = 0.0,
    .mouse_button_down = false,
    .frame_callback = NULL,
    .frame_callback_data = NULL
};

static bool queue_mouse_event(AromaEventType type, double mouse_x, double mouse_y, uint8_t button)
{
    AromaNode *root = aroma_event_get_root();
    if (!root) return false;

    AromaNode *target = aroma_event_hit_test(root, (int)mouse_x, (int)mouse_y);
    uint64_t target_id = target ? target->node_id : root->node_id;

    AromaEvent *event = aroma_event_create_mouse(type, target_id, (int)mouse_x, (int)mouse_y, button);
    if (!event) return false;

    event->data.mouse.delta_x = (int)(mouse_x - platform_ctx.last_mouse_x);
    event->data.mouse.delta_y = (int)(mouse_y - platform_ctx.last_mouse_y);

    return aroma_event_queue(event);
}

static bool queue_key_event(AromaEventType type, uint32_t key_value, uint16_t modifiers)
{
    AromaNode *root = aroma_event_get_root();
    if (!root) return false;

    AromaNode *target = aroma_ui_get_focused_node();
    if (!target) target = root;

    AromaEvent *event = aroma_event_create_key(type, target->node_id, key_value, modifiers);
    if (!event) return false;

    return aroma_event_queue(event);
}

static void glfw_cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
    bool moved = (xpos != platform_ctx.last_mouse_x) || (ypos != platform_ctx.last_mouse_y);
    if (moved) {
        queue_mouse_event(EVENT_TYPE_MOUSE_MOVE, xpos, ypos, 0);
    }
    aroma_event_handle_pointer_move((int)xpos, (int)ypos, platform_ctx.mouse_button_down);

    platform_ctx.last_mouse_x = xpos;
    platform_ctx.last_mouse_y = ypos;
}

static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    bool state = (action == GLFW_PRESS);
    platform_ctx.mouse_button_down = state;
    AromaEventType type = state ? EVENT_TYPE_MOUSE_CLICK : EVENT_TYPE_MOUSE_RELEASE;
    queue_mouse_event(type, platform_ctx.last_mouse_x, platform_ctx.last_mouse_y, button);

    if (!state) {
        aroma_event_handle_pointer_move((int)platform_ctx.last_mouse_x, (int)platform_ctx.last_mouse_y, false);
    }
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    AromaEventType type = (action == GLFW_RELEASE) ? EVENT_TYPE_KEY_RELEASE : EVENT_TYPE_KEY_PRESS;

    uint32_t key_value = 0;
    bool is_printable = false;

    switch (key) {
        case GLFW_KEY_BACKSPACE: key_value = 8; break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: key_value = 10; break;
        case GLFW_KEY_LEFT: key_value = 0xFF51; break;
        case GLFW_KEY_UP: key_value = 0xFF52; break;
        case GLFW_KEY_RIGHT: key_value = 0xFF53; break;
        case GLFW_KEY_DOWN: key_value = 0xFF54; break;
        case GLFW_KEY_HOME: key_value = 0xFF50; break;
        case GLFW_KEY_END: key_value = 0xFF57; break;
        case GLFW_KEY_DELETE: key_value = 127; break;
        default:
            is_printable = true;
            key_value = (uint32_t)key;
            break;
    }

    uint16_t modifiers = 0;
    if (mods & GLFW_MOD_CAPS_LOCK) modifiers |= AROMA_KEY_MOD_CAPSLOCK;
    if (mods & GLFW_MOD_CONTROL)   modifiers |= AROMA_KEY_MOD_CTRL;

    if (is_printable && type == EVENT_TYPE_KEY_PRESS && !(modifiers & AROMA_KEY_MOD_CTRL)) {
        return; 
    }

    queue_key_event(type, key_value, modifiers);
}

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    queue_key_event(EVENT_TYPE_KEY_PRESS, codepoint, 0);
}

static void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (xoffset == 0 && yoffset == 0) return;
    
    int mx = (int)platform_ctx.last_mouse_x;
    int my = (int)platform_ctx.last_mouse_y;
    
    AromaNode *root = aroma_event_get_root();
    if (!root) return;
    
    AromaNode *target = aroma_event_hit_test(root, mx, my);
    uint64_t node_id = target ? target->node_id : root->node_id;
    
    
    AromaEvent *ev = aroma_event_create_scroll(node_id, mx, my, (float)xoffset, (float)yoffset);
    if (ev) aroma_event_queue(ev);
}

static int initialize()
{
    if (!glfwInit())
    {
        LOG_CRITICAL("Failed to initialize GLFW");
        return 0;
    }

    AromaGraphicsBackendType type = aroma_backend_abi.get_graphics_backend_type
                                    ? aroma_backend_abi.get_graphics_backend_type()
                                    : GRAPHICS_BACKEND_GLES3;
    if (type == GRAPHICS_BACKEND_VULKAN) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    } else if (type == GRAPHICS_BACKEND_GLES3) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    }

    return 1;
}
static bool g_offscreen_mode = false;

static void set_offscreen_mode(bool offscreen) {
    g_offscreen_mode = offscreen;
}

static void read_pixels(size_t window_id, void* buffer, int width, int height) {
    (void)window_id;
    /* OpenGL ES 3.0 glReadPixels */
    extern void glReadPixels(int x, int y, int width, int height, unsigned int format, unsigned int type, void *pixels);
    glReadPixels(0, 0, width, height, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, buffer);
}


static size_t create_window(const char *title, int x, int y, int width, int height)
{
    (void)x; (void)y; 

    if (g_offscreen_mode) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow *window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window)
    {
        const char *description;
        int code = glfwGetError(&description);
        LOG_CRITICAL("glfwCreateWindow failed: %d: %s", code, description);
        return 0;
    }

    size_t window_id = 1; 

    if (!platform_ctx.primary_window)
    {
        platform_ctx.primary_window = window;

        glfwMakeContextCurrent(window);
        extern const unsigned char* glGetString(unsigned int name);
        const char* version = (const char*)glGetString(0x1F02);
        LOG_INFO("GLFW OpenGL/ES Version: %s", version ? version : "NULL");

        glfwSetCursorPosCallback(window, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
        glfwSetScrollCallback(window, glfw_scroll_callback);
        glfwSetKeyCallback(window, glfw_key_callback);
        glfwSetCharCallback(window, glfw_char_callback);

        extern const unsigned char* glGetString(unsigned int name);
        const char *v = (const char *)glGetString(0x1F02);
        LOG_INFO("DEBUG GLFW: GL_VERSION=%s", v ? v : "null");

        aroma_backend_abi.get_graphics_interface()->setup_shared_window_resources();
    }

    aroma_backend_abi.get_graphics_interface()->setup_separate_window_resources(window_id);

    return window_id;
}

static void make_context_current(size_t window_id)
{
    if (platform_ctx.primary_window) {
        glfwMakeContextCurrent(platform_ctx.primary_window);
    }
}

static void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    if (platform_ctx.primary_window) {
        glfwGetFramebufferSize(platform_ctx.primary_window, window_width, window_height);
    }
}

static void set_window_update_callback(void (*callback)(size_t window_id, void *data), void *data)
{
    platform_ctx.frame_callback = callback;
    platform_ctx.frame_callback_data = data;
}

static void request_window_update(size_t window_id)
{
    if (platform_ctx.frame_callback) {
        platform_ctx.frame_callback(window_id, platform_ctx.frame_callback_data);
    }
}

static bool run_event_loop()
{
    if (!platform_ctx.primary_window)
    {
        LOG_ERROR("Window not initialized. Cannot run event loop.");
        return false;
    }

    glfwPollEvents();

    if (aroma_dirty_list_has_entries())
    {
        request_window_update(1);
    }
    
    return !glfwWindowShouldClose(platform_ctx.primary_window);
}

static void swap_buffers(size_t window_id)
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
    if (platform_ctx.primary_window) {
        glfwSwapBuffers(platform_ctx.primary_window);
    }
}

static void shutdown()
{
    if (platform_ctx.primary_window) {
        glfwDestroyWindow(platform_ctx.primary_window);
        platform_ctx.primary_window = NULL;
    }
    glfwTerminate();
}

#ifdef AROMA_HAS_VULKAN
static int glfw_create_vulkan_surface(void *instance, void **surface)
{
    if (!platform_ctx.primary_window) return 0;
    VkResult err = glfwCreateWindowSurface((VkInstance)instance, platform_ctx.primary_window, NULL, (VkSurfaceKHR*)surface);
    return err == VK_SUCCESS ? 1 : 0;
}

static const char **glfw_get_vulkan_instance_extensions(uint32_t *count)
{
    return glfwGetRequiredInstanceExtensions(count);
}
#endif

static void *glfw_get_native_window_ptr(size_t window_id)
{
    return platform_ctx.primary_window;
}

static void *glfw_get_display()
{
    return NULL; 
}

AromaPlatformInterface aroma_platform_glfw = {
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
    .create_vulkan_surface = glfw_create_vulkan_surface,
    .get_vulkan_instance_extensions = glfw_get_vulkan_instance_extensions,
#else
    .create_vulkan_surface = NULL,
    .get_vulkan_instance_extensions = NULL,
#endif

    .get_native_window_ptr = glfw_get_native_window_ptr,
    .get_native_display_ptr = glfw_get_display,
    .set_offscreen_mode = set_offscreen_mode,
    .read_pixels = read_pixels,
};

#endif
