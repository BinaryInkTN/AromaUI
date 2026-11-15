#ifndef ESP32
#include "aroma_platform_interface.h"

#ifdef AROMA_HAS_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include "vendors/nfd/include/nfd.h"
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

extern const unsigned char *glGetString(unsigned int name);

typedef struct
{
    GLFWwindow *primary_window;
    double last_mouse_x;
    double last_mouse_y;
    bool mouse_button_down;
    bool window_dragging;
    bool window_maximized;
    double window_drag_offset_x;
    double window_drag_offset_y;
    void (*frame_callback)(size_t, void *);
    void *frame_callback_data;

    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;
    bool surfaceless_active;
} AromaGLFContext;

static AromaGLFContext platform_ctx = {
    .primary_window = NULL,
    .last_mouse_x = 0.0,
    .last_mouse_y = 0.0,
    .mouse_button_down = false,
    .window_dragging = false,
    .frame_callback = NULL,
    .frame_callback_data = NULL,
    .egl_display = EGL_NO_DISPLAY,
    .egl_context = EGL_NO_CONTEXT,
    .surfaceless_active = false,
};

static bool g_use_surfaceless = false;
static bool g_offscreen_mode = false;
static int g_window_width = 800;
static int g_window_height = 600;
static bool g_initialized = false;

static int initialize_glfw(void);
static int initialize_egl_surfaceless(void);
static void shutdown_egl_surfaceless(void);

static int initialize_egl_surfaceless(void)
{
    LOG_INFO("Starting EGL surfaceless initialization...");

    platform_ctx.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (platform_ctx.egl_display == EGL_NO_DISPLAY)
    {

        platform_ctx.egl_display = eglGetDisplay((EGLNativeDisplayType)EGL_DEFAULT_DISPLAY);
        if (platform_ctx.egl_display == EGL_NO_DISPLAY)
        {
            LOG_CRITICAL("Failed to get EGL display for surfaceless context");
            return 0;
        }
    }

    EGLint major, minor;
    if (!eglInitialize(platform_ctx.egl_display, &major, &minor))
    {
        EGLint error = eglGetError();
        LOG_CRITICAL("Failed to initialize EGL for surfaceless context: error 0x%x", error);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
        return 0;
    }
    LOG_INFO("EGL surfaceless initialized: version %d.%d", major, minor);

    const char *extensions = eglQueryString(platform_ctx.egl_display, EGL_EXTENSIONS);
    if (extensions)
    {
        LOG_INFO("EGL extensions: %s", extensions);
        if (!strstr(extensions, "EGL_KHR_surfaceless_context"))
        {
            LOG_ERROR("EGL_KHR_surfaceless_context not supported, trying without it...");
        }
    }
    else
    {
        LOG_ERROR("Failed to query EGL extensions");
    }

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE};

    EGLint num_configs;
    if (!eglChooseConfig(platform_ctx.egl_display, config_attribs,
                         &platform_ctx.egl_config, 1, &num_configs))
    {
        EGLint error = eglGetError();
        LOG_CRITICAL("Failed to choose EGL config for surfaceless context: error 0x%x", error);
        eglTerminate(platform_ctx.egl_display);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
        return 0;
    }

    if (num_configs == 0)
    {
        LOG_CRITICAL("No suitable EGL config found for surfaceless context");
        eglTerminate(platform_ctx.egl_display);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
        return 0;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        EGLint error = eglGetError();
        LOG_CRITICAL("Failed to bind OpenGL ES API: error 0x%x", error);
        eglTerminate(platform_ctx.egl_display);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
        return 0;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE};

    platform_ctx.egl_context = eglCreateContext(platform_ctx.egl_display,
                                                platform_ctx.egl_config,
                                                EGL_NO_CONTEXT,
                                                context_attribs);
    if (platform_ctx.egl_context == EGL_NO_CONTEXT)
    {
        EGLint error = eglGetError();
        LOG_CRITICAL("Failed to create surfaceless EGL context: error 0x%x", error);
        eglTerminate(platform_ctx.egl_display);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
        return 0;
    }

    if (!eglMakeCurrent(platform_ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        platform_ctx.egl_context))
    {
        EGLint error = eglGetError();
        LOG_CRITICAL("Failed to make surfaceless context current: error 0x%x", error);
        eglDestroyContext(platform_ctx.egl_display, platform_ctx.egl_context);
        eglTerminate(platform_ctx.egl_display);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
        platform_ctx.egl_context = EGL_NO_CONTEXT;
        return 0;
    }

    platform_ctx.surfaceless_active = true;
    LOG_INFO("EGL surfaceless context created and made current successfully");

    const char *version = (const char *)glGetString(GL_VERSION);
    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    LOG_INFO("Surfaceless GL Version: %s", version ? version : "NULL");
    LOG_INFO("Surfaceless GL Vendor: %s", vendor ? vendor : "NULL");
    LOG_INFO("Surfaceless GL Renderer: %s", renderer ? renderer : "NULL");

    return 1;
}

