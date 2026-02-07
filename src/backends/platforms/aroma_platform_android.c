/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */



#ifdef __ANDROID__
#include "aroma_platform_interface.h"
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdbool.h>
#include <jni.h>

#include "../aroma_abi.h"
#include "../graphics/aroma_graphics_interface.h"
#include "core/aroma_logger.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"
#include "widgets/aroma_window.h"

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

static void get_physical_screen_info(struct android_app* app, int* width, int* height) {
    JNIEnv* env = NULL;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);

    jclass activity_class = (*env)->GetObjectClass(env, app->activity->clazz);
    jmethodID get_wm_method = (*env)->GetMethodID(env, activity_class, "getWindowManager", "()Landroid/view/WindowManager;");
    jobject wm_obj = (*env)->CallObjectMethod(env, app->activity->clazz, get_wm_method);

    jclass wm_class = (*env)->FindClass(env, "android/view/WindowManager");
    jmethodID get_display_method = (*env)->GetMethodID(env, wm_class, "getDefaultDisplay", "()Landroid/view/Display;");
    jobject display_obj = (*env)->CallObjectMethod(env, wm_obj, get_display_method);

    jclass dm_class = (*env)->FindClass(env, "android/util/DisplayMetrics");
    jmethodID dm_ctor = (*env)->GetMethodID(env, dm_class, "<init>", "()V");
    jobject dm_obj = (*env)->NewObject(env, dm_class, dm_ctor);

    jclass display_class = (*env)->FindClass(env, "android/view/Display");
    jmethodID get_real_metrics_method = (*env)->GetMethodID(env, display_class, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V");
    (*env)->CallVoidMethod(env, display_obj, get_real_metrics_method, dm_obj);

    jfieldID width_field = (*env)->GetFieldID(env, dm_class, "widthPixels", "I");
    jfieldID height_field = (*env)->GetFieldID(env, dm_class, "heightPixels", "I");

    if (width) *width = (*env)->GetIntField(env, dm_obj, width_field);
    if (height) *height = (*env)->GetIntField(env, dm_obj, height_field);

    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
}

static void update_surface_size(void) {
    if (!g_app || !g_app->window) return;

    int phys_w = 0, phys_h = 0;
    get_physical_screen_info(g_app, &phys_w, &phys_h);

    if (phys_w > 0 && phys_h > 0) {
        g_width = phys_w;
        g_height = phys_h;
    } else {
        g_width = ANativeWindow_getWidth(g_app->window);
        g_height = ANativeWindow_getHeight(g_app->window);
    }
    
    glViewport(0, 0, g_width, g_height);

    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    extern int g_window_count;
    
    for(int i = 0; i < g_window_count; i++) {
        if(g_windows[i].root_node && g_windows[i].window) {
            g_windows[i].window->rect.width = g_width;
            g_windows[i].window->rect.height = g_height;
            aroma_node_invalidate(g_windows[i].root_node);
        }
    }
}

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return 0;

    int action = AMotionEvent_getAction(event);
    int actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    int index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    float x = AMotionEvent_getX(event, index);
    float y = AMotionEvent_getY(event, index);

    switch (actionMasked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            aroma_event_handle_pointer_move((int)x, (int)y, true);
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            aroma_event_handle_pointer_move((int)x, (int)y, false);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            x = AMotionEvent_getX(event, 0);
            y = AMotionEvent_getY(event, 0);
            aroma_event_handle_pointer_move((int)x, (int)y, true);
            break;
    }
    return 1;
}

static int init_display(struct android_app* app) {
    if (!app->window) return -1;

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) return -1;
    if (!eglInitialize(dpy, NULL, NULL)) return -1;

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(dpy, attribs, &config, 1, &numConfigs) || numConfigs == 0)
        return -1;

    EGLint format;
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);
    
    int phys_w, phys_h;
    get_physical_screen_info(app, &phys_w, &phys_h);
    ANativeWindow_setBuffersGeometry(app->window, phys_w, phys_h, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, app->window, NULL);
    if (surf == EGL_NO_SURFACE) return -1;

    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) return -1;

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) return -1;

    display = dpy;
    context = ctx;
    surface = surf;
    g_width = phys_w;
    g_height = phys_h;
    g_has_window = true;

    glViewport(0, 0, g_width, g_height);

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx) {
        if (gfx->setup_shared_window_resources) gfx->setup_shared_window_resources();
        if (gfx->setup_separate_window_resources) gfx->setup_separate_window_resources(0);
    }

    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    extern int g_window_count;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].root_node && g_windows[i].window) {
            g_windows[i].window->rect.width = g_width;
            g_windows[i].window->rect.height = g_height;
            aroma_node_invalidate(g_windows[i].root_node);
        }
    }

    return 0;
}

static void term_display(void) {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    surface = EGL_NO_SURFACE;
    g_has_window = false;
    g_width = 0;
    g_height = 0;
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) init_display(app);
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            update_surface_size();
            break;
        case APP_CMD_TERM_WINDOW:
            term_display();
            break;
    }
}

int initialize(void) {
    if (!g_app) return 0;
    g_app->onAppCmd = handle_cmd;
    g_app->onInputEvent = handle_input;
    return 1;
}

void shutdown(void) {
    term_display();
}

size_t create_window(const char* title, int x, int y, int width, int height) {
    return 0;
}

void make_context_current(size_t window_id) {
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT)
        eglMakeCurrent(display, surface, surface, context);
}

void set_window_update_callback(void (*callback)(size_t window_id, void *data), void* data) {
    g_update_callback = callback;
    g_update_callback_data = data;
}

void get_window_size(size_t window_id, int *window_width, int *window_height) {
    if (!g_has_window) {
        *window_width = 0;
        *window_height = 0;
        return;
    }
    *window_width = g_width;
    *window_height = g_height;
}

void request_window_update(size_t window_id) {}

bool run_event_loop(void) {
    int events;
    struct android_poll_source* source;
    while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0) {
        if (source) source->process(g_app, source);
        if (g_app->destroyRequested) return false;
    }
    if (g_has_window && g_update_callback) {
        g_update_callback(0, g_update_callback_data);
    }
    return true;
}

void swap_buffers(size_t window_id) {
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
        eglSwapBuffers(display, surface);
}

void* get_tft_context(void) { return NULL; }
void call_flush_function_ptr(void (*flush_fn)(struct AromaDrawList* list, size_t window_id, int x, int y, int width, int height), void* list) {}
void tft_mark_tiles_dirty(int y, int h) {}
void set_clear_color(uint16_t color) {}

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

#endif