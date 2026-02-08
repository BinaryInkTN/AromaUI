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
#ifndef AWINDOW_FLAG_FULLSCREEN
#define AWINDOW_FLAG_FULLSCREEN 0x00000400
#endif
#ifndef AWINDOW_FLAG_FORCE_NOT_FULLSCREEN
#define AWINDOW_FLAG_FORCE_NOT_FULLSCREEN 0x00000800
#endif
#ifndef AWINDOW_FLAG_LAYOUT_IN_SCREEN
#define AWINDOW_FLAG_LAYOUT_IN_SCREEN 0x00000100
#endif
#ifndef AWINDOW_FLAG_LAYOUT_NO_LIMITS
#define AWINDOW_FLAG_LAYOUT_NO_LIMITS 0x00000200
#endif
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

static void android_open_url(const char* url) {
    if (!g_app || !g_app->activity || !url) {
        LOGE("Cannot open URL: Invalid state or URL");
        return;
    }

    JNIEnv* env = NULL;
    JavaVM* vm = g_app->activity->vm;
    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    bool did_attach = false;
    
    if (status < 0) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
             LOGE("Failed to attach current thread for URL opening");
             return;
        }
        did_attach = true;
    }

    jclass activity_class = (*env)->GetObjectClass(env, g_app->activity->clazz);
    if (!activity_class) {
         LOGE("Failed to get activity class");
         if (did_attach) (*vm)->DetachCurrentThread(vm);
         return;
    }

    // Launch Browser via Intent (ACTION_VIEW)
    // Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
    // startActivity(intent);

    jstring jurl = (*env)->NewStringUTF(env, url);

    // 1. Uri.parse(url)
    jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
    jmethodID uri_parse = (*env)->GetStaticMethodID(env, uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
    jobject uri_obj = (*env)->CallStaticObjectMethod(env, uri_class, uri_parse, jurl);

    // 2. new Intent(Intent.ACTION_VIEW, uri)
    jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
    jfieldID action_view_field = (*env)->GetStaticFieldID(env, intent_class, "ACTION_VIEW", "Ljava/lang/String;");
    jobject action_view = (*env)->GetStaticObjectField(env, intent_class, action_view_field);
    
    jmethodID intent_ctor = (*env)->GetMethodID(env, intent_class, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
    jobject intent_obj = (*env)->NewObject(env, intent_class, intent_ctor, action_view, uri_obj);

    // 3. startActivity(intent)
    jmethodID start_activity = (*env)->GetMethodID(env, activity_class, "startActivity", "(Landroid/content/Intent;)V");
    (*env)->CallVoidMethod(env, g_app->activity->clazz, start_activity, intent_obj);

    (*env)->DeleteLocalRef(env, jurl);
    (*env)->DeleteLocalRef(env, uri_obj);
    (*env)->DeleteLocalRef(env, action_view);
    (*env)->DeleteLocalRef(env, intent_obj);
    (*env)->DeleteLocalRef(env, uri_class);
    (*env)->DeleteLocalRef(env, intent_class);
    (*env)->DeleteLocalRef(env, activity_class);

    if ((*env)->ExceptionCheck(env)) {
         (*env)->ExceptionDescribe(env);
         (*env)->ExceptionClear(env);
         LOGE("Exception occurred while launching browser intent");
    }

    if (did_attach) {
        (*vm)->DetachCurrentThread(vm);
    }
}

static void android_send_intent(int action_enum, const char* uri, const char* type, const void* extras, int extra_count) {
    if (!g_app || !g_app->activity) {
        LOGE("Cannot send intent: Invalid state");
        return;
    }

    const char* action = "android.intent.action.VIEW";
    switch (action_enum) {
        case 0: action = "android.intent.action.VIEW"; break; // AROMA_INTENT_VIEW
        case 1: action = "android.intent.action.SEND"; break; // AROMA_INTENT_SEND
        case 2: action = "android.intent.action.EDIT"; break; // AROMA_INTENT_EDIT
        case 3: action = "android.intent.action.DIAL"; break; // AROMA_INTENT_DIAL
        case 4: action = "android.intent.action.CALL"; break; // AROMA_INTENT_CALL
        default: break;
    }

    JNIEnv* env = NULL;
    JavaVM* vm = g_app->activity->vm;
    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    bool did_attach = false;
    
    if (status < 0) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
             LOGE("Failed to attach current thread for intent");
             return;
        }
        did_attach = true;
    }

    // Classes and Methods
    jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
    jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
    jclass activity_class = (*env)->GetObjectClass(env, g_app->activity->clazz);

    if (!intent_class || !uri_class || !activity_class) {
         LOGE("Failed to find required classes for intent");
         if (did_attach) (*vm)->DetachCurrentThread(vm);
         return;
    }

    // Intent(String action) constructor
    jmethodID intent_ctor = (*env)->GetMethodID(env, intent_class, "<init>", "(Ljava/lang/String;)V");
    jstring jaction = (*env)->NewStringUTF(env, action);
    jobject intent_obj = (*env)->NewObject(env, intent_class, intent_ctor, jaction);

    // Handle URI and Type
    jobject uri_obj = NULL;
    if (uri) {
        jstring juri = (*env)->NewStringUTF(env, uri);
        jmethodID uri_parse = (*env)->GetStaticMethodID(env, uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        uri_obj = (*env)->CallStaticObjectMethod(env, uri_class, uri_parse, juri);
        (*env)->DeleteLocalRef(env, juri);
    }

    if (uri_obj && type) {
        // setDataAndType(Uri data, String type)
        jstring jtype = (*env)->NewStringUTF(env, type);
        jmethodID set_data_and_type = (*env)->GetMethodID(env, intent_class, "setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;");
        (*env)->CallObjectMethod(env, intent_obj, set_data_and_type, uri_obj, jtype);
        (*env)->DeleteLocalRef(env, jtype);
    } 
    else if (uri_obj) {
        // setData(Uri data)
        jmethodID set_data = (*env)->GetMethodID(env, intent_class, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;");
        (*env)->CallObjectMethod(env, intent_obj, set_data, uri_obj);
    }
    else if (type) {
         // setType(String type)
         jstring jtype = (*env)->NewStringUTF(env, type);
         jmethodID set_type = (*env)->GetMethodID(env, intent_class, "setType", "(Ljava/lang/String;)Landroid/content/Intent;");
         (*env)->CallObjectMethod(env, intent_obj, set_type, jtype);
         (*env)->DeleteLocalRef(env, jtype);
    }

    // Handle Extras
    if (extras && extra_count > 0) {
        const AromaIntentExtra* extra_list = (const AromaIntentExtra*)extras;
        // putExtra(String, String)
        jmethodID put_extra = (*env)->GetMethodID(env, intent_class, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        
        if (put_extra) {
            for (int i = 0; i < extra_count; i++) {
                if (extra_list[i].key && extra_list[i].string_value) {
                    jstring k = (*env)->NewStringUTF(env, extra_list[i].key);
                    jstring v = (*env)->NewStringUTF(env, extra_list[i].string_value);
                    jobject res = (*env)->CallObjectMethod(env, intent_obj, put_extra, k, v);
                    if (res) (*env)->DeleteLocalRef(env, res);
                    (*env)->DeleteLocalRef(env, k);
                    (*env)->DeleteLocalRef(env, v);
                }
            }
        }
    }

    // startActivity(Intent intent)
    jmethodID start_activity = (*env)->GetMethodID(env, activity_class, "startActivity", "(Landroid/content/Intent;)V");
    (*env)->CallVoidMethod(env, g_app->activity->clazz, start_activity, intent_obj);

    if ((*env)->ExceptionCheck(env)) {
         (*env)->ExceptionDescribe(env);
         (*env)->ExceptionClear(env);
         LOGE("Exception while sending intent");
    }

    // Cleanup
    if (uri_obj) (*env)->DeleteLocalRef(env, uri_obj);
    (*env)->DeleteLocalRef(env, intent_obj);
    (*env)->DeleteLocalRef(env, jaction);
    (*env)->DeleteLocalRef(env, activity_class);
    (*env)->DeleteLocalRef(env, uri_class);
    (*env)->DeleteLocalRef(env, intent_class);

    if (did_attach) {
        (*vm)->DetachCurrentThread(vm);
    }
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
    LOGI("Window surface resized: %dx%d -> %dx%d", g_width, g_height, w, h);

    g_width = w;
    g_height = h;

    glViewport(0, 0, g_width, g_height);

    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    
    for (int i = 0; i < AROMA_MAX_WINDOWS; i++) {
        if (g_windows[i].is_active && g_windows[i].root_node) {
             struct AromaWindow* win_widget = (struct AromaWindow*)g_windows[i].root_node->node_widget_ptr;
             if (win_widget) {
                 win_widget->rect.width = g_width;
                 win_widget->rect.height = g_height;
             }
             
             aroma_node_update_layout(g_windows[i].root_node, 0, 0, g_width, g_height);
             
             // Invalidate to ensure redraw on new surface
             aroma_node_invalidate(g_windows[i].root_node);
             
             AromaEvent* event = aroma_event_create(EVENT_TYPE_WINDOW_RESIZE, g_windows[i].root_node->node_id);
             if (event) {
                 event->data.resize.width = g_width;
                 event->data.resize.height = g_height;
                 aroma_event_queue(event);
             }
        }
    }
}

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
            int32_t key_code = AKeyEvent_getKeyCode(event);
            char ch = 0;
            if (key_code >= AKEYCODE_0 && key_code <= AKEYCODE_9) ch = '0' + (key_code - AKEYCODE_0);
            else if (key_code >= AKEYCODE_A && key_code <= AKEYCODE_Z) {
                ch = 'a' + (key_code - AKEYCODE_A);
                int meta = AKeyEvent_getMetaState(event);
                if (meta & AMETA_SHIFT_ON) ch = 'A' + (key_code - AKEYCODE_A);
            }
            else if (key_code == AKEYCODE_SPACE) ch = ' ';
            else if (key_code == AKEYCODE_DEL) ch = 8; 
            else if (key_code == AKEYCODE_ENTER) ch = 10;
            else if (key_code == AKEYCODE_PERIOD) ch = '.';
            else if (key_code == AKEYCODE_COMMA) ch = ',';
            else if (key_code == AKEYCODE_MINUS) ch = '-';
            else if (key_code == AKEYCODE_AT) ch = '@';

            if (ch != 0) {
                AromaNode* focused = aroma_ui_get_focused_node();
                if (focused) {
                    AromaEvent* evt = aroma_event_create(EVENT_TYPE_KEY_PRESS, focused->node_id);
                    if (evt) {
                        evt->data.key.key_code = ch;
                        aroma_event_queue(evt);
                    }
                    return 1;
                }
            }
        }
        return 0;
    }

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

    ANativeActivity_setWindowFlags(app->activity, 
        AWINDOW_FLAG_FORCE_NOT_FULLSCREEN, 
        AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_LAYOUT_IN_SCREEN | AWINDOW_FLAG_LAYOUT_NO_LIMITS
    );

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
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    EGLint num;
    if (!eglChooseConfig(dpy, attribs, &config, 1, &num) || num == 0) {
        LOGE("Failed to choose config");
        return -1;
    }

    EGLint format;
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, app->window, NULL);
    if (surf == EGL_NO_SURFACE) {
        LOGE("Failed to create window surface");
        return -1;
    }

    EGLContext ctx = context;
    if (ctx == EGL_NO_CONTEXT) {
        const EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
        if (ctx == EGL_NO_CONTEXT) {
            LOGE("Failed to create context");
            return -1;
        }
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
         LOGE("Failed to make current");
        return -1;
    }

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

