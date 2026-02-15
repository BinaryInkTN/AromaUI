
#ifdef __ANDROID__
#include "aroma_platform_interface.h"
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/native_activity.h>
#include "aroma_android.h"
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
#ifndef AWINDOW_FLAG_KEEP_SCREEN_ON
#define AWINDOW_FLAG_KEEP_SCREEN_ON 0x00000080
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
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

#ifndef EGL_SWAP_BEHAVIOR_PRESERVED_BIT
#define EGL_SWAP_BEHAVIOR_PRESERVED_BIT 0x0400
#endif

static struct android_app *g_app = NULL;
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
static bool g_window_flags_set = false;
static void (*g_update_callback)(size_t window_id, void *data) = NULL;
static void *g_update_callback_data = NULL;

#define LOG_TAG "AromaUI-Android"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

typedef struct
{
    jclass helper_class;
    jmethodID show_toast;
    jmethodID bt_get_paired;
    jmethodID bt_connect;
    jmethodID bt_disconnect;
    jmethodID bt_send;
    jmethodID bt_is_connected;
    bool initialized;
} AromaHelperCache;

static AromaHelperCache g_helper_cache = {0};

static bool ensure_aroma_helper_initialized(JNIEnv *env)
{
    if (g_helper_cache.initialized && g_helper_cache.helper_class != NULL)
    {
        return true;
    }

    memset(&g_helper_cache, 0, sizeof(g_helper_cache));

    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        LOGE("Cannot find AromaHelper: no activity");
        return false;
    }

    jclass activityClass = (*env)->GetObjectClass(env, activity);
    jmethodID getClassLoader = (*env)->GetMethodID(env, activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");

    if (!getClassLoader)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        LOGE("Failed to get class loader method");
        return false;
    }

    jobject classLoader = (*env)->CallObjectMethod(env, activity, getClassLoader);
    if (!classLoader || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        LOGE("Failed to get class loader");
        return false;
    }

    jclass classLoaderClass = (*env)->GetObjectClass(env, classLoader);
    jmethodID loadClass = (*env)->GetMethodID(env, classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

    if (!loadClass)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, classLoaderClass);
        (*env)->DeleteLocalRef(env, classLoader);
        (*env)->DeleteLocalRef(env, activityClass);
        LOGE("Failed to get loadClass method");
        return false;
    }

    jstring jclassName = (*env)->NewStringUTF(env, "AromaHelper");
    jclass helper = (jclass)(*env)->CallObjectMethod(env, classLoader, loadClass, jclassName);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jclassName);
        (*env)->DeleteLocalRef(env, classLoaderClass);
        (*env)->DeleteLocalRef(env, classLoader);
        (*env)->DeleteLocalRef(env, activityClass);
        LOGE("AromaHelper class not found");
        return false;
    }

    (*env)->DeleteLocalRef(env, jclassName);

    g_helper_cache.helper_class = (jclass)(*env)->NewGlobalRef(env, helper);

    g_helper_cache.show_toast = (*env)->GetStaticMethodID(env, helper, "showToast", "(Landroid/app/Activity;Ljava/lang/String;Z)V");
    g_helper_cache.bt_get_paired = (*env)->GetStaticMethodID(env, helper, "btGetPairedDevices", "()[Ljava/lang/String;");
    g_helper_cache.bt_connect = (*env)->GetStaticMethodID(env, helper, "btConnect", "(Ljava/lang/String;)Z");
    g_helper_cache.bt_disconnect = (*env)->GetStaticMethodID(env, helper, "btDisconnect", "()V");
    g_helper_cache.bt_send = (*env)->GetStaticMethodID(env, helper, "btSend", "([B)I");
    g_helper_cache.bt_is_connected = (*env)->GetStaticMethodID(env, helper, "btIsConnected", "()Z");

    if (!g_helper_cache.show_toast)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_paired)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_connect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_disconnect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_send)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_is_connected)
        (*env)->ExceptionClear(env);

    (*env)->DeleteLocalRef(env, helper);
    (*env)->DeleteLocalRef(env, classLoaderClass);
    (*env)->DeleteLocalRef(env, classLoader);
    (*env)->DeleteLocalRef(env, activityClass);

    g_helper_cache.initialized = true;
    LOGI("AromaHelper initialized successfully");
    return true;
}