static int initialize_glfw(void)
{
    LOG_INFO("Starting GLFW initialization...");

    if (!glfwInit())
    {
        LOG_CRITICAL("Failed to initialize GLFW");
        return 0;
    }

    AromaGraphicsBackendType type = aroma_backend_abi.get_graphics_backend_type
                                        ? aroma_backend_abi.get_graphics_backend_type()
                                        : GRAPHICS_BACKEND_GLES3;
    if (type == GRAPHICS_BACKEND_VULKAN)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else if (type == GRAPHICS_BACKEND_GLES3)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_RED_BITS, 8);
        glfwWindowHint(GLFW_GREEN_BITS, 8);
        glfwWindowHint(GLFW_BLUE_BITS, 8);
        glfwWindowHint(GLFW_ALPHA_BITS, 0);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);
        glfwWindowHint(GLFW_STENCIL_BITS, 8);
    }

    LOG_INFO("GLFW initialized successfully");
    return 1;
}

static void shutdown_egl_surfaceless(void)
{
    if (platform_ctx.egl_display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(platform_ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (platform_ctx.egl_context != EGL_NO_CONTEXT)
        {
            eglDestroyContext(platform_ctx.egl_display, platform_ctx.egl_context);
            platform_ctx.egl_context = EGL_NO_CONTEXT;
        }

        eglTerminate(platform_ctx.egl_display);
        platform_ctx.egl_display = EGL_NO_DISPLAY;
    }
    platform_ctx.surfaceless_active = false;
    LOG_INFO("EGL surfaceless shutdown complete");
}

static bool queue_mouse_event(AromaEventType type, double mouse_x, double mouse_y, uint8_t button)
{
    int win_idx = find_window_index_at_point((int)mouse_x, (int)mouse_y);
    AromaNode *root = NULL;
    if (win_idx >= 0)
    {
        root = g_windows[win_idx].root_node;
        aroma_ui_activate_window(g_windows[win_idx].window_id);
    }
    else
    {
        root = aroma_event_get_root();
    }
    if (!root)
        return false;

    AromaNode *target = aroma_event_hit_test(root, (int)mouse_x, (int)mouse_y);
    uint64_t target_id = target ? target->node_id : root->node_id;

    AromaEvent *event = aroma_event_create_mouse(type, target_id, (int)mouse_x, (int)mouse_y, button);
    if (!event)
        return false;

    event->data.mouse.delta_x = (int)(mouse_x - platform_ctx.last_mouse_x);
    event->data.mouse.delta_y = (int)(mouse_y - platform_ctx.last_mouse_y);

    return aroma_event_queue(event);
}

static bool queue_key_event(AromaEventType type, uint32_t key_value, uint16_t modifiers)
{
    AromaNode *root = aroma_event_get_root();
    if (!root)
        return false;

    AromaNode *target = aroma_ui_get_focused_node();
    if (!target)
        target = root;

    AromaEvent *event = aroma_event_create_key(type, target->node_id, key_value, modifiers);
    if (!event)
        return false;

    return aroma_event_queue(event);
}

static void glfw_cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (platform_ctx.window_dragging)
    {
        int window_x, window_y;
        glfwGetWindowPos(window, &window_x, &window_y);
        glfwSetWindowPos(window,
                         (int)(window_x + xpos - platform_ctx.window_drag_offset_x),
                         (int)(window_y + ypos - platform_ctx.window_drag_offset_y));
    }

    bool moved = (xpos != platform_ctx.last_mouse_x) || (ypos != platform_ctx.last_mouse_y);
    if (moved)
    {
        queue_mouse_event(EVENT_TYPE_MOUSE_MOVE, xpos, ypos, 0);
    }
    aroma_event_handle_pointer_move((int)xpos, (int)ypos, platform_ctx.mouse_button_down);

    platform_ctx.last_mouse_x = xpos;
    platform_ctx.last_mouse_y = ypos;
}