static void term_display_surface_only(void) {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
    }
    surface = EGL_NO_SURFACE;
    g_has_window = false;
    g_width = 0;
    g_height = 0;
}

static void term_display(void) {
    term_display_surface_only();
    if (display != EGL_NO_DISPLAY) {
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        eglTerminate(display);
    }

    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                // Don't kill context if we don't have to
                term_display_surface_only();
                init_display(app);
            }
            break;
            
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            term_display_surface_only();
            if (app->window)
                init_display(app);
            break;
        
        // ... (APP_CMD_CONTENT_RECT_CHANGED, etc) ...

        case APP_CMD_GAINED_FOCUS:
        case APP_CMD_RESUME:
            if (app->window && !g_has_window) {
                term_display_surface_only();
                init_display(app);
            } else if (g_has_window) {
                 update_surface_size();
                 extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
                 for (int i = 0; i < AROMA_MAX_WINDOWS; i++) {
                    if (g_windows[i].is_active && g_windows[i].root_node) {
                         aroma_node_invalidate(g_windows[i].root_node);
                    }
                 }
            }
            break;
            
        case APP_CMD_TERM_WINDOW:
            term_display_surface_only();
            break;
            
        case APP_CMD_DESTROY:
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
    g_update_callback = NULL;
    g_update_callback_data = NULL;

    while (context == EGL_NO_CONTEXT && !g_app->destroyRequested) {
        int events;
        struct android_poll_source* source;
        if (ALooper_pollAll(-1, NULL, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(g_app, source);
            }
        }
    }

    if (g_app->destroyRequested) {
        return 0;
    }

    return 1;
}