static void cache_physical_screen_info(struct android_app *app)
{
    if (g_phys_cached || !app || !app->activity)
        return;

    JNIEnv *env = NULL;
    int status = (*app->activity->vm)->GetEnv(app->activity->vm, (void **)&env, JNI_VERSION_1_6);
    bool did_attach = false;

    if (status < 0)
    {
        if ((*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    }

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

    (*env)->DeleteLocalRef(env, dm);
    (*env)->DeleteLocalRef(env, dm_class);
    (*env)->DeleteLocalRef(env, display_obj);
    (*env)->DeleteLocalRef(env, display_class);
    (*env)->DeleteLocalRef(env, wm);
    (*env)->DeleteLocalRef(env, wm_class);
    (*env)->DeleteLocalRef(env, activity_class);

    if (did_attach)
    {
        (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
    }
}

static void android_open_url(const char *url)
{
    if (!g_app || !g_app->activity || !url)
    {
        LOGE("Cannot open URL: Invalid state or URL");
        return;
    }

    JNIEnv *env = NULL;
    JavaVM *vm = g_app->activity->vm;
    int status = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    bool did_attach = false;

    if (status < 0)
    {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
        {
            LOGE("Failed to attach current thread for URL opening");
            return;
        }
        did_attach = true;
    }

    jclass activity_class = (*env)->GetObjectClass(env, g_app->activity->clazz);
    if (!activity_class)
    {
        LOGE("Failed to get activity class");
        if (did_attach)
        {
        }
        return;
    }

    jstring jurl = (*env)->NewStringUTF(env, url);

    jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
    jmethodID uri_parse = (*env)->GetStaticMethodID(env, uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
    jobject uri_obj = (*env)->CallStaticObjectMethod(env, uri_class, uri_parse, jurl);

    jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
    jfieldID action_view_field = (*env)->GetStaticFieldID(env, intent_class, "ACTION_VIEW", "Ljava/lang/String;");
    jobject action_view = (*env)->GetStaticObjectField(env, intent_class, action_view_field);

    jmethodID intent_ctor = (*env)->GetMethodID(env, intent_class, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
    jobject intent_obj = (*env)->NewObject(env, intent_class, intent_ctor, action_view, uri_obj);

    jmethodID start_activity = (*env)->GetMethodID(env, activity_class, "startActivity", "(Landroid/content/Intent;)V");
    (*env)->CallVoidMethod(env, g_app->activity->clazz, start_activity, intent_obj);

    (*env)->DeleteLocalRef(env, jurl);
    (*env)->DeleteLocalRef(env, uri_obj);
    (*env)->DeleteLocalRef(env, action_view);
    (*env)->DeleteLocalRef(env, intent_obj);
    (*env)->DeleteLocalRef(env, uri_class);
    (*env)->DeleteLocalRef(env, intent_class);
    (*env)->DeleteLocalRef(env, activity_class);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        LOGE("Exception occurred while launching browser intent");
    }

    if (did_attach)
    {
    }
}

static void android_send_intent(int action_enum, const char *uri, const char *type, const void *extras, int extra_count)
{
    if (!g_app || !g_app->activity)
    {
        LOGE("Cannot send intent: Invalid state");
        return;
    }

    const char *action = "android.intent.action.VIEW";
    switch (action_enum)
    {
    case 0:
        action = "android.intent.action.VIEW";
        break;
    case 1:
        action = "android.intent.action.SEND";
        break;
    case 2:
        action = "android.intent.action.EDIT";
        break;
    case 3:
        action = "android.intent.action.DIAL";
        break;
    case 4:
        action = "android.intent.action.CALL";
        break;
    default:
        break;
    }

    JNIEnv *env = NULL;
    JavaVM *vm = g_app->activity->vm;
    int status = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    bool did_attach = false;

    if (status < 0)
    {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
        {
            LOGE("Failed to attach current thread for intent");
            return;
        }
        did_attach = true;
    }

    jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
    jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
    jclass activity_class = (*env)->GetObjectClass(env, g_app->activity->clazz);

    if (!intent_class || !uri_class || !activity_class)
    {
        LOGE("Failed to find required classes for intent");
        if (did_attach)
        {
        }
        return;
    }

    jmethodID intent_ctor = (*env)->GetMethodID(env, intent_class, "<init>", "(Ljava/lang/String;)V");
    jstring jaction = (*env)->NewStringUTF(env, action);
    jobject intent_obj = (*env)->NewObject(env, intent_class, intent_ctor, jaction);

    jobject uri_obj = NULL;
    if (uri)
    {
        jstring juri = (*env)->NewStringUTF(env, uri);
        jmethodID uri_parse = (*env)->GetStaticMethodID(env, uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        uri_obj = (*env)->CallStaticObjectMethod(env, uri_class, uri_parse, juri);
        (*env)->DeleteLocalRef(env, juri);
    }

    if (uri_obj && type)
    {
        jstring jtype = (*env)->NewStringUTF(env, type);
        jmethodID set_data_and_type = (*env)->GetMethodID(env, intent_class, "setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;");
        (*env)->CallObjectMethod(env, intent_obj, set_data_and_type, uri_obj, jtype);
        (*env)->DeleteLocalRef(env, jtype);
    }
    else if (uri_obj)
    {
        jmethodID set_data = (*env)->GetMethodID(env, intent_class, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;");
        (*env)->CallObjectMethod(env, intent_obj, set_data, uri_obj);
    }
    else if (type)
    {
        jstring jtype = (*env)->NewStringUTF(env, type);
        jmethodID set_type = (*env)->GetMethodID(env, intent_class, "setType", "(Ljava/lang/String;)Landroid/content/Intent;");
        (*env)->CallObjectMethod(env, intent_obj, set_type, jtype);
        (*env)->DeleteLocalRef(env, jtype);
    }

    if (extras && extra_count > 0)
    {
        const AromaIntentExtra *extra_list = (const AromaIntentExtra *)extras;
        jmethodID put_extra = (*env)->GetMethodID(env, intent_class, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        if (put_extra)
        {
            for (int i = 0; i < extra_count; i++)
            {
                if (extra_list[i].key && extra_list[i].string_value)
                {
                    jstring k = (*env)->NewStringUTF(env, extra_list[i].key);
                    jstring v = (*env)->NewStringUTF(env, extra_list[i].string_value);
                    jobject res = (*env)->CallObjectMethod(env, intent_obj, put_extra, k, v);
                    if (res)
                        (*env)->DeleteLocalRef(env, res);
                    (*env)->DeleteLocalRef(env, k);
                    (*env)->DeleteLocalRef(env, v);
                }
            }
        }
    }

    jmethodID start_activity = (*env)->GetMethodID(env, activity_class, "startActivity", "(Landroid/content/Intent;)V");
    (*env)->CallVoidMethod(env, g_app->activity->clazz, start_activity, intent_obj);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        LOGE("Exception while sending intent");
    }

    if (uri_obj)
        (*env)->DeleteLocalRef(env, uri_obj);
    (*env)->DeleteLocalRef(env, intent_obj);
    (*env)->DeleteLocalRef(env, jaction);
    (*env)->DeleteLocalRef(env, activity_class);
    (*env)->DeleteLocalRef(env, uri_class);
    (*env)->DeleteLocalRef(env, intent_class);

    if (did_attach)
    {
    }
}

static void update_surface_size(void)
{
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE)
        return;

    EGLint w = 0, h = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    if (w <= 0 || h <= 0)
        return;

    if (w == g_width && h == g_height)
        return;

    if (g_width != 0 && g_height != 0)
    {
        LOGI("Window surface resized: %dx%d -> %dx%d", g_width, g_height, w, h);
    }

    g_width = w;
    g_height = h;

    glViewport(0, 0, g_width, g_height);

    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];

    for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
    {
        if (g_windows[i].is_active && g_windows[i].root_node)
        {
            struct AromaWindow *win_widget = (struct AromaWindow *)g_windows[i].root_node->node_widget_ptr;
            if (win_widget)
            {
                win_widget->rect.width = g_width;
                win_widget->rect.height = g_height;
            }

            aroma_node_update_layout(g_windows[i].root_node, 0, 0, g_width, g_height);
            aroma_node_invalidate(g_windows[i].root_node);

            AromaEvent *event = aroma_event_create(EVENT_TYPE_WINDOW_RESIZE, g_windows[i].root_node->node_id);
            if (event)
            {
                event->data.resize.width = g_width;
                event->data.resize.height = g_height;
                aroma_event_queue(event);
            }
        }
    }
}

static int32_t handle_input(struct android_app *app, AInputEvent *event)
{
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY)
    {
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN)
        {
            int32_t key_code = AKeyEvent_getKeyCode(event);
            char ch = 0;
            if (key_code >= AKEYCODE_0 && key_code <= AKEYCODE_9)
                ch = '0' + (key_code - AKEYCODE_0);
            else if (key_code >= AKEYCODE_A && key_code <= AKEYCODE_Z)
            {
                ch = 'a' + (key_code - AKEYCODE_A);
                int meta = AKeyEvent_getMetaState(event);
                if (meta & AMETA_SHIFT_ON)
                    ch = 'A' + (key_code - AKEYCODE_A);
            }
            else if (key_code == AKEYCODE_SPACE)
                ch = ' ';
            else if (key_code == AKEYCODE_DEL)
                ch = 8;
            else if (key_code == AKEYCODE_ENTER)
                ch = 10;
            else if (key_code == AKEYCODE_PERIOD)
                ch = '.';
            else if (key_code == AKEYCODE_COMMA)
                ch = ',';
            else if (key_code == AKEYCODE_MINUS)
                ch = '-';
            else if (key_code == AKEYCODE_AT)
                ch = '@';

            if (ch != 0)
            {
                AromaNode *focused = aroma_ui_get_focused_node();
                if (focused)
                {
                    AromaEvent *evt = aroma_event_create(EVENT_TYPE_KEY_PRESS, focused->node_id);
                    if (evt)
                    {
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
    int index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    float x = AMotionEvent_getX(event, index);
    float y = AMotionEvent_getY(event, index);

    switch (masked)
    {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        aroma_event_handle_touch(AMotionEvent_getPointerId(event, index), (int)x, (int)y, 1);
        aroma_event_handle_pointer_move((int)x, (int)y, true);
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
        aroma_event_handle_touch(AMotionEvent_getPointerId(event, index), (int)x, (int)y, 0);
        aroma_event_handle_pointer_move((int)x, (int)y, false);
        break;
    case AMOTION_EVENT_ACTION_MOVE:
    {
        size_t ptr_count = AMotionEvent_getPointerCount(event);
        for (size_t i = 0; i < ptr_count; i++)
        {
            aroma_event_handle_touch(
                AMotionEvent_getPointerId(event, i),
                (int)AMotionEvent_getX(event, i),
                (int)AMotionEvent_getY(event, i),
                2);
        }
        aroma_event_handle_pointer_move(
            (int)AMotionEvent_getX(event, 0),
            (int)AMotionEvent_getY(event, 0),
            true);
        break;
    }
    }
    return 1;
}

static int init_display(struct android_app *app)
{
    if (!app || !app->window)
        return -1;

    if (!g_window_flags_set)
    {
        ANativeActivity_setWindowFlags(app->activity,
                                       AWINDOW_FLAG_FORCE_NOT_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON,
                                       AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_LAYOUT_IN_SCREEN | AWINDOW_FLAG_LAYOUT_NO_LIMITS);
        g_window_flags_set = true;
    }

    cache_physical_screen_info(app);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY)
        return -1;

    if (!eglInitialize(dpy, NULL, NULL))
        return -1;

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE};

    EGLint num;
    if (!eglChooseConfig(dpy, attribs, &config, 1, &num) || num == 0)
    {
        LOGE("Failed to choose config with preserved swap behavior, trying fallback");
        const EGLint attribs_fallback[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0,
            EGL_STENCIL_SIZE, 0,
            EGL_NONE};
        if (!eglChooseConfig(dpy, attribs_fallback, &config, 1, &num) || num == 0)
        {
            LOGE("Failed to choose fallback config");
            return -1;
        }
    }

    EGLint format;
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, app->window, NULL);
    if (surf == EGL_NO_SURFACE)
    {
        LOGE("Failed to create window surface");
        return -1;
    }

    EGLContext ctx = context;
    if (ctx == EGL_NO_CONTEXT)
    {
        const EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE};
        ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
        if (ctx == EGL_NO_CONTEXT)
        {
            LOGE("Failed to create context");
            return -1;
        }
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx))
    {
        LOGE("Failed to make current");
        return -1;
    }

    eglSurfaceAttrib(dpy, surf, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
    glDisable(GL_SCISSOR_TEST);

    display = dpy;
    surface = surf;
    context = ctx;
    g_has_window = true;

    update_surface_size();

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx)
    {
        if (gfx->setup_shared_window_resources)
            gfx->setup_shared_window_resources();
        if (gfx->setup_separate_window_resources)
            gfx->setup_separate_window_resources(0);
    }

    return 0;
}

static void term_display_surface_only(void)
{
    if (display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE)
        {
            eglDestroySurface(display, surface);
        }
    }
    surface = EGL_NO_SURFACE;
    g_has_window = false;
    g_width = 0;
    g_height = 0;
}

static void term_display(void)
{
    term_display_surface_only();
    if (display != EGL_NO_DISPLAY)
    {
        if (context != EGL_NO_CONTEXT)
        {
            eglDestroyContext(display, context);
        }
        eglTerminate(display);
    }

    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    g_window_flags_set = false;
}

static void handle_cmd(struct android_app *app, int32_t cmd)
{
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        if (app->window)
        {
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
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_RESUME:
        if (app->window && !g_has_window)
        {
            term_display_surface_only();
            init_display(app);
        }
        else if (g_has_window)
        {
            update_surface_size();
            extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
            for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
            {
                if (g_windows[i].is_active && g_windows[i].root_node)
                {
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

int initialize(void)
{
    if (!g_app)
        return 0;

    cache_physical_screen_info(g_app);

    g_app->onAppCmd = handle_cmd;
    g_app->onInputEvent = handle_input;
    g_update_callback = NULL;
    g_update_callback_data = NULL;

    while (context == EGL_NO_CONTEXT && !g_app->destroyRequested)
    {
        int events;
        struct android_poll_source *source;
        if (ALooper_pollAll(-1, NULL, &events, (void **)&source) >= 0)
        {
            if (source)
            {
                source->process(g_app, source);
            }
        }
    }

    if (g_app->destroyRequested)
    {
        return 0;
    }

    return 1;
}

void shutdown(void)
{
    term_display();
}

size_t create_window(const char *title, int x, int y, int width, int height)
{
    return 0;
}

void make_context_current(size_t window_id)
{
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT)
    {
        if (eglGetCurrentContext() != context || eglGetCurrentSurface(EGL_DRAW) != surface)
        {
            if (!eglMakeCurrent(display, surface, surface, context))
            {
                EGLint error = eglGetError();
                LOGE("make_context_current failed: 0x%x", error);
            }
        }
    }
    else
    {
        LOGE("Attempted to make context current but display/surface/context is invalid");
    }
}

void set_window_update_callback(void (*callback)(size_t, void *), void *data)
{
    g_update_callback = callback;
    g_update_callback_data = data;
}

void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    if (!g_has_window)
    {
        *window_width = 0;
        *window_height = 0;
        return;
    }
    *window_width = g_width;
    *window_height = g_height;
}

void set_fullscreen(size_t window_id, bool enabled)
{
    if (!g_app || !g_app->activity)
        return;

    if (enabled)
    {
        ANativeActivity_setWindowFlags(g_app->activity, AWINDOW_FLAG_FULLSCREEN, 0);
    }
    else
    {
        ANativeActivity_setWindowFlags(g_app->activity, 0, AWINDOW_FLAG_FULLSCREEN);
    }
}

void request_window_update(size_t window_id) {}

bool run_event_loop(void)
{
    int events;
    struct android_poll_source *source;

    while (ALooper_pollAll(0, NULL, &events, (void **)&source) >= 0)
    {
        if (source)
            source->process(g_app, source);
        if (g_app->destroyRequested)
            return false;
    }

    if (g_has_window)
    {
        update_surface_size();
        if (g_update_callback)
        {
            g_update_callback(0, g_update_callback_data);
        }
        else
        {
        }
    }

    return true;
}

void swap_buffers(size_t window_id)
{
    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
        eglSwapBuffers(display, surface);
}

void *get_tft_context(void) { return NULL; }
void call_flush_function_ptr(void (*flush_fn)(struct AromaDrawList *, size_t, int, int, int, int), void *list) {}
void tft_mark_tiles_dirty(int y, int h) {}
void set_clear_color(uint16_t color) {}

static void android_show_keyboard(void)
{
    if (!g_app || !g_app->activity)
    {
        LOGE("Cannot show keyboard: g_app is NULL");
        return;
    }

    LOGI("Requesting soft keyboard via JNI");

    JNIEnv *env = NULL;
    int status = (*g_app->activity->vm)->GetEnv(g_app->activity->vm, (void **)&env, JNI_VERSION_1_6);
    bool did_attach = false;

    if (status < 0)
    {
        if ((*g_app->activity->vm)->AttachCurrentThread(g_app->activity->vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    }

    jclass activityClass = (*env)->GetObjectClass(env, g_app->activity->clazz);
    jmethodID getSystemService = (*env)->GetMethodID(env, activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, g_app->activity->clazz, getSystemService, serviceName);
    (*env)->DeleteLocalRef(env, serviceName);

    if (imm)
    {
        jmethodID getWindow = (*env)->GetMethodID(env, activityClass, "getWindow", "()Landroid/view/Window;");
        jobject window = (*env)->CallObjectMethod(env, g_app->activity->clazz, getWindow);
        jclass windowClass = (*env)->FindClass(env, "android/view/Window");
        jmethodID getDecorView = (*env)->GetMethodID(env, windowClass, "getDecorView", "()Landroid/view/View;");
        jobject decorView = (*env)->CallObjectMethod(env, window, getDecorView);

        if (decorView)
        {
            jclass immClass = (*env)->FindClass(env, "android/view/inputmethod/InputMethodManager");
            jmethodID showSoftInput = (*env)->GetMethodID(env, immClass, "showSoftInput", "(Landroid/view/View;I)Z");
            (*env)->CallBooleanMethod(env, imm, showSoftInput, decorView, 2);
            LOGI("Keyboard show requested via JNI");
            (*env)->DeleteLocalRef(env, decorView);
            (*env)->DeleteLocalRef(env, windowClass);
            (*env)->DeleteLocalRef(env, immClass);
        }

        (*env)->DeleteLocalRef(env, window);
        (*env)->DeleteLocalRef(env, imm);
    }
    else
    {
        LOGE("Failed to get InputMethodManager");
    }

    (*env)->DeleteLocalRef(env, activityClass);

    if (did_attach)
    {
        (*g_app->activity->vm)->DetachCurrentThread(g_app->activity->vm);
    }
}

static void android_hide_keyboard(void)
{
    if (!g_app || !g_app->activity)
        return;

    JNIEnv *env = NULL;
    int status = (*g_app->activity->vm)->GetEnv(g_app->activity->vm, (void **)&env, JNI_VERSION_1_6);
    bool did_attach = false;

    if (status < 0)
    {
        if ((*g_app->activity->vm)->AttachCurrentThread(g_app->activity->vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    }

    jclass activityClass = (*env)->GetObjectClass(env, g_app->activity->clazz);
    jmethodID getSystemService = (*env)->GetMethodID(env, activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, g_app->activity->clazz, getSystemService, serviceName);
    (*env)->DeleteLocalRef(env, serviceName);

    if (imm)
    {
        jmethodID getWindow = (*env)->GetMethodID(env, activityClass, "getWindow", "()Landroid/view/Window;");
        jobject window = (*env)->CallObjectMethod(env, g_app->activity->clazz, getWindow);
        jclass windowClass = (*env)->FindClass(env, "android/view/Window");
        jmethodID getDecorView = (*env)->GetMethodID(env, windowClass, "getDecorView", "()Landroid/view/View;");
        jobject decorView = (*env)->CallObjectMethod(env, window, getDecorView);

        if (decorView)
        {
            jclass viewClass = (*env)->FindClass(env, "android/view/View");
            jmethodID getWindowToken = (*env)->GetMethodID(env, viewClass, "getWindowToken", "()Landroid/os/IBinder;");
            jobject token = (*env)->CallObjectMethod(env, decorView, getWindowToken);

            if (token)
            {
                jclass immClass = (*env)->FindClass(env, "android/view/inputmethod/InputMethodManager");
                jmethodID hideSoftInput = (*env)->GetMethodID(env, immClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
                (*env)->CallBooleanMethod(env, imm, hideSoftInput, token, 0);
                LOGI("Keyboard hide requested via JNI");
                (*env)->DeleteLocalRef(env, token);
                (*env)->DeleteLocalRef(env, immClass);
            }

            (*env)->DeleteLocalRef(env, viewClass);
            (*env)->DeleteLocalRef(env, decorView);
        }

        (*env)->DeleteLocalRef(env, windowClass);
        (*env)->DeleteLocalRef(env, window);
        (*env)->DeleteLocalRef(env, imm);
    }

    (*env)->DeleteLocalRef(env, activityClass);

    if (did_attach)
    {
        (*g_app->activity->vm)->DetachCurrentThread(g_app->activity->vm);
    }
}

static const char *impl_android_get_internal_path(void)
{
    if (!g_app || !g_app->activity)
        return NULL;
    return g_app->activity->internalDataPath;
}

static const char *impl_android_get_external_path(void)
{
    if (!g_app || !g_app->activity)
        return NULL;
    return g_app->activity->externalDataPath;
}

void platform_set_android_app(void *state)
{
    g_app = (struct android_app *)state;
}

JNIEnv *aroma_android_get_env()
{
    if (!g_app || !g_app->activity || !g_app->activity->vm)
        return NULL;
    JNIEnv *env = NULL;
    jint res = (*g_app->activity->vm)->AttachCurrentThread(g_app->activity->vm, &env, NULL);
    return (res == JNI_OK) ? env : NULL;
}

jobject aroma_android_get_activity()
{
    if (!g_app || !g_app->activity)
        return NULL;
    return g_app->activity->clazz;
}

JavaVM *aroma_android_get_jvm()
{
    if (!g_app || !g_app->activity)
        return NULL;
    return g_app->activity->vm;
}

static jstring StrToJstring(JNIEnv *env, const char *str)
{
    return (*env)->NewStringUTF(env, str);
}

static jclass GetContextClass(JNIEnv *env)
{
    jclass cls = (*env)->FindClass(env, "android/content/Context");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return cls;
}

static jclass FindClassSafe(JNIEnv *env, const char *name)
{
    jclass cls = (*env)->FindClass(env, name);
    if (!cls || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        jobject activity = aroma_android_get_activity();
        if (activity)
        {
            jclass activityClass = (*env)->GetObjectClass(env, activity);
            jmethodID getClassLoader = (*env)->GetMethodID(env, activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            if (getClassLoader)
            {
                jobject classLoader = (*env)->CallObjectMethod(env, activity, getClassLoader);
                if (classLoader)
                {
                    jclass classLoaderClass = (*env)->GetObjectClass(env, classLoader);
                    jmethodID loadClass = (*env)->GetMethodID(env, classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                    if (loadClass)
                    {
                        size_t len = strlen(name);
                        char *dotted_name = (char *)malloc(len + 1);
                        if (dotted_name)
                        {
                            strcpy(dotted_name, name);
                            for (size_t i = 0; i < len; i++)
                            {
                                if (dotted_name[i] == '/')
                                    dotted_name[i] = '.';
                            }
                            jstring jname = (*env)->NewStringUTF(env, dotted_name);
                            cls = (jclass)(*env)->CallObjectMethod(env, classLoader, loadClass, jname);
                            if ((*env)->ExceptionCheck(env))
                            {
                                (*env)->ExceptionClear(env);
                                cls = NULL;
                            }
                            (*env)->DeleteLocalRef(env, jname);
                            free(dotted_name);
                        }
                    }
                    (*env)->DeleteLocalRef(env, classLoaderClass);
                    (*env)->DeleteLocalRef(env, classLoader);
                }
            }
            (*env)->DeleteLocalRef(env, activityClass);
        }
    }
    return cls;
}

static bool impl_android_check_permission(const char *permission_name)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return false;

    jobject activity = aroma_android_get_activity();
    jclass contextClass = GetContextClass(env);
    if (!contextClass)
        return false;

    jmethodID checkSelfPermission = (*env)->GetMethodID(env, contextClass, "checkSelfPermission", "(Ljava/lang/String;)I");

    bool granted = false;
    if (checkSelfPermission)
    {
        jstring perm = StrToJstring(env, permission_name);
        jint res = (*env)->CallIntMethod(env, activity, checkSelfPermission, perm);
        granted = (res == 0);
        (*env)->DeleteLocalRef(env, perm);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, contextClass);
    return granted;
}
static void impl_android_request_permission(const char **permissions, int permCount)
{
    if (permCount <= 0)
        return;

    __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "Requesting %d permissions in SINGLE request:", permCount);
    for (int i = 0; i < permCount; i++)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "  [%d] %s", i, permissions[i]);
    }

    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    jobject activity = aroma_android_get_activity();
    if (!activity)
        return;

    jclass activityClass = (*env)->GetObjectClass(env, activity);
    if (!activityClass)
        return;

    jmethodID checkSelfPermission = (*env)->GetMethodID(env, activityClass, "checkSelfPermission", "(Ljava/lang/String;)I");
    jmethodID requestPermissions = (*env)->GetMethodID(env, activityClass, "requestPermissions", "([Ljava/lang/String;I)V");
    if (!checkSelfPermission || !requestPermissions)
    {
        __android_log_print(ANDROID_LOG_ERROR, "PermissionRequest", "Failed to get permission methods");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        return;
    }

    jclass versionClass = (*env)->FindClass(env, "android/os/Build$VERSION");
    jfieldID sdkIntField = (*env)->GetStaticFieldID(env, versionClass, "SDK_INT", "I");
    jint sdkInt = (*env)->GetStaticIntField(env, versionClass, sdkIntField);
    (*env)->DeleteLocalRef(env, versionClass);
    __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "Android API level: %d", sdkInt);

    int toRequestCount = 0;
    for (int i = 0; i < permCount; i++)
    {
        const char *perm = permissions[i];

        jstring permStr = (*env)->NewStringUTF(env, perm);
        jint granted = (*env)->CallIntMethod(env, activity, checkSelfPermission, permStr);
        (*env)->DeleteLocalRef(env, permStr);

        if (granted != 0)
        {
            toRequestCount++;
            __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "Need to request: %s", perm);
        }
        else
        {
            __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "Already granted: %s", perm);
        }
    }

    if (toRequestCount == 0)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "All permissions already granted");
        (*env)->DeleteLocalRef(env, activityClass);
        return;
    }

    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    if (!stringClass)
    {
        __android_log_print(ANDROID_LOG_ERROR, "PermissionRequest", "Failed to find String class");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        return;
    }

    jobjectArray permArray = (*env)->NewObjectArray(env, toRequestCount, stringClass, NULL);
    if (!permArray)
    {
        __android_log_print(ANDROID_LOG_ERROR, "PermissionRequest", "Failed to create permission array");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, stringClass);
        (*env)->DeleteLocalRef(env, activityClass);
        return;
    }

    int idx = 0;
    for (int i = 0; i < permCount; i++)
    {
        const char *perm = permissions[i];

        jstring permStr = (*env)->NewStringUTF(env, perm);
        jint granted = (*env)->CallIntMethod(env, activity, checkSelfPermission, permStr);
        (*env)->DeleteLocalRef(env, permStr);

        if (granted != 0)
        {
            jstring arrayPermStr = (*env)->NewStringUTF(env, perm);
            (*env)->SetObjectArrayElement(env, permArray, idx++, arrayPermStr);
            (*env)->DeleteLocalRef(env, arrayPermStr);
        }
    }

    int requestCode = 1000;
    __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "Requesting %d permissions in SINGLE request with code %d", toRequestCount, requestCode);
    (*env)->CallVoidMethod(env, activity, requestPermissions, permArray, requestCode);

    if ((*env)->ExceptionCheck(env))
    {
        __android_log_print(ANDROID_LOG_ERROR, "PermissionRequest", "Exception during permission request");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, permArray);
    (*env)->DeleteLocalRef(env, stringClass);
    (*env)->DeleteLocalRef(env, activityClass);

    __android_log_print(ANDROID_LOG_DEBUG, "PermissionRequest", "Permission request completed");
}
static void *impl_android_get_system_service(const char *service_name)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return NULL;

    jobject activity = aroma_android_get_activity();
    if (!activity)
        return NULL;

    jclass activityClass = (*env)->GetObjectClass(env, activity);
    jmethodID getSystemService = (*env)->GetMethodID(env, activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");

    jobject service = NULL;
    if (getSystemService)
    {
        jstring name = StrToJstring(env, service_name);
        service = (*env)->CallObjectMethod(env, activity, getSystemService, name);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            if (service)
            {
                (*env)->DeleteLocalRef(env, service);
                service = NULL;
            }
        }
        (*env)->DeleteLocalRef(env, name);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, activityClass);
    return service;
}

