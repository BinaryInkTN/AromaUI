#ifdef __ANDROID__
#include "aroma_platform_interface.h"
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdbool.h>
#include <time.h>

#include "../aroma_abi.h"
#include "core/aroma_logger.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"
void aroma_android_set_app(struct android_app* state);

static struct android_app* g_app = NULL;
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;
static EGLConfig config;
static int g_width = 0;
static int g_height = 0;
static bool g_has_window = false;

static void (*g_update_callback)(size_t window_id, void *data) = NULL;
static void* g_update_callback_data = NULL;

#define LOG_TAG "AromaUI-Android"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

// Android Logging wrapper for Aroma Logger
void android_log_wrapper(int level, const char* msg) {
    switch(level) {
        case 0: __android_log_print(ANDROID_LOG_DEBUG, "AromaUI", "%s", msg); break;
        case 1: __android_log_print(ANDROID_LOG_INFO, "AromaUI", "%s", msg); break;
        case 2: __android_log_print(ANDROID_LOG_WARN, "AromaUI", "%s", msg); break;
        case 3: __android_log_print(ANDROID_LOG_ERROR, "AromaUI", "%s", msg); break;
        default: __android_log_print(ANDROID_LOG_INFO, "AromaUI", "%s", msg); break;
    }
}

// Event input wrapper
static void handle_input_mouse(int action, float x, float y) {
    bool is_down = (action == AMOTION_EVENT_ACTION_DOWN) || (action == AMOTION_EVENT_ACTION_MOVE);
    // On Android, we just send "pointer move" with down flag.
    // If we want click events, we need the event system to synthesize them or handle up/down better.
    // AromaEvent system usually expects move events and handles clicks internally?
    // Let's check aroma_event_handle_pointer_move...
    // Assuming (x,y, is_down) covers hover vs drag/click.
    
    aroma_event_handle_pointer_move((int)x, (int)y, is_down);
    
    // Also, if action is UP, we should probably send a move with is_down=false at that location
    // The current code does:
    // DOWN -> move(x, y, true)
    // UP   -> move(x, y, false)
    // MOVE -> move(x, y, true)
}

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        
        switch(action) {
            case AMOTION_EVENT_ACTION_DOWN:
                aroma_event_handle_pointer_move((int)x, (int)y, true);
                break;
            case AMOTION_EVENT_ACTION_UP:
                aroma_event_handle_pointer_move((int)x, (int)y, false);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                aroma_event_handle_pointer_move((int)x, (int)y, true);
                break;
        }
        return 1;
    }
    return 0;
}

static int init_display(struct android_app* app) {
    // Initialize EGL
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, 0, 0);

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };
    EGLint w, h, format;
    EGLint numConfigs;
    eglChooseConfig(dpy, attribs, &config, 1, &numConfigs);
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);

    ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, app->window, NULL);
    
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, contextAttribs);

    if (eglMakeCurrent(dpy, surf, surf, ctx) == EGL_FALSE) {
        LOGE("Unable to eglMakeCurrent");
        return -1;
    }

    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    display = dpy;
    context = ctx;
    surface = surf;
    g_width = w;
    g_height = h;
    g_has_window = true;

    // Initialize Graphics Backend Resources (Shaders, VBOs, etc.)
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx) {
        if (gfx->setup_shared_window_resources) {
            if (!gfx->setup_shared_window_resources()) {
                LOGE("Failed to setup shared graphics resources");
            } else {
                LOGI("Shared graphics resources setup success");
            }
        }
        
        // We only support one window on Android (id: 1)
        if (gfx->setup_separate_window_resources) {
            if (!gfx->setup_separate_window_resources(1)) {
                LOGE("Failed to setup separate resources for window 1");
            } else {
                 LOGI("Separate graphics resources setup success for window 1");
            }
        }
    }

    // Force invalidate root nodes to ensure initial draw
    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    extern int g_window_count;
    
    for(int i = 0; i < g_window_count; i++) {
        if(g_windows[i].root_node) {
            aroma_node_invalidate(g_windows[i].root_node);
            LOGI("Invalidated window %d after init", (int)i);
        }
    }

    return 0;
}

static void term_display() {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    surface = EGL_NO_SURFACE;
    g_has_window = false;
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != NULL) {
                init_display(app);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            term_display();
            break;
        case APP_CMD_GAINED_FOCUS:
            // Resume
            break;
        case APP_CMD_LOST_FOCUS:
            // Pause
            break;
    }
}


int initialize(void) {
    LOGI("AromaUI Android Initialize");
    if (!g_app) {
        LOGE("Android App state not set. Call aroma_android_set_app first.");
        return 0;
    }
    // Set logger hack?
    // aroma_logger_set_callback(android_log_wrapper); 
    
    g_app->onAppCmd = handle_cmd;
    g_app->onInputEvent = handle_input;

    return 1;
}

void shutdown(void) {
    term_display();
}

size_t create_window(const char* title, int x, int y, int width, int height) {
    // On Android, we just return a dummy ID 1, as we only support one window (the Activity)
    // If the window isn't created yet, we wait for it in the event loop
    return 1;
}

void make_context_current(size_t window_id) {
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT) {
        eglMakeCurrent(display, surface, surface, context);
    }
}

void set_window_update_callback(void (*callback)(size_t window_id, void *data), void* data) {
    g_update_callback = callback;
    g_update_callback_data = data;
}

void get_window_size(size_t window_id, int *window_width, int *window_height) {
    if (g_has_window) {
        *window_width = g_width;
        *window_height = g_height;
    } else {
        *window_width = 0;
        *window_height = 0;
    }
}

void request_window_update(size_t window_id) {
    // If we had an invalidation loop we could signal it.
    // For now we assume we redraw every frame in the loop
}

bool run_event_loop(void) {
    int events;
    struct android_poll_source* source;
    
    // Poll events
    while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0) {
        if (source != NULL) {
            source->process(g_app, source);
        }
        if (g_app->destroyRequested != 0) {
            return false;
        }
    }

    if (g_has_window) {
        if (g_update_callback) {
            g_update_callback(1, g_update_callback_data);
        }
    }
    return true;
}

void swap_buffers(size_t window_id) {
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
        eglSwapBuffers(display, surface);
    }
}

void* get_tft_context(void) { return NULL; }
void call_flush_function_ptr(void (*flush_fn)(struct AromaDrawList* list, size_t window_id, int x, int y, int width, int height), void* list) {}
void tft_mark_tiles_dirty(int y, int h) {}
void set_clear_color(uint16_t color) {}


// void aroma_android_set_app(struct android_app* state) {
//    g_app = state;
// }

void platform_set_android_app(void* state) {
    g_app = (struct android_app*)state;
}

AromaPlatformInterface aroma_platform_android = {
    .initialize = initialize,
    .shutdown = shutdown,
    .create_window = create_window,
    .make_context_current = make_context_current,
    .set_window_update_callback = set_window_update_callback,
    .get_window_size = get_window_size,
    .request_window_update = request_window_update,
    .run_event_loop = run_event_loop,
    .swap_buffers = swap_buffers,
    .get_tft_context = get_tft_context,
    .call_flush_function_ptr = call_flush_function_ptr,
    .tft_mark_tiles_dirty = tft_mark_tiles_dirty,
    .set_clear_color = set_clear_color,
    .set_android_app = platform_set_android_app
};

// void aroma_android_set_app(struct android_app* state) {
//    g_app = state;
// }

#endif