void shutdown(void) {
    term_display();
}

size_t create_window(const char* title, int x, int y, int width, int height) {
    return 0;
}

void make_context_current(size_t window_id) {
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT) {
        if (eglGetCurrentContext() != context || eglGetCurrentSurface(EGL_DRAW) != surface) {
            if (!eglMakeCurrent(display, surface, surface, context)) {
                EGLint error = eglGetError();
                LOGE("make_context_current failed: 0x%x", error);
            }
        }
    } else {
        LOGE("Attempted to make context current but display/surface/context is invalid");
    }
}

void set_window_update_callback(void (*callback)(size_t, void*), void* data) {
    g_update_callback = callback;
    g_update_callback_data = data;
}

void get_window_size(size_t window_id, int* window_width, int* window_height) {
    if (!g_has_window) {
        *window_width = 0;
        *window_height = 0;
        return;
    }
    *window_width = g_width;
    *window_height = g_height;
}

void set_fullscreen(size_t window_id, bool enabled) {
    if (!g_app || !g_app->activity) return;
    
    if (enabled) {
        // Add FULLSCREEN, remove nothing
        ANativeActivity_setWindowFlags(g_app->activity, AWINDOW_FLAG_FULLSCREEN, 0);
    } else {
        // Remove FULLSCREEN
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
        if (g_update_callback) {
            g_update_callback(0, g_update_callback_data);
        } else {
             // Debug: Why no callback? 
             // LOGI("Loop running but no update callback set!");
        }
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

static void android_show_keyboard(void) {
    if (!g_app || !g_app->activity) {
        LOGE("Cannot show keyboard: g_app is NULL");
        return;
    }
    
    LOGI("Requesting soft keyboard via JNI");
    
    JNIEnv* env = NULL;
    (*g_app->activity->vm)->AttachCurrentThread(g_app->activity->vm, &env, NULL);
    
    jclass activityClass = (*env)->GetObjectClass(env, g_app->activity->clazz);
    
    // Context.INPUT_METHOD_SERVICE = "input_method"
    jmethodID getSystemService = (*env)->GetMethodID(env, activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, g_app->activity->clazz, getSystemService, serviceName);
    (*env)->DeleteLocalRef(env, serviceName);
    
    if (imm) {
        // Get DecorView for token
        jmethodID getWindow = (*env)->GetMethodID(env, activityClass, "getWindow", "()Landroid/view/Window;");
        jobject window = (*env)->CallObjectMethod(env, g_app->activity->clazz, getWindow);
        
        jclass windowClass = (*env)->FindClass(env, "android/view/Window");
        jmethodID getDecorView = (*env)->GetMethodID(env, windowClass, "getDecorView", "()Landroid/view/View;");
        jobject decorView = (*env)->CallObjectMethod(env, window, getDecorView);
        
        // InputMethodManager.showSoftInput(View, int flags)
        jclass immClass = (*env)->FindClass(env, "android/view/inputmethod/InputMethodManager");
        jmethodID showSoftInput = (*env)->GetMethodID(env, immClass, "showSoftInput", "(Landroid/view/View;I)Z");
        
        // Flags: SHOW_IMPLICIT = 1, SHOW_FORCED = 2
        (*env)->CallBooleanMethod(env, imm, showSoftInput, decorView, 2);
        
        LOGI("Keyboard show requested via JNI");
    } else {
        LOGE("Failed to get InputMethodManager");
    }
    
    (*g_app->activity->vm)->DetachCurrentThread(g_app->activity->vm);
}

static void android_hide_keyboard(void) {
    if (!g_app || !g_app->activity) return;
    
    JNIEnv* env = NULL;
    (*g_app->activity->vm)->AttachCurrentThread(g_app->activity->vm, &env, NULL);
    
    jclass activityClass = (*env)->GetObjectClass(env, g_app->activity->clazz);
    jmethodID getSystemService = (*env)->GetMethodID(env, activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, g_app->activity->clazz, getSystemService, serviceName);
    (*env)->DeleteLocalRef(env, serviceName);
    
    if (imm) {
        jmethodID getWindow = (*env)->GetMethodID(env, activityClass, "getWindow", "()Landroid/view/Window;");
        jobject window = (*env)->CallObjectMethod(env, g_app->activity->clazz, getWindow);
        jclass windowClass = (*env)->FindClass(env, "android/view/Window");
        jmethodID getDecorView = (*env)->GetMethodID(env, windowClass, "getDecorView", "()Landroid/view/View;");
        jobject decorView = (*env)->CallObjectMethod(env, window, getDecorView);
        
        jclass viewClass = (*env)->FindClass(env, "android/view/View");
        jmethodID getWindowToken = (*env)->GetMethodID(env, viewClass, "getWindowToken", "()Landroid/os/IBinder;");
        jobject token = (*env)->CallObjectMethod(env, decorView, getWindowToken);
        
        jclass immClass = (*env)->FindClass(env, "android/view/inputmethod/InputMethodManager");
        jmethodID hideSoftInput = (*env)->GetMethodID(env, immClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
        
        (*env)->CallBooleanMethod(env, imm, hideSoftInput, token, 0);
        LOGI("Keyboard hide requested via JNI");
    }
    
    (*g_app->activity->vm)->DetachCurrentThread(g_app->activity->vm);
}

void platform_set_android_app(void* state) {
    g_app = (struct android_app*)state;
}

AromaPlatformInterface aroma_platform_android = {
    .initialize = initialize,
    .shutdown = shutdown,
    .create_window = create_window,
    .show_keyboard = android_show_keyboard,
    .hide_keyboard = android_hide_keyboard,
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
    .set_fullscreen = set_fullscreen,
    .open_url = android_open_url,
    .android_send_intent = android_send_intent
};

#endif