static void impl_android_vibrate(int ms)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    jobject vibrator = (jobject)impl_android_get_system_service("vibrator");
    if (!vibrator)
        return;

    jclass vibratorClass = (*env)->GetObjectClass(env, vibrator);
    jmethodID vibrate = (*env)->GetMethodID(env, vibratorClass, "vibrate", "(J)V");

    if (!vibrate)
    {
        (*env)->ExceptionClear(env);
    }

    bool done = false;
    if (vibrate)
    {
        (*env)->CallVoidMethod(env, vibrator, vibrate, (jlong)ms);
        if (!(*env)->ExceptionCheck(env))
        {
            done = true;
        }
        else
        {
            (*env)->ExceptionClear(env);
        }
    }

    if (!done)
    {
        jclass effectClass = (*env)->FindClass(env, "android/os/VibrationEffect");
        if (effectClass)
        {
            jmethodID createOneShot = (*env)->GetStaticMethodID(env, effectClass, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
            if (createOneShot)
            {
                jobject effect = (*env)->CallStaticObjectMethod(env, effectClass, createOneShot, (jlong)ms, -1);
                if ((*env)->ExceptionCheck(env))
                {
                    (*env)->ExceptionClear(env);
                    if (effect)
                        (*env)->DeleteLocalRef(env, effect);
                    effect = NULL;
                }

                if (effect)
                {
                    jmethodID vibrateEffect = (*env)->GetMethodID(env, vibratorClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
                    if (vibrateEffect)
                    {
                        (*env)->CallVoidMethod(env, vibrator, vibrateEffect, effect);
                        if ((*env)->ExceptionCheck(env))
                        {
                            (*env)->ExceptionClear(env);
                        }
                    }
                    else
                    {
                        (*env)->ExceptionClear(env);
                    }
                    (*env)->DeleteLocalRef(env, effect);
                }
            }
            else
            {
                (*env)->ExceptionClear(env);
            }
            (*env)->DeleteLocalRef(env, effectClass);
        }
        else
        {
            (*env)->ExceptionClear(env);
        }
    }

    (*env)->DeleteLocalRef(env, vibrator);
    (*env)->DeleteLocalRef(env, vibratorClass);
}

static void impl_android_toast(const char *msg, bool long_duration)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env))
    {
        LOGE("AromaHelper not available for toast");
        return;
    }

    jobject activity = aroma_android_get_activity();
    if (!activity)
        return;

    if (g_helper_cache.show_toast)
    {
        jstring jmsg = (*env)->NewStringUTF(env, msg);
        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.show_toast, activity, jmsg, (jboolean)long_duration);
        (*env)->DeleteLocalRef(env, jmsg);
    }
    else
    {
        LOGE("showToast method not available");
    }
}

static void impl_android_launch_camera(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    jclass intentClass = (*env)->FindClass(env, "android/content/Intent");
    if (!intentClass)
    {
        (*env)->ExceptionClear(env);
        return;
    }

    jstring actionImageCapture = (*env)->NewStringUTF(env, "android.media.action.IMAGE_CAPTURE");
    jmethodID intentCtor = (*env)->GetMethodID(env, intentClass, "<init>", "(Ljava/lang/String;)V");
    jobject intent = (*env)->NewObject(env, intentClass, intentCtor, actionImageCapture);

    jobject activity = aroma_android_get_activity();
    if (activity && intent)
    {
        jclass activityClass = (*env)->GetObjectClass(env, activity);
        jmethodID startActivity = (*env)->GetMethodID(env, activityClass, "startActivity", "(Landroid/content/Intent;)V");
        if (startActivity)
        {
            (*env)->CallVoidMethod(env, activity, startActivity, intent);
        }
        else
        {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, activityClass);
    }

    if (intent)
        (*env)->DeleteLocalRef(env, intent);
    (*env)->DeleteLocalRef(env, actionImageCapture);
    (*env)->DeleteLocalRef(env, intentClass);
}

static bool impl_android_is_wifi_enabled(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return false;
    jobject wifiMgr = (jobject)impl_android_get_system_service("wifi");
    if (!wifiMgr)
        return false;

    jclass wifiClass = (*env)->GetObjectClass(env, wifiMgr);
    jmethodID isWifiEnabled = (*env)->GetMethodID(env, wifiClass, "isWifiEnabled", "()Z");
    bool res = false;
    if (isWifiEnabled)
    {
        res = (*env)->CallBooleanMethod(env, wifiMgr, isWifiEnabled);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, wifiMgr);
    (*env)->DeleteLocalRef(env, wifiClass);
    return res;
}

static bool impl_android_is_bluetooth_enabled(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return false;

    jclass adapterClass = (*env)->FindClass(env, "android/bluetooth/BluetoothAdapter");
    if (!adapterClass)
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    jmethodID getDefaultAdapter = (*env)->GetStaticMethodID(env, adapterClass, "getDefaultAdapter", "()Landroid/bluetooth/BluetoothAdapter;");
    if (!getDefaultAdapter)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, adapterClass);
        return false;
    }

    jobject adapter = (*env)->CallStaticObjectMethod(env, adapterClass, getDefaultAdapter);

    if (!adapter)
    {
        (*env)->DeleteLocalRef(env, adapterClass);
        return false;
    }

    jmethodID isEnabled = (*env)->GetMethodID(env, adapterClass, "isEnabled", "()Z");
    bool res = false;
    if (isEnabled)
    {
        res = (*env)->CallBooleanMethod(env, adapter, isEnabled);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, adapter);
    (*env)->DeleteLocalRef(env, adapterClass);
    return res;
}

static int impl_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return 0;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_paired)
    {
        LOGE("btGetPairedDevices not available");
        return 0;
    }

    jobjectArray arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_paired);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return 0;
    }

    if (!arr)
        return 0;

    jsize len = (*env)->GetArrayLength(env, arr);
    int written = 0;
    for (jsize i = 0; i < len && written < max_devices; i++)
    {
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
        const char *str = (*env)->GetStringUTFChars(env, s, NULL);

        char addr[18] = {0};
        char name[248] = {0};
        const char *sep = strchr(str, ';');
        if (sep)
        {
            int a_len = sep - str;
            if (a_len >= 17)
                a_len = 17;
            strncpy(addr, str, a_len);
            addr[a_len] = '\0';
            strncpy(name, sep + 1, 247);
            name[247] = '\0';
        }
        else
        {
            strncpy(addr, str, 17);
            addr[17] = '\0';
            strncpy(name, "", 1);
        }

        strncpy(out_addrs[written], addr, 18);
        strncpy(out_names[written], name, 248);
        written++;

        (*env)->ReleaseStringUTFChars(env, s, str);
        (*env)->DeleteLocalRef(env, s);
    }

    (*env)->DeleteLocalRef(env, arr);
    return written;
}

static bool impl_android_bt_connect(const char *addr)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env || !addr)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_connect)
    {
        LOGE("btConnect not available");
        return false;
    }

    jstring jaddr = (*env)->NewStringUTF(env, addr);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_connect, jaddr);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }

    (*env)->DeleteLocalRef(env, jaddr);
    return res == JNI_TRUE;
}

static void impl_android_bt_disconnect(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_disconnect)
    {
        LOGE("btDisconnect not available");
        return;
    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_disconnect);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
}

static int impl_android_bt_send(const char *data, int len)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env || !data || len <= 0)
        return -1;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_send)
    {
        LOGE("btSend not available");
        return -1;
    }

    jbyteArray arr = (*env)->NewByteArray(env, len);
    (*env)->SetByteArrayRegion(env, arr, 0, len, (const jbyte *)data);

    jint written = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_send, arr);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        written = -1;
    }

    (*env)->DeleteLocalRef(env, arr);
    return (int)written;
}

static bool impl_android_bt_is_connected(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_is_connected)
    {
        LOGE("btIsConnected not available");
        return false;
    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_is_connected);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }

    return res == JNI_TRUE;
}

static int impl_android_get_battery_level(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return -1;

    jobject batteryManager = (jobject)impl_android_get_system_service("batterymanager");
    if (!batteryManager)
        return -1;

    jclass bmClass = (*env)->GetObjectClass(env, batteryManager);
    jmethodID getIntProperty = (*env)->GetMethodID(env, bmClass, "getIntProperty", "(I)I");
    int capacity = -1;
    if (getIntProperty)
    {
        capacity = (*env)->CallIntMethod(env, batteryManager, getIntProperty, 4);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, batteryManager);
    (*env)->DeleteLocalRef(env, bmClass);
    return capacity;
}

const char *aroma_android_get_id() { return "Android"; }

static void impl_android_set_wifi_enabled(bool enabled)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;
    jobject wifiMgr = (jobject)impl_android_get_system_service("wifi");
    if (!wifiMgr)
        return;

    jclass wifiClass = (*env)->GetObjectClass(env, wifiMgr);
    jmethodID setWifiEnabled = (*env)->GetMethodID(env, wifiClass, "setWifiEnabled", "(Z)Z");
    if (setWifiEnabled)
    {
        (*env)->CallBooleanMethod(env, wifiMgr, setWifiEnabled, (jboolean)enabled);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
        }
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, wifiMgr);
    (*env)->DeleteLocalRef(env, wifiClass);
}

static void impl_android_open_settings(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    jclass intentClass = (*env)->FindClass(env, "android/content/Intent");
    if (!intentClass)
    {
        (*env)->ExceptionClear(env);
        return;
    }

    jstring actionSettings = (*env)->NewStringUTF(env, "android.settings.SETTINGS");
    jmethodID intentCtor = (*env)->GetMethodID(env, intentClass, "<init>", "(Ljava/lang/String;)V");
    jobject intent = (*env)->NewObject(env, intentClass, intentCtor, actionSettings);

    jobject activity = aroma_android_get_activity();
    if (activity && intent)
    {
        jclass activityClass = (*env)->GetObjectClass(env, activity);
        jmethodID startActivity = (*env)->GetMethodID(env, activityClass, "startActivity", "(Landroid/content/Intent;)V");
        if (startActivity)
        {
            (*env)->CallVoidMethod(env, activity, startActivity, intent);
        }
        else
        {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, activityClass);
    }

    if (intent)
        (*env)->DeleteLocalRef(env, intent);
    (*env)->DeleteLocalRef(env, actionSettings);
    (*env)->DeleteLocalRef(env, intentClass);
}

static void impl_android_launch_gallery(void)
{
    JNIEnv *env = aroma_android_get_env();
    if (!env)
        return;

    jclass intentClass = (*env)->FindClass(env, "android/content/Intent");
    if (!intentClass)
    {
        (*env)->ExceptionClear(env);
        return;
    }

    jstring actionMain = (*env)->NewStringUTF(env, "android.intent.action.MAIN");
    jstring categoryAppGallery = (*env)->NewStringUTF(env, "android.intent.category.APP_GALLERY");

    jmethodID intentCtor = (*env)->GetMethodID(env, intentClass, "<init>", "(Ljava/lang/String;)V");
    jobject intent = (*env)->NewObject(env, intentClass, intentCtor, actionMain);

    if (intent)
    {
        jmethodID addCategory = (*env)->GetMethodID(env, intentClass, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;");
        if (addCategory)
        {
            (*env)->CallObjectMethod(env, intent, addCategory, categoryAppGallery);
        }
        else
        {
            (*env)->ExceptionClear(env);
        }

        jobject activity = aroma_android_get_activity();
        if (activity)
        {
            jclass activityClass = (*env)->GetObjectClass(env, activity);
            jmethodID startActivity = (*env)->GetMethodID(env, activityClass, "startActivity", "(Landroid/content/Intent;)V");
            if (startActivity)
            {
                (*env)->CallVoidMethod(env, activity, startActivity, intent);
            }
            else
            {
                (*env)->ExceptionClear(env);
            }
            (*env)->DeleteLocalRef(env, activityClass);
        }
        (*env)->DeleteLocalRef(env, intent);
    }

    (*env)->DeleteLocalRef(env, actionMain);
    (*env)->DeleteLocalRef(env, categoryAppGallery);
    (*env)->DeleteLocalRef(env, intentClass);
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
    .android_send_intent = android_send_intent,
    .android_check_permission = impl_android_check_permission,
    .android_request_permission = impl_android_request_permission,
    .android_toast = impl_android_toast,
    .android_open_settings = impl_android_open_settings,
    .android_vibrate = impl_android_vibrate,
    .android_get_battery_level = impl_android_get_battery_level,
    .android_is_wifi_enabled = impl_android_is_wifi_enabled,
    .android_set_wifi_enabled = impl_android_set_wifi_enabled,
    .android_is_bluetooth_enabled = impl_android_is_bluetooth_enabled,
    .android_bt_get_paired = impl_android_bt_get_paired,
    .android_bt_connect = impl_android_bt_connect,
    .android_bt_disconnect = impl_android_bt_disconnect,
    .android_bt_send = impl_android_bt_send,
    .android_bt_is_connected = impl_android_bt_is_connected,
    .android_launch_camera = impl_android_launch_camera,
    .android_launch_gallery = impl_android_launch_gallery,
    .android_get_system_service = impl_android_get_system_service,
    .android_get_internal_path = impl_android_get_internal_path,
    .android_get_external_path = impl_android_get_external_path};

#endif