static void glfw_set_window_decorated(size_t window_id, bool decorated)
{
    (void)window_id;
    if (platform_ctx.primary_window)
        glfwSetWindowAttrib(platform_ctx.primary_window, GLFW_DECORATED,
                            decorated ? GLFW_TRUE : GLFW_FALSE);
}

static void glfw_begin_window_drag(size_t window_id)
{
    (void)window_id;
    if (!platform_ctx.primary_window)
        return;
    double cursor_x, cursor_y;
    glfwGetCursorPos(platform_ctx.primary_window, &cursor_x, &cursor_y);
    platform_ctx.window_drag_offset_x = cursor_x;
    platform_ctx.window_drag_offset_y = cursor_y;
    platform_ctx.window_dragging = true;
}

static void glfw_set_window_minimized(size_t window_id, bool minimized)
{
    (void)window_id;
    if (!platform_ctx.primary_window)
        return;
    if (minimized)
        glfwIconifyWindow(platform_ctx.primary_window);
    else
        glfwRestoreWindow(platform_ctx.primary_window);
}

static void glfw_set_window_maximized(size_t window_id, bool maximized)
{
    (void)window_id;
    if (!platform_ctx.primary_window)
        return;
    if (maximized)
        glfwMaximizeWindow(platform_ctx.primary_window);
    else
        glfwRestoreWindow(platform_ctx.primary_window);
}

static void glfw_toggle_window_maximized(size_t window_id)
{
    platform_ctx.window_maximized = !platform_ctx.window_maximized;
    glfw_set_window_maximized(window_id, platform_ctx.window_maximized);
}

static void glfw_mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    LOG_INFO("Mouse button event: button=%d, action=%d, mods=%d", button, action, mods);
    bool state = (action == GLFW_PRESS);
    platform_ctx.mouse_button_down = state;
    if (!state)
        platform_ctx.window_dragging = false;
    AromaEventType type = state ? EVENT_TYPE_MOUSE_CLICK : EVENT_TYPE_MOUSE_RELEASE;
    queue_mouse_event(type, platform_ctx.last_mouse_x, platform_ctx.last_mouse_y, button);

    if (!state)
    {
        aroma_event_handle_pointer_move((int)platform_ctx.last_mouse_x, (int)platform_ctx.last_mouse_y, false);
        .toggle_window_maximized = glfw_toggle_window_maximized,
    };
}

