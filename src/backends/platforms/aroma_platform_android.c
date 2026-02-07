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
#include <android/native_window.h>
#include <android/native_activity.h>
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

static int g_phys_width = 0;
static int g_phys_height = 0;
static bool g_phys_cached = false;

static void (*g_update_callback)(size_t window_id, void *data) = NULL;
static void* g_update_callback_data = NULL;

#define LOG_TAG "AromaUI-Android"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

static void cache_physical_screen_info(struct android_app* app) {
    if (g_phys_cached || !app || !app->activity) return;

    JNIEnv* env = NULL;
    if ((*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL) != JNI_OK)
        return;

    jclass activity_class = (*env)->GetObjectClass(env, app->activity->clazz);
    jmethodID get_wm = (*env)->GetMethodID(env, activity_class, "getWindowManager", "()Landroid/view/WindowManager;");
    jobject wm = (*env)->CallObjectMethod(env, app->activity->clazz, get_wm);

    jclass wm_class = (*env)->FindClass(env, "android/view/WindowManager");
    jmethodID get_display = (*env)->GetMethodID(env, wm_class, "getDefaultDisplay", "()Landroid/view/Display;");
    jobject display_obj = (*env)->CallObjectMethod(env, wm, get_display);

    jclass dm_class = (*env)->FindClass(env, "android/util/DisplayMetrics");
    jmethodID dm_ctor = (*env)->GetMethodID(env, dm_class, "<init>", "()V");
    jobject dm = (*env)->NewObject(env, dm_class, dm_ctor);

    jclass display_class = (*env)->FindClass(env, "android/view/Display");
    jmethodID get_real_metrics = (*env)->GetMethodID(env, display_class, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V");
    (*env)->CallVoidMethod(env, display_obj, get_real_metrics, dm);

    jfieldID w_field = (*env)->GetFieldID(env, dm_class, "widthPixels", "I");
    jfieldID h_field = (*env)->GetFieldID(env, dm_class, "heightPixels", "I");

    g_phys_width = (*env)->GetIntField(env, dm, w_field);
    g_phys_height = (*env)->GetIntField(env, dm, h_field);

    g_phys_cached = true;

    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
}

static void update_surface_size(void) {
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE)
        return;

    EGLint w = 0, h = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    if (w <= 0 || h <= 0)
        return;

    if (w == g_width && h == g_height)
        return;

    g_width = w;
    g_height = h;

    glViewport(0, 0, g_width, g_height);

    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    extern int g_window_count;

}

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return 0;

    int action = AMotionEvent_getAction(event);
    int masked = action & AMOTION_EVENT_ACTION_MASK;
    int index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    float x = AMotionEvent_getX(event, index);
    float y = AMotionEvent_getY(event, index);

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            aroma_event_handle_pointer_move((int)x, (int)y, true);
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            aroma_event_handle_pointer_move((int)x, (int)y, false);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            aroma_event_handle_pointer_move(
                (int)AMotionEvent_getX(event, 0),
                (int)AMotionEvent_getY(event, 0),
                true
            );
            break;
    }
    return 1;
}

static int init_display(struct android_app* app) {
    if (!app || !app->window)
        return -1;

    cache_physical_screen_info(app);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY)
        return -1;

    if (!eglInitialize(dpy, NULL, NULL))
        return -1;

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint num;
    if (!eglChooseConfig(dpy, attribs, &config, 1, &num) || num == 0)
        return -1;

    EGLint format;
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, app->window, NULL);
    if (surf == EGL_NO_SURFACE)
        return -1;

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT)
        return -1;

    if (!eglMakeCurrent(dpy, surf, surf, ctx))
        return -1;

    display = dpy;
    surface = surf;
    context = ctx;
    g_has_window = true;

    update_surface_size();

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx) {
        if (gfx->setup_shared_window_resources)
            gfx->setup_shared_window_resources();
        if (gfx->setup_separate_window_resources)
            gfx->setup_separate_window_resources(0);
    }

    return 0;
}

static void term_display(void) {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
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
            if (app->window)
                init_display(app);
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
            update_surface_size();
            break;
        case APP_CMD_TERM_WINDOW:
            term_display();
            break;
    }
}

int initialize(void) {
    if (!g_app)
        return 0;
    
    cache_physical_screen_info(g_app);

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

void set_window_update_callback(void (*callback)(size_t, void*), void* data) {
    g_update_callback = callback;
    g_update_callback_data = data;
}

void get_window_size(size_t window_id, int* window_width, int* window_height) {
    if (!g_has_window) {
        if (g_phys_cached) {
            *window_width = g_phys_width;
            *window_height = g_phys_height;
        } else {
            *window_width = 0;
            *window_height = 0;
        }
        return;
    }
    *window_width = g_width;
    *window_height = g_height;
}

void set_fullscreen(size_t window_id, bool enabled) {
    if (!g_app || !g_app->activity) return;
    if (enabled) {
        ANativeActivity_setWindowFlags(g_app->activity, AWINDOW_FLAG_FULLSCREEN, 0);
    } else {
        ANativeActivity_setWindowFlags(g_app->activity, 0, AWINDOW_FLAG_FULLSCREEN);
    }
}

void request_window_update(size_t window_id) {}

bool run_event_loop(void) {
    int events;
    struct android_poll_source* source;

    while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0) {
        if (source)
            source->process(g_app, source);
        if (g_app->destroyRequested)
            return false;
    }

    if (g_has_window) {
        update_surface_size();
        if (g_update_callback)
            g_update_callback(0, g_update_callback_data);
    }

    return true;
}

void swap_buffers(size_t window_id) {
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
        eglSwapBuffers(display, surface);
}

void* get_tft_context(void) { return NULL; }
void call_flush_function_ptr(void (*flush_fn)(struct AromaDrawList*, size_t, int, int, int, int), void* list) {}
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
    .set_android_app = platform_set_android_app,
    .set_fullscreen = set_fullscreen
};

#endif