static uint32_t glfw_key_to_value(int key, bool shift, bool ctrl)
{
    if (ctrl)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            return (uint32_t)(key - GLFW_KEY_A + 1);
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        {
            return (uint32_t)(key - GLFW_KEY_0 + 48);
        }
        switch (key)
        {
        case GLFW_KEY_SPACE: return 0;
        case GLFW_KEY_MINUS: return 31;
        case GLFW_KEY_PERIOD: return 26;
        case GLFW_KEY_SLASH: return 29;
        case GLFW_KEY_BACKSLASH: return 28;
        case GLFW_KEY_SEMICOLON: return 27;
        case GLFW_KEY_APOSTROPHE: return 25;
        case GLFW_KEY_LEFT_BRACKET: return 27;
        case GLFW_KEY_RIGHT_BRACKET: return 29;
        case GLFW_KEY_COMMA: return 24;
        case GLFW_KEY_GRAVE_ACCENT: return 26;
        default: return 0;
        }
    }

    switch (key)
    {
    case GLFW_KEY_ESCAPE: return 27;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return 10;
    case GLFW_KEY_TAB: return 9;
    case GLFW_KEY_BACKSPACE: return 8;
    case GLFW_KEY_DELETE: return 127;
    case GLFW_KEY_INSERT: return 0xFFFF;
    case GLFW_KEY_LEFT: return 0xFF51;
    case GLFW_KEY_UP: return 0xFF52;
    case GLFW_KEY_RIGHT: return 0xFF53;
    case GLFW_KEY_DOWN: return 0xFF54;
    case GLFW_KEY_HOME: return 0xFF50;
    case GLFW_KEY_END: return 0xFF57;
    case GLFW_KEY_PAGE_UP: return 0xFF55;
    case GLFW_KEY_PAGE_DOWN: return 0xFF56;
    case GLFW_KEY_SPACE: return 32;
    case GLFW_KEY_KP_0: return '0';
    case GLFW_KEY_KP_1: return '1';
    case GLFW_KEY_KP_2: return '2';
    case GLFW_KEY_KP_3: return '3';
    case GLFW_KEY_KP_4: return '4';
    case GLFW_KEY_KP_5: return '5';
    case GLFW_KEY_KP_6: return '6';
    case GLFW_KEY_KP_7: return '7';
    case GLFW_KEY_KP_8: return '8';
    case GLFW_KEY_KP_9: return '9';
    case GLFW_KEY_KP_DECIMAL: return '.';
    case GLFW_KEY_KP_DIVIDE: return '/';
    case GLFW_KEY_KP_MULTIPLY: return '*';
    case GLFW_KEY_KP_SUBTRACT: return '-';
    case GLFW_KEY_KP_ADD: return '+';
    case GLFW_KEY_KP_EQUAL: return '=';
    case GLFW_KEY_F1: return 0xFFBE;
    case GLFW_KEY_F2: return 0xFFBF;
    case GLFW_KEY_F3: return 0xFFC0;
    case GLFW_KEY_F4: return 0xFFC1;
    case GLFW_KEY_F5: return 0xFFC2;
    case GLFW_KEY_F6: return 0xFFC3;
    case GLFW_KEY_F7: return 0xFFC4;
    case GLFW_KEY_F8: return 0xFFC5;
    case GLFW_KEY_F9: return 0xFFC6;
    case GLFW_KEY_F10: return 0xFFC7;
    case GLFW_KEY_F11: return 0xFFC8;
    case GLFW_KEY_F12: return 0xFFC9;
    case GLFW_KEY_PAUSE: return 0xFF13;
    case GLFW_KEY_SCROLL_LOCK: return 0xFF14;
    case GLFW_KEY_NUM_LOCK: return 0xFF9F;
    case GLFW_KEY_PRINT_SCREEN: return 0xFF61;
    case GLFW_KEY_MENU: return 0xFF67;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: return 0;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: return 0;
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT: return 0;
    case GLFW_KEY_LEFT_SUPER:
    case GLFW_KEY_RIGHT_SUPER: return 0;
    case GLFW_KEY_CAPS_LOCK: return 0;
    case GLFW_KEY_A: case GLFW_KEY_B: case GLFW_KEY_C: case GLFW_KEY_D:
    case GLFW_KEY_E: case GLFW_KEY_F: case GLFW_KEY_G: case GLFW_KEY_H:
    case GLFW_KEY_I: case GLFW_KEY_J: case GLFW_KEY_K: case GLFW_KEY_L:
    case GLFW_KEY_M: case GLFW_KEY_N: case GLFW_KEY_O: case GLFW_KEY_P:
    case GLFW_KEY_Q: case GLFW_KEY_R: case GLFW_KEY_S: case GLFW_KEY_T:
    case GLFW_KEY_U: case GLFW_KEY_V: case GLFW_KEY_W: case GLFW_KEY_X:
    case GLFW_KEY_Y: case GLFW_KEY_Z:
        if (shift)
            return (uint32_t)('A' + (key - GLFW_KEY_A));
        return (uint32_t)('a' + (key - GLFW_KEY_A));
    case GLFW_KEY_0: case GLFW_KEY_1: case GLFW_KEY_2: case GLFW_KEY_3:
    case GLFW_KEY_4: case GLFW_KEY_5: case GLFW_KEY_6: case GLFW_KEY_7:
    case GLFW_KEY_8: case GLFW_KEY_9:
        return (uint32_t)('0' + (key - GLFW_KEY_0));
    case GLFW_KEY_MINUS: return shift ? '_' : '-';
    case GLFW_KEY_EQUAL: return shift ? '+' : '=';
    case GLFW_KEY_LEFT_BRACKET: return shift ? '{' : '[';
    case GLFW_KEY_RIGHT_BRACKET: return shift ? '}' : ']';
    case GLFW_KEY_BACKSLASH: return shift ? '|' : '\\';
    case GLFW_KEY_SEMICOLON: return shift ? ':' : ';';
    case GLFW_KEY_APOSTROPHE: return shift ? '"' : '\'';
    case GLFW_KEY_GRAVE_ACCENT: return shift ? '~' : '`';
    case GLFW_KEY_COMMA: return shift ? '<' : ',';
    case GLFW_KEY_PERIOD: return shift ? '>' : '.';
    case GLFW_KEY_SLASH: return shift ? '?' : '/';
    case GLFW_KEY_KP_EQUAL: return '=';
    case GLFW_KEY_KP_DECIMAL: return '.';
    case GLFW_KEY_WORLD_1:
    case GLFW_KEY_WORLD_2:
    default:
        return 0;
    }
}

static void glfw_key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        if (aroma_ui_handle_back_press())
            return;
    }

    AromaEventType type = (action == GLFW_RELEASE) ? EVENT_TYPE_KEY_RELEASE : EVENT_TYPE_KEY_PRESS;

    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    uint32_t key_value = glfw_key_to_value(key, shift, ctrl);

    if (key_value == 0 && action == GLFW_PRESS)
    {
        return;
    }

    uint16_t modifiers = 0;
    if (mods & GLFW_MOD_CAPS_LOCK)
        modifiers |= AROMA_KEY_MOD_CAPSLOCK;
    if (ctrl)
        modifiers |= AROMA_KEY_MOD_CTRL;
    if (shift)
        modifiers |= AROMA_KEY_MOD_SHIFT;

    if (action == GLFW_REPEAT)
    {
        type = EVENT_TYPE_KEY_PRESS;
    }

    queue_key_event(type, key_value, modifiers);
}

static void glfw_char_callback(GLFWwindow *window, unsigned int codepoint)
{
    /* Char callback handles IME/dead-key input for non-ASCII codepoints.
     * ASCII printable keys are already mapped by glfw_key_callback above. */
    if (codepoint < 128)
        return;
    queue_key_event(EVENT_TYPE_KEY_PRESS, codepoint, 0);
}

static void glfw_scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    if (xoffset == 0 && yoffset == 0)
        return;

    int mx = (int)platform_ctx.last_mouse_x;
    int my = (int)platform_ctx.last_mouse_y;

    int win_idx = find_window_index_at_point(mx, my);
    AromaNode *root = NULL;
    if (win_idx >= 0)
    {
        root = g_windows[win_idx].root_node;
        aroma_ui_activate_window(g_windows[win_idx].window_id);
    }
    else
    {
        root = aroma_event_get_root();
    }
    if (!root)
        return;

    AromaNode *target = aroma_event_hit_test(root, mx, my);
    uint64_t node_id = target ? target->node_id : root->node_id;

    AromaEvent *ev = aroma_event_create_scroll(node_id, mx, my, (float)xoffset, (float)yoffset);
    if (ev)
        aroma_event_queue(ev);
}

static int initialize()
{
    if (g_initialized)
    {
        LOG_WARNING("Platform already initialized");
        return 1;
    }

    LOG_INFO("Starting platform initialization...");
    LOG_INFO("Surfaceless mode: %s", g_use_surfaceless ? "ENABLED" : "DISABLED");

    int result = 0;

    if (g_use_surfaceless)
    {
        LOG_INFO("Initializing EGL surfaceless platform...");
        result = initialize_egl_surfaceless();
        if (!result)
        {
            LOG_ERROR("Failed to initialize EGL surfaceless platform");
            LOG_ERROR("Falling back to GLFW platform");
            g_use_surfaceless = false;
            result = initialize_glfw();
        }
    }
    else
    {
        result = initialize_glfw();

    }
    printf("Platform: %s\n", glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? "Wayland" :
                         glfwGetPlatform() == GLFW_PLATFORM_X11 ? "X11" :
                         "Other");
    if (result)
    {
        g_initialized = true;
        LOG_INFO("Platform initialization complete");
    }
    else
    {
        LOG_CRITICAL("Platform initialization failed");
    }

    return result;
}

static void set_use_surfaceless(bool use_surfaceless)
{
    if (g_initialized)
    {
        LOG_ERROR("Cannot change surfaceless mode after initialization");
        return;
    }

    g_use_surfaceless = use_surfaceless;
    LOG_INFO("Surfaceless mode %s for next initialization", use_surfaceless ? "ENABLED" : "DISABLED");
}

static void set_offscreen_mode(bool offscreen)
{
    g_offscreen_mode = offscreen;
    LOG_INFO("Offscreen mode %s", offscreen ? "enabled" : "disabled");
}

static void read_pixels(size_t window_id, void *buffer, int width, int height)
{
    (void)window_id;
    extern void glReadPixels(int x, int y, int width, int height, unsigned int format, unsigned int type, void *pixels);
    glReadPixels(0, 0, width, height, 0x1908, 0x1401, buffer);
}

static size_t create_window(const char *title, int x, int y, int width, int height)
{
    (void)x;
    (void)y;

    if (!g_initialized)
    {
        LOG_ERROR("Platform not initialized. Cannot create window.");
        return 0;
    }

    g_window_width = width;
    g_window_height = height;

    if (g_use_surfaceless)
    {
        LOG_INFO("Creating virtual window in surfaceless mode: %dx%d", width, height);

        if (!platform_ctx.surfaceless_active)
        {
            LOG_ERROR("Surfaceless context not active. Did initialization fail?");
            return 0;
        }

        static bool resources_setup = false;
        if (!resources_setup)
        {

            platform_ctx.primary_window = (GLFWwindow *)0x1;

            AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
            if (gfx && gfx->setup_shared_window_resources)
            {
                gfx->setup_shared_window_resources();
                LOG_INFO("Shared window resources setup in surfaceless mode");
            }
            resources_setup = true;
        }

        size_t window_id = 1;

        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->setup_separate_window_resources)
        {
            gfx->setup_separate_window_resources(window_id);
            LOG_INFO("Separate window resources setup for window %zu", window_id);
        }

        return window_id;
    }

    LOG_INFO("Creating GLFW window: %dx%d", width, height);

    if (g_offscreen_mode)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
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

        if (!g_use_surfaceless)
        {
            glfwShowWindow(window);
            glfwFocusWindow(window);
            glfwSwapInterval(1);
        }

        const char *version = (const char *)glGetString(GL_VERSION);
        LOG_INFO("GLFW OpenGL/ES Version: %s", version ? version : "NULL");

        glfwSetCursorPosCallback(window, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
        glfwSetScrollCallback(window, glfw_scroll_callback);
        glfwSetKeyCallback(window, glfw_key_callback);
        glfwSetCharCallback(window, glfw_char_callback);

        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->setup_shared_window_resources)
        {
            gfx->setup_shared_window_resources();
        }
    }

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->setup_separate_window_resources)
    {
        gfx->setup_separate_window_resources(window_id);
    }

    return window_id;
}

static void make_context_current(size_t window_id)
{
    if (g_use_surfaceless)
    {
        if (platform_ctx.surfaceless_active)
        {

            eglMakeCurrent(platform_ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           platform_ctx.egl_context);
        }
        return;
    }

    if (platform_ctx.primary_window && platform_ctx.primary_window != (GLFWwindow *)0x1)
    {
        glfwMakeContextCurrent(platform_ctx.primary_window);
    }
}

static void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    if (g_use_surfaceless)
    {
        *window_width = g_window_width;
        *window_height = g_window_height;
        return;
    }

    if (platform_ctx.primary_window && platform_ctx.primary_window != (GLFWwindow *)0x1)
    {
        glfwGetFramebufferSize(platform_ctx.primary_window, window_width, window_height);
    }
    else
    {
        *window_width = g_window_width;
        *window_height = g_window_height;
    }
}

static void set_window_update_callback(void (*callback)(size_t window_id, void *data), void *data)
{
    platform_ctx.frame_callback = callback;
    platform_ctx.frame_callback_data = data;
}

static void request_window_update(size_t window_id)
{
    if (platform_ctx.frame_callback)
    {
        platform_ctx.frame_callback(window_id, platform_ctx.frame_callback_data);
    }
}

static bool run_event_loop()
{
    if (!g_initialized)
    {
        LOG_ERROR("Platform not initialized. Cannot run event loop.");
        return false;
    }

    if (g_use_surfaceless)
    {

        if (aroma_dirty_list_has_entries())
        {
            request_window_update(1);
        }
        return platform_ctx.surfaceless_active;
    }

    if (!platform_ctx.primary_window || platform_ctx.primary_window == (GLFWwindow *)0x1)
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
    if (g_use_surfaceless)
    {
        extern void glFlush(void);
        glFlush();
        return;
    }

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

    if (platform_ctx.primary_window && platform_ctx.primary_window != (GLFWwindow *)0x1)
    {
        glfwSwapBuffers(platform_ctx.primary_window);
    }
    else
    {

        extern void glFlush(void);
        glFlush();
    }
}

static void shutdown()
{
    LOG_INFO("Shutting down platform...");

    if (g_use_surfaceless)
    {
        shutdown_egl_surfaceless();
        platform_ctx.primary_window = NULL;
        g_initialized = false;
        return;
    }

    if (platform_ctx.primary_window && platform_ctx.primary_window != (GLFWwindow *)0x1)
    {
        glfwDestroyWindow(platform_ctx.primary_window);
        platform_ctx.primary_window = NULL;
    }
    glfwTerminate();
    g_initialized = false;

    LOG_INFO("Platform shutdown complete");
}

#ifdef AROMA_HAS_VULKAN
static bool glfw_create_vulkan_surface(size_t window_id, void *instance, void **surface)
{
    if (g_use_surfaceless)
    {
        LOG_WARNING("Vulkan surface creation not supported in surfaceless mode");
        return 0;
    }

    if (!platform_ctx.primary_window || platform_ctx.primary_window == (GLFWwindow *)0x1)
        return 0;
    VkResult err = glfwCreateWindowSurface((VkInstance)instance, platform_ctx.primary_window, NULL, (VkSurfaceKHR *)surface);
    return err == VK_SUCCESS ? 1 : 0;
}

static const char **glfw_get_vulkan_instance_extensions(uint32_t *count)
{
    if (g_use_surfaceless)
    {
        *count = 0;
        return NULL;
    }
    return glfwGetRequiredInstanceExtensions(count);
}
#endif

static void *glfw_get_native_window_ptr(size_t window_id)
{
    if (g_use_surfaceless || platform_ctx.primary_window == (GLFWwindow *)0x1)
    {
        return NULL;
    }
    return platform_ctx.primary_window;
}

static void *glfw_get_display()
{
    if (g_use_surfaceless && platform_ctx.surfaceless_active)
    {
        return (void *)platform_ctx.egl_display;
    }
    return NULL;
}

static void glfw_set_clipboard_text(const char *text)
{
    if (platform_ctx.primary_window && platform_ctx.primary_window != (GLFWwindow *)0x1)
        glfwSetClipboardString(platform_ctx.primary_window, text);
}

static bool glfw_get_clipboard_text(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return false;
    const char *clip = glfwGetClipboardString(platform_ctx.primary_window);
    if (!clip || !clip[0])
        return false;
    size_t len = strlen(clip);
    if (len >= buffer_size)
        len = buffer_size - 1;
    memcpy(buffer, clip, len);
    buffer[len] = '\0';
    return true;
}

static bool glfw_pick_folder(const char *default_path, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
        return false;

    nfdchar_t *out = NULL;
    nfdresult_t result = NFD_PickFolder(default_path, &out);
    if (result == NFD_OKAY && out) {
        size_t len = strlen(out);
        if (len >= out_size)
            len = out_size - 1;
        memcpy(out_path, out, len);
        out_path[len] = '\0';
        NFD_FreePath(out);
        return true;
    }
    if (out)
        NFD_FreePath(out);
    return false;
}

static bool glfw_pick_open_file(const char *default_path, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
        return false;

    nfdchar_t *out = NULL;
    nfdresult_t result = NFD_OpenDialog(NULL, default_path, &out);
    if (result == NFD_OKAY && out) {
        size_t len = strlen(out);
        if (len >= out_size)
            len = out_size - 1;
        memcpy(out_path, out, len);
        out_path[len] = '\0';
        NFD_FreePath(out);
        return true;
    }
    if (out)
        NFD_FreePath(out);
    return false;
}

static bool glfw_pick_save_file(const char *default_path, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
        return false;

    nfdchar_t *out = NULL;
    nfdresult_t result = NFD_SaveDialog(NULL, default_path, &out);
    if (result == NFD_OKAY && out) {
        size_t len = strlen(out);
        if (len >= out_size)
            len = out_size - 1;
        memcpy(out_path, out, len);
        out_path[len] = '\0';
        NFD_FreePath(out);
        return true;
    }
    if (out)
        NFD_FreePath(out);
    return false;
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
    .set_window_decorated = glfw_set_window_decorated,
    .begin_window_drag = glfw_begin_window_drag,
    .set_window_minimized = glfw_set_window_minimized,
    .set_window_maximized = glfw_set_window_maximized,
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
    .set_use_surfaceless = set_use_surfaceless,
    .read_pixels = read_pixels,
    .set_clipboard_text = glfw_set_clipboard_text,
    .get_clipboard_text = glfw_get_clipboard_text,
    .pick_folder = glfw_pick_folder,
    .pick_open_file = glfw_pick_open_file,
    .pick_save_file = glfw_pick_save_file,
};

#endif