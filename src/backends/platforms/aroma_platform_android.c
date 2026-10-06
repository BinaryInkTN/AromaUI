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
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include <math.h>
#include "../aroma_abi.h"
#include "../graphics/aroma_graphics_interface.h"

#ifdef AROMA_HAS_VULKAN
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#endif
#include "core/aroma_logger.h"
#include "core/aroma_event.h"
#include "core/aroma_timer.h"
#include "core/aroma_time.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"
#include "widgets/aroma_window.h"
#include "widgets/aroma_textbox.h"
#define AROMA_MAX_TOUCHES 10

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
static bool g_using_vulkan = false;

static bool is_vulkan_backend(void)
{
#ifdef AROMA_HAS_VULKAN

    if (!aroma_backend_abi.get_graphics_backend_type)
        return true;
    AromaGraphicsBackendType type = aroma_backend_abi.get_graphics_backend_type();

    return (type == GRAPHICS_BACKEND_VULKAN || type == GRAPHICS_BACKEND_TFT_ESPI);
#else
    return false;
#endif
}
static int g_phys_width = 0;
static int g_phys_height = 0;
static bool g_phys_cached = false;
static bool g_window_flags_set = false;
static void (*g_update_callback)(size_t window_id, void *data) = NULL;
static void *g_update_callback_data = NULL;

#include <android/choreographer.h>
static void update_surface_size(void);
static AChoreographer *g_choreographer = NULL;
static bool g_frame_requested = false;
static bool g_frame_needed = false;
static int g_avail_width = 0;
static int g_avail_height = 0;

static void get_layout_viewport(int *width, int *height)
{
    *width = g_width;
    *height = g_height;
}

static void choreographer_callback(long frameTimeNanos, void *data)
{
    (void)data;
    (void)frameTimeNanos;
    g_frame_requested = false;

    if (!g_has_window || !g_update_callback)
        return;

    update_surface_size();
    aroma_timer_tick(aroma_time_now_ms());

    extern AromaWindowHandle g_windows[];
    extern int g_window_count;
    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_windows[i].is_active && g_windows[i].window)
            g_update_callback(g_windows[i].window_id, g_update_callback_data);
    }

    if (g_frame_needed)
    {
        g_frame_needed = false;
        if (g_choreographer && !g_frame_requested)
        {
            AChoreographer_postFrameCallback(g_choreographer,
                                             choreographer_callback, NULL);
            g_frame_requested = true;
        }
    }
}

static void request_frame(void)
{
    if (g_choreographer && !g_frame_requested && g_has_window)
    {
        AChoreographer_postFrameCallback(g_choreographer,
                                         choreographer_callback, NULL);
        g_frame_requested = true;
    }
}
static float g_density = 1.0f;
static int g_density_dpi = 160;
static float g_scaled_density = 1.0f;
static float g_xdpi = 160.0f;
static float g_ydpi = 160.0f;

typedef struct
{
    jclass helper_class;
    jclass native_callback_class;
    jmethodID init;
    jmethodID add_callback;
    jmethodID remove_callback;
    jmethodID show_toast;
    jmethodID bt_scan;
    jmethodID bt_stop_scan;
    jmethodID bt_get_paired;
    jmethodID bt_pair;
    jmethodID bt_unpair;
    jmethodID bt_get_pair_state;
    jmethodID bt_connect;
    jmethodID bt_connect_with_mode;
    jmethodID bt_disconnect;
    jmethodID bt_send;
    jmethodID bt_is_connected;
    jmethodID bt_get_device_type;
    jmethodID bt_get_device_name;
    jmethodID bt_get_current_mode;
    jmethodID bt_get_mode_name;
    bool initialized;
    jobject callback_obj;
    jmethodID get_preference;
    jmethodID set_preference;
    jmethodID get_preference_bool;
    jmethodID set_preference_bool;
    jmethodID get_preference_int;
    jmethodID set_preference_int;
    jmethodID get_preference_float;
    jmethodID set_preference_float;
    jmethodID get_preference_long;
    jmethodID set_preference_long;
    jmethodID notify_channel;
    jmethodID notify_show;
    jmethodID notify_cancel;
    jmethodID notify_cancel_all;
    jmethodID notify_enabled;
    jmethodID vibrate_effect;
    jmethodID clip_set;
    jmethodID clip_get;
    jmethodID share_text;
    jmethodID ui_immersive;
    jmethodID ui_keep_screen_on;
    jmethodID orient_lock;
    jmethodID orient_set;
    jmethodID orient_unlock;
    jmethodID orient_locked;
    jmethodID pick_image;
    jmethodID capture_photo;
    jmethodID open_document;
    jmethodID sensor_start;
    jmethodID sensor_stop;
    jmethodID sensor_available;
    jmethodID add_sensor_callback;
    jmethodID remove_sensor_callback;
    jmethodID tts_speak;
    jmethodID tts_stop;
    jmethodID tts_is_speaking;
    jmethodID btle_start_scan;
    jmethodID btle_stop_scan;
    jmethodID btle_connect;
    jmethodID btle_disconnect;
    jmethodID btle_is_connected;
    jmethodID btle_discover;
    jmethodID btle_read;
    jmethodID btle_write;
    jmethodID btle_notify;
    jmethodID dev_manufacturer;
    jmethodID dev_model;
    jmethodID dev_os_version;
    jmethodID dev_sdk_int;
    jmethodID app_package;
    jmethodID app_version_name;
    jmethodID app_version_code;
    jmethodID app_install_time;
    jmethodID mem_avail_mb;
    jmethodID mem_low;
    jmethodID net_connected;
    jmethodID net_type;
    jmethodID batt_charging;
    jmethodID dev_interactive;
    jmethodID locale_tag;
    jmethodID timezone_id;
    jmethodID net_operator;
    jmethodID wake_acquire;
    jmethodID wake_release;
    jmethodID torch_set;
    jmethodID nfc_start;
    jmethodID nfc_stop;
    jmethodID nfc_available;
    jmethodID nfc_enabled;
    jmethodID bio_available;
    jmethodID bio_auth;
    jmethodID loc_start;
    jmethodID loc_stop;
    jmethodID loc_available;
    jmethodID rec_start;
    jmethodID rec_stop;
    jmethodID sc_add;
    jmethodID sc_remove;
    jmethodID sc_count;
    jmethodID pip_enter;
    jmethodID pip_available;
    jmethodID wp_set_image;
    jmethodID vol_music_get;
    jmethodID vol_music_max;
    jmethodID vol_music_set;
    jmethodID ringer_get;
    jmethodID storage_free_mb;
    jmethodID storage_total_mb;
    jmethodID sysbar_status_px;
    jmethodID sysbar_nav_px;
    jmethodID kb_visible;
    jmethodID app_installed;
    jmethodID app_open;
    jmethodID contacts_pick;
    jmethodID shot_capture;
    jmethodID http_download;
    jmethodID open_url;
    jmethodID show_ime;
    jmethodID hide_ime;
    jmethodID ir_available;
    jmethodID ir_transmit;
    jclass activity_class;

} AromaHelperCache;

static AromaHelperCache g_helper_cache = {0};

typedef struct
{
    void (*device_discovered_cb)(const char *addr, const char *name, int type, int rssi);
    void (*scan_finished_cb)(void);
    void (*connection_result_cb)(bool success, const char *name, int type, int mode);
    void (*data_received_cb)(const char *data, int len);
    void (*pairing_result_cb)(bool success, const char *addr, const char *name);
} AromaBluetoothCallbacks;

static AromaBluetoothCallbacks g_bt_callbacks = {0};

static JNIEnv *get_jni_env(int *attach)
{
    if (!g_app || !g_app->activity || !g_app->activity->vm)
        return NULL;
    JNIEnv *env = NULL;
    int status = (*g_app->activity->vm)->GetEnv(g_app->activity->vm, (void **)&env, JNI_VERSION_1_6);
    *attach = 0;
    if (status < 0)
    {
        if ((*g_app->activity->vm)->AttachCurrentThread(g_app->activity->vm, &env, NULL) == JNI_OK)
        {
            *attach = 1;
        }
        else
        {
            return NULL;
        }
    }
    return env;
}

static void detach_jni_env(int attach)
{
    if (attach && g_app && g_app->activity && g_app->activity->vm)
    {
        (*g_app->activity->vm)->DetachCurrentThread(g_app->activity->vm);
    }
}

static jstring str_to_jstring(JNIEnv *env, const char *str)
{
    return (*env)->NewStringUTF(env, str);
}

static jclass find_class_safe(JNIEnv *env, const char *name)
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

static void JNICALL native_on_device_discovered(JNIEnv *env, jobject thiz,
                                                jstring address, jstring name,
                                                jint type, jint rssi)
{
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    const char *name_str = name ? (*env)->GetStringUTFChars(env, name, NULL) : "";

    __android_log_print(ANDROID_LOG_DEBUG, "AromaHelper-Native", "Device discovered: %s - %s", addr_str, name_str);

    if (g_bt_callbacks.device_discovered_cb)
    {
        g_bt_callbacks.device_discovered_cb(addr_str, name_str, (int)type, (int)rssi);
    }

    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
    if (name)
        (*env)->ReleaseStringUTFChars(env, name, name_str);
}

static void JNICALL native_on_scan_finished(JNIEnv *env, jobject thiz)
{
    __android_log_print(ANDROID_LOG_DEBUG, "AromaHelper-Native", "Scan finished");
    if (g_bt_callbacks.scan_finished_cb)
    {
        g_bt_callbacks.scan_finished_cb();
    }
}

static void JNICALL native_on_pairing_result(JNIEnv *env, jobject thiz,
                                             jboolean success,
                                             jstring address, jstring name)
{
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    const char *name_str = name ? (*env)->GetStringUTFChars(env, name, NULL) : "";

    __android_log_print(ANDROID_LOG_DEBUG, "AromaHelper-Native", "Pairing result: %d %s", success, addr_str);

    if (g_bt_callbacks.pairing_result_cb)
    {
        g_bt_callbacks.pairing_result_cb(success == JNI_TRUE, addr_str, name_str);
    }

    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
    if (name)
        (*env)->ReleaseStringUTFChars(env, name, name_str);
}

static void JNICALL native_on_connection_result(JNIEnv *env, jobject thiz,
                                                jboolean success, jstring deviceName,
                                                jint deviceType, jint mode)
{
    const char *name_str = deviceName ? (*env)->GetStringUTFChars(env, deviceName, NULL) : "";

    __android_log_print(ANDROID_LOG_DEBUG, "AromaHelper-Native", "Connection result: %d %s", success, name_str);

    if (g_bt_callbacks.connection_result_cb)
    {
        g_bt_callbacks.connection_result_cb(success == JNI_TRUE, name_str, (int)deviceType, (int)mode);
    }

    if (deviceName)
        (*env)->ReleaseStringUTFChars(env, deviceName, name_str);
}

static void JNICALL native_on_data_received(JNIEnv *env, jobject thiz,
                                            jbyteArray data, jint length)
{
    if (g_bt_callbacks.data_received_cb && data)
    {
        jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
        g_bt_callbacks.data_received_cb((const char *)bytes, (int)length);
        (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    }
}

static void JNICALL native_on_connection_state_changed(JNIEnv *env, jobject thiz, jint state)
{
}

typedef struct
{
    bool active;
    int request_code;
    char permissions[16][128];
    int perm_count;
    void (*result_cb)(const char *permission, bool granted);
} AromaPermissionRequest;

typedef struct
{
    bool active;
    int request_code;
    void (*result_cb)(const char *path);
} AromaMediaRequest;

typedef struct
{
    void (*device_cb)(const char *addr, const char *name, int rssi);
    void (*scan_finished_cb)(void);
    void (*connection_cb)(const char *addr, int status, bool connected);
    void (*services_cb)(const char *addr, const char *services_csv);
    void (*data_cb)(const char *addr, const char *char_uuid, const char *data, int len);
    void (*write_cb)(const char *addr, const char *char_uuid, int status);
} AromaBleCallbacks;

typedef struct
{
    void (*sensor_cb)(int type, float x, float y, float z, long long timestamp_ns);
} AromaSensorCallbacks;

static AromaPermissionRequest g_perm_request = {0};
static AromaMediaRequest g_media_requests[5] = {{0}};
static AromaBleCallbacks g_ble_callbacks = {0};
static AromaSensorCallbacks g_sensor_callbacks = {0};
static void request_permissions_code(const char **permissions, int permCount, int requestCode);
static void impl_android_orient_lock_new(void);
static void impl_android_orient_unlock_new(void);
static void impl_android_orient_portrait_new(void);
static void impl_android_orient_landscape_new(void);
static void impl_android_orient_sensor_new(void);
static bool impl_android_orient_locked_new(void);
static void JNICALL native_on_nfc_tag(JNIEnv *env, jobject thiz, jstring payload);
static void JNICALL native_on_rfid_tag(JNIEnv *env, jobject thiz, jstring uid);
static void JNICALL native_on_biometric(JNIEnv *env, jobject thiz, jboolean success);
static void JNICALL native_on_location(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon, jfloat accuracy, jlong timeMs);
static void JNICALL native_on_screenshot(JNIEnv *env, jobject thiz, jstring path);

static void JNICALL native_on_text_input(JNIEnv *env, jobject thiz, jstring text)
{
    (void)thiz;
    if (!env || !text)
        return;
    const char *utf = (*env)->GetStringUTFChars(env, text, NULL);
    if (!utf)
        return;
    AromaNode *focused = aroma_textbox_get_focused();
    if (!focused)
        focused = aroma_ui_get_focused_node();
    if (focused)
    {
        const char *p = utf;
        while (*p)
        {
            aroma_textbox_on_char(focused, *p);
            p++;
        }
        aroma_ui_request_redraw(NULL);
    }
    (*env)->ReleaseStringUTFChars(env, text, utf);
}

static void JNICALL native_on_backspace(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    AromaNode *focused = aroma_textbox_get_focused();
    if (!focused)
        focused = aroma_ui_get_focused_node();
    if (focused)
    {
        aroma_textbox_on_backspace(focused);
        aroma_ui_request_redraw(NULL);
    }
}

static void JNICALL native_on_editor_done(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    AromaNode *focused = aroma_ui_get_focused_node();
    if (focused)
        aroma_textbox_set_focused(focused, false);
}

static void JNICALL native_on_permission_result(JNIEnv *env, jobject thiz,
                                                jint requestCode,
                                                jobjectArray permissions,
                                                jintArray grantResults)
{
    (void)thiz;
    if (requestCode != 1001 || !g_perm_request.active || !g_perm_request.result_cb)
    {
        return;
    }
    g_perm_request.active = false;
    if (!permissions || !grantResults)
    {
        return;
    }
    jsize n = (*env)->GetArrayLength(env, permissions);
    jsize m = (*env)->GetArrayLength(env, grantResults);
    jint *grants = (*env)->GetIntArrayElements(env, grantResults, NULL);
    jsize count = n < m ? n : m;
    if (count > g_perm_request.perm_count)
    {
        count = g_perm_request.perm_count;
    }
    int i = 0;
    for (i = 0; i < count; i++)
    {
        jstring perm = (jstring)(*env)->GetObjectArrayElement(env, permissions, i);
        const char *perm_str = perm ? (*env)->GetStringUTFChars(env, perm, NULL) : "";
        g_perm_request.result_cb(perm_str, grants[i] == 0);
        if (perm)
        {
            (*env)->ReleaseStringUTFChars(env, perm, perm_str);
            (*env)->DeleteLocalRef(env, perm);
        }
    }
    (*env)->ReleaseIntArrayElements(env, grantResults, grants, JNI_ABORT);
}

static void JNICALL native_on_activity_result(JNIEnv *env, jobject thiz,
                                              jint requestCode,
                                              jint resultCode,
                                              jstring data,
                                              jstring extra)
{
    (void)thiz;
    (void)resultCode;
    (void)extra;
    int i = 0;
    for (i = 0; i < 5; i++)
    {
        if (g_media_requests[i].active && g_media_requests[i].request_code == requestCode)
        {
            g_media_requests[i].active = false;
            if (g_media_requests[i].result_cb)
            {
                if (data)
                {
                    const char *path = (*env)->GetStringUTFChars(env, data, NULL);
                    g_media_requests[i].result_cb(path);
                    (*env)->ReleaseStringUTFChars(env, data, path);
                }
                else
                {
                    g_media_requests[i].result_cb(NULL);
                }
            }
            return;
        }
    }
}

static void JNICALL native_on_ble_device(JNIEnv *env, jobject thiz,
                                         jstring address, jstring name, jint rssi)
{
    (void)thiz;
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    const char *name_str = name ? (*env)->GetStringUTFChars(env, name, NULL) : "";
    if (g_ble_callbacks.device_cb)
    {
        g_ble_callbacks.device_cb(addr_str, name_str, (int)rssi);
    }
    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
    if (name)
        (*env)->ReleaseStringUTFChars(env, name, name_str);
}

static void JNICALL native_on_ble_scan_finished(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    if (g_ble_callbacks.scan_finished_cb)
    {
        g_ble_callbacks.scan_finished_cb();
    }
}

static void JNICALL native_on_ble_connection(JNIEnv *env, jobject thiz,
                                             jstring address, jint status, jboolean connected)
{
    (void)thiz;
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    if (g_ble_callbacks.connection_cb)
    {
        g_ble_callbacks.connection_cb(addr_str, (int)status, connected == JNI_TRUE);
    }
    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
}

static void JNICALL native_on_ble_services(JNIEnv *env, jobject thiz,
                                           jstring address, jstring services)
{
    (void)thiz;
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    const char *svc_str = services ? (*env)->GetStringUTFChars(env, services, NULL) : "";
    if (g_ble_callbacks.services_cb)
    {
        g_ble_callbacks.services_cb(addr_str, svc_str);
    }
    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
    if (services)
        (*env)->ReleaseStringUTFChars(env, services, svc_str);
}

static void JNICALL native_on_ble_data(JNIEnv *env, jobject thiz,
                                       jstring address, jstring charUuid,
                                       jbyteArray data, jint length)
{
    (void)thiz;
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    const char *uuid_str = charUuid ? (*env)->GetStringUTFChars(env, charUuid, NULL) : "";
    if (g_ble_callbacks.data_cb && data)
    {
        jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
        g_ble_callbacks.data_cb(addr_str, uuid_str, (const char *)bytes, (int)length);
        (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    }
    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
    if (charUuid)
        (*env)->ReleaseStringUTFChars(env, charUuid, uuid_str);
}

static void JNICALL native_on_ble_write(JNIEnv *env, jobject thiz,
                                       jstring address, jstring charUuid, jint status)
{
    (void)thiz;
    const char *addr_str = address ? (*env)->GetStringUTFChars(env, address, NULL) : "";
    const char *uuid_str = charUuid ? (*env)->GetStringUTFChars(env, charUuid, NULL) : "";
    if (g_ble_callbacks.write_cb)
    {
        g_ble_callbacks.write_cb(addr_str, uuid_str, (int)status);
    }
    if (address)
        (*env)->ReleaseStringUTFChars(env, address, addr_str);
    if (charUuid)
        (*env)->ReleaseStringUTFChars(env, charUuid, uuid_str);
}

static void JNICALL native_on_sensor_changed(JNIEnv *env, jobject thiz,
                                             jint type, jfloat x, jfloat y, jfloat z, jlong timestamp)
{
    (void)env;
    (void)thiz;
    if (g_sensor_callbacks.sensor_cb)
    {
        g_sensor_callbacks.sensor_cb((int)type, (float)x, (float)y, (float)z, (long long)timestamp);
    }
}

static bool get_app_package(JNIEnv *env, jobject activity, char *out, size_t out_len)
{
    if (!env || !activity || !out || out_len == 0)
    {
        return false;
    }
    jclass activityClass = (*env)->GetObjectClass(env, activity);
    if (!activityClass)
    {
        return false;
    }
    jmethodID getPackageName = (*env)->GetMethodID(env, activityClass, "getPackageName", "()Ljava/lang/String;");
    if (!getPackageName)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        return false;
    }
    jstring pkg = (jstring)(*env)->CallObjectMethod(env, activity, getPackageName);
    bool ok = false;
    if (pkg && !(*env)->ExceptionCheck(env))
    {
        const char *utf = (*env)->GetStringUTFChars(env, pkg, NULL);
        if (utf)
        {
            strncpy(out, utf, out_len - 1);
            out[out_len - 1] = '\0';
            ok = out[0] != '\0';
            (*env)->ReleaseStringUTFChars(env, pkg, utf);
        }
        (*env)->DeleteLocalRef(env, pkg);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, activityClass);
    return ok;
}

static jclass load_class_dotted(JNIEnv *env, jobject classLoader, jmethodID loadClass, const char *dotted)
{
    if (!env || !classLoader || !loadClass || !dotted)
    {
        return NULL;
    }
    jstring jname = (*env)->NewStringUTF(env, dotted);
    if (!jname)
    {
        return NULL;
    }
    jclass cls = (jclass)(*env)->CallObjectMethod(env, classLoader, loadClass, jname);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        cls = NULL;
    }
    (*env)->DeleteLocalRef(env, jname);
    return cls;
}

static jclass load_helper_class(JNIEnv *env, jobject classLoader, jmethodID loadClass, const char *pkg, const char *leaf)
{
    char dotted[256];
    jclass cls = NULL;
    if (pkg && pkg[0])
    {
        size_t i = 0;
        size_t o = 0;
        while (pkg[i] && o + 1 < sizeof(dotted))
        {
            dotted[o++] = pkg[i] == '/' ? '.' : pkg[i];
            i++;
        }
        if (o + 1 < sizeof(dotted))
        {
            dotted[o++] = '.';
        }
        size_t j = 0;
        while (leaf[j] && o + 1 < sizeof(dotted))
        {
            dotted[o++] = leaf[j];
            j++;
        }
        dotted[o] = '\0';
        cls = load_class_dotted(env, classLoader, loadClass, dotted);
    }
    if (!cls)
    {
        cls = load_class_dotted(env, classLoader, loadClass, leaf);
    }
    return cls;
}

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
        LOG_ERROR("Cannot find AromaHelper: no activity");
        return false;
    }

    jclass activityClass = (*env)->GetObjectClass(env, activity);
    jmethodID getClassLoader = (*env)->GetMethodID(env, activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");

    if (!getClassLoader)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        LOG_ERROR("Failed to get class loader method");
        return false;
    }

    jobject classLoader = (*env)->CallObjectMethod(env, activity, getClassLoader);
    if (!classLoader || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        LOG_ERROR("Failed to get class loader");
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
        LOG_ERROR("Failed to get loadClass method");
        return false;
    }

    char pkg_name[192] = {0};
    get_app_package(env, activity, pkg_name, sizeof(pkg_name));

    jclass helper = load_helper_class(env, classLoader, loadClass, pkg_name, "AromaHelper");

    if (!helper)
    {
        (*env)->DeleteLocalRef(env, classLoaderClass);
        (*env)->DeleteLocalRef(env, classLoader);
        (*env)->DeleteLocalRef(env, activityClass);
        LOG_ERROR("AromaHelper class not found");
        return false;
    }

    g_helper_cache.helper_class = (jclass)(*env)->NewGlobalRef(env, helper);

    g_helper_cache.init = (*env)->GetStaticMethodID(env, helper, "init", "(Landroid/content/Context;)V");
    {
        char pkg_slash[192] = {0};
        size_t pi = 0;
        while (pkg_name[pi] && pi + 1 < sizeof(pkg_slash))
        {
            pkg_slash[pi] = pkg_name[pi] == '.' ? '/' : pkg_name[pi];
            pi++;
        }
        pkg_slash[pi] = '\0';
        char sig_cb[256];
        char sig_rm[256];
        if (pkg_slash[0])
        {
            snprintf(sig_cb, sizeof(sig_cb), "(L%s/AromaHelper$BluetoothCallback;)V", pkg_slash);
            snprintf(sig_rm, sizeof(sig_rm), "(L%s/AromaHelper$BluetoothCallback;)V", pkg_slash);
        }
        else
        {
            snprintf(sig_cb, sizeof(sig_cb), "(LAromaHelper$BluetoothCallback;)V");
            snprintf(sig_rm, sizeof(sig_rm), "(LAromaHelper$BluetoothCallback;)V");
        }
        g_helper_cache.add_callback = (*env)->GetStaticMethodID(env, helper, "addCallback", sig_cb);
        if (!g_helper_cache.add_callback)
        {
            (*env)->ExceptionClear(env);
        }
        g_helper_cache.remove_callback = (*env)->GetStaticMethodID(env, helper, "removeCallback", sig_rm);
        if (!g_helper_cache.remove_callback)
        {
            (*env)->ExceptionClear(env);
        }
        if (pkg_slash[0])
        {
            snprintf(sig_cb, sizeof(sig_cb), "(L%s/AromaHelper$SensorCallback;)V", pkg_slash);
        }
        else
        {
            snprintf(sig_cb, sizeof(sig_cb), "(LAromaHelper$SensorCallback;)V");
        }
        g_helper_cache.add_sensor_callback = (*env)->GetStaticMethodID(env, helper, "addSensorCallback", sig_cb);
        if (!g_helper_cache.add_sensor_callback)
        {
            (*env)->ExceptionClear(env);
        }
        if (pkg_slash[0])
        {
            snprintf(sig_rm, sizeof(sig_rm), "(L%s/AromaHelper$SensorCallback;)V", pkg_slash);
        }
        else
        {
            snprintf(sig_rm, sizeof(sig_rm), "(LAromaHelper$SensorCallback;)V");
        }
        g_helper_cache.remove_sensor_callback = (*env)->GetStaticMethodID(env, helper, "removeSensorCallback", sig_rm);
        if (!g_helper_cache.remove_sensor_callback)
        {
            (*env)->ExceptionClear(env);
        }
    }
    g_helper_cache.show_toast = (*env)->GetStaticMethodID(env, helper, "showToast", "(Landroid/app/Activity;Ljava/lang/String;Z)V");
    g_helper_cache.bt_scan = (*env)->GetStaticMethodID(env, helper, "startScan", "(I)V");
    g_helper_cache.bt_stop_scan = (*env)->GetStaticMethodID(env, helper, "stopScan", "()V");
    g_helper_cache.bt_get_paired = (*env)->GetStaticMethodID(env, helper, "btGetPairedDevices", "()[Ljava/lang/String;");
    g_helper_cache.bt_pair = (*env)->GetStaticMethodID(env, helper, "btPair", "(Ljava/lang/String;)Z");
    g_helper_cache.bt_unpair = (*env)->GetStaticMethodID(env, helper, "btUnpair", "(Ljava/lang/String;)Z");
    g_helper_cache.bt_get_pair_state = (*env)->GetStaticMethodID(env, helper, "btGetPairState", "(Ljava/lang/String;)I");
    g_helper_cache.bt_connect = (*env)->GetStaticMethodID(env, helper, "btConnect", "(Ljava/lang/String;)Z");
    g_helper_cache.bt_connect_with_mode = (*env)->GetStaticMethodID(env, helper, "btConnectWithMode", "(Ljava/lang/String;I)Z");
    g_helper_cache.bt_disconnect = (*env)->GetStaticMethodID(env, helper, "btDisconnect", "()V");
    g_helper_cache.bt_send = (*env)->GetStaticMethodID(env, helper, "btSend", "([B)I");
    g_helper_cache.bt_is_connected = (*env)->GetStaticMethodID(env, helper, "btIsConnected", "()Z");
    g_helper_cache.bt_get_device_type = (*env)->GetStaticMethodID(env, helper, "btGetDeviceType", "()I");
    g_helper_cache.bt_get_device_name = (*env)->GetStaticMethodID(env, helper, "btGetDeviceName", "()Ljava/lang/String;");
    g_helper_cache.bt_get_current_mode = (*env)->GetStaticMethodID(env, helper, "btGetCurrentMode", "()I");
    g_helper_cache.bt_get_mode_name = (*env)->GetStaticMethodID(env, helper, "btGetModeName", "()Ljava/lang/String;");
    g_helper_cache.get_preference = (*env)->GetStaticMethodID(env, helper, "getPref", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    g_helper_cache.set_preference = (*env)->GetStaticMethodID(env, helper, "setPref", "(Ljava/lang/String;Ljava/lang/String;)V");
    g_helper_cache.get_preference_bool = (*env)->GetStaticMethodID(env, helper, "getPrefBoolean", "(Ljava/lang/String;Z)Z");
    g_helper_cache.set_preference_bool = (*env)->GetStaticMethodID(env, helper, "setPrefBoolean", "(Ljava/lang/String;Z)V");
    g_helper_cache.get_preference_int = (*env)->GetStaticMethodID(env, helper, "getPrefInt", "(Ljava/lang/String;I)I");
    g_helper_cache.set_preference_int = (*env)->GetStaticMethodID(env, helper, "setPrefInt", "(Ljava/lang/String;I)V");
    g_helper_cache.get_preference_float = (*env)->GetStaticMethodID(env, helper, "getPrefFloat", "(Ljava/lang/String;F)F");
    g_helper_cache.set_preference_float = (*env)->GetStaticMethodID(env, helper, "setPrefFloat", "(Ljava/lang/String;F)V");
    g_helper_cache.get_preference_long = (*env)->GetStaticMethodID(env, helper, "getPrefLong", "(Ljava/lang/String;J)J");
    g_helper_cache.set_preference_long = (*env)->GetStaticMethodID(env, helper, "setPrefLong", "(Ljava/lang/String;J)V");

    if (!g_helper_cache.show_toast)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_scan)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_stop_scan)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_paired)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_pair)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_unpair)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_pair_state)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_connect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_connect_with_mode)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_disconnect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_send)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_is_connected)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_device_type)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_device_name)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_current_mode)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bt_get_mode_name)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.get_preference)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.set_preference)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.get_preference_bool)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.set_preference_bool)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.get_preference_int)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.set_preference_int)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.get_preference_float)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.set_preference_float)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.get_preference_long)
        (*env)->ExceptionClear(env);
    if(!g_helper_cache.set_preference_long)
        (*env)->ExceptionClear(env);
    g_helper_cache.notify_channel = (*env)->GetStaticMethodID(env, helper, "notifyChannel", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
    g_helper_cache.notify_show = (*env)->GetStaticMethodID(env, helper, "notifyShow", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    g_helper_cache.notify_cancel = (*env)->GetStaticMethodID(env, helper, "notifyCancel", "(I)V");
    g_helper_cache.notify_cancel_all = (*env)->GetStaticMethodID(env, helper, "notifyCancelAll", "()V");
    g_helper_cache.notify_enabled = (*env)->GetStaticMethodID(env, helper, "notifyEnabled", "()Z");
    g_helper_cache.vibrate_effect = (*env)->GetStaticMethodID(env, helper, "vibrateEffect", "(JI)V");
    g_helper_cache.clip_set = (*env)->GetStaticMethodID(env, helper, "clipSet", "(Ljava/lang/String;)V");
    g_helper_cache.clip_get = (*env)->GetStaticMethodID(env, helper, "clipGet", "()Ljava/lang/String;");
    g_helper_cache.share_text = (*env)->GetStaticMethodID(env, helper, "shareText", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V");
    g_helper_cache.ui_immersive = (*env)->GetStaticMethodID(env, helper, "uiImmersive", "(Landroid/app/Activity;Z)V");
    g_helper_cache.ui_keep_screen_on = (*env)->GetStaticMethodID(env, helper, "uiKeepScreenOn", "(Landroid/app/Activity;Z)V");
    g_helper_cache.orient_lock = (*env)->GetStaticMethodID(env, helper, "orientLock", "(Landroid/app/Activity;)V");
    g_helper_cache.orient_set = (*env)->GetStaticMethodID(env, helper, "orientSet", "(Landroid/app/Activity;I)V");
    g_helper_cache.orient_unlock = (*env)->GetStaticMethodID(env, helper, "orientUnlock", "(Landroid/app/Activity;)V");
    g_helper_cache.orient_locked = (*env)->GetStaticMethodID(env, helper, "orientLocked", "(Landroid/app/Activity;)Z");
    g_helper_cache.pick_image = (*env)->GetStaticMethodID(env, helper, "pickImage", "(Landroid/app/Activity;I)V");
    g_helper_cache.capture_photo = (*env)->GetStaticMethodID(env, helper, "capturePhoto", "(Landroid/app/Activity;I)V");
    g_helper_cache.open_document = (*env)->GetStaticMethodID(env, helper, "openDocument", "(Landroid/app/Activity;Ljava/lang/String;I)V");
    g_helper_cache.sensor_start = (*env)->GetStaticMethodID(env, helper, "sensorStart", "(II)Z");
    g_helper_cache.sensor_stop = (*env)->GetStaticMethodID(env, helper, "sensorStop", "(I)V");
    g_helper_cache.sensor_available = (*env)->GetStaticMethodID(env, helper, "sensorAvailable", "(I)Z");
    g_helper_cache.tts_speak = (*env)->GetStaticMethodID(env, helper, "ttsSpeak", "(Ljava/lang/String;)V");
    g_helper_cache.tts_stop = (*env)->GetStaticMethodID(env, helper, "ttsStop", "()V");
    g_helper_cache.tts_is_speaking = (*env)->GetStaticMethodID(env, helper, "ttsIsSpeaking", "()Z");
    g_helper_cache.btle_start_scan = (*env)->GetStaticMethodID(env, helper, "btleStartScan", "(Ljava/lang/String;I)V");
    g_helper_cache.btle_stop_scan = (*env)->GetStaticMethodID(env, helper, "btleStopScan", "()V");
    g_helper_cache.btle_connect = (*env)->GetStaticMethodID(env, helper, "btleConnect", "(Ljava/lang/String;)V");
    g_helper_cache.btle_disconnect = (*env)->GetStaticMethodID(env, helper, "btleDisconnect", "()V");
    g_helper_cache.btle_is_connected = (*env)->GetStaticMethodID(env, helper, "btleIsConnected", "()Z");
    g_helper_cache.btle_discover = (*env)->GetStaticMethodID(env, helper, "btleDiscover", "()Z");
    g_helper_cache.btle_read = (*env)->GetStaticMethodID(env, helper, "btleRead", "(Ljava/lang/String;Ljava/lang/String;)Z");
    g_helper_cache.btle_write = (*env)->GetStaticMethodID(env, helper, "btleWrite", "(Ljava/lang/String;Ljava/lang/String;[BI)Z");
    g_helper_cache.btle_notify = (*env)->GetStaticMethodID(env, helper, "btleNotify", "(Ljava/lang/String;Ljava/lang/String;Z)Z");
    g_helper_cache.dev_manufacturer = (*env)->GetStaticMethodID(env, helper, "devManufacturer", "()Ljava/lang/String;");
    g_helper_cache.dev_model = (*env)->GetStaticMethodID(env, helper, "devModel", "()Ljava/lang/String;");
    g_helper_cache.dev_os_version = (*env)->GetStaticMethodID(env, helper, "devOsVersion", "()Ljava/lang/String;");
    g_helper_cache.dev_sdk_int = (*env)->GetStaticMethodID(env, helper, "devSdkInt", "()I");
    g_helper_cache.app_package = (*env)->GetStaticMethodID(env, helper, "appPackage", "()Ljava/lang/String;");
    g_helper_cache.app_version_name = (*env)->GetStaticMethodID(env, helper, "appVersionName", "()Ljava/lang/String;");
    g_helper_cache.app_version_code = (*env)->GetStaticMethodID(env, helper, "appVersionCode", "()J");
    g_helper_cache.app_install_time = (*env)->GetStaticMethodID(env, helper, "appInstallTime", "()J");
    g_helper_cache.mem_avail_mb = (*env)->GetStaticMethodID(env, helper, "memAvailMb", "()J");
    g_helper_cache.mem_low = (*env)->GetStaticMethodID(env, helper, "memLow", "()Z");
    g_helper_cache.net_connected = (*env)->GetStaticMethodID(env, helper, "netConnected", "()Z");
    g_helper_cache.net_type = (*env)->GetStaticMethodID(env, helper, "netType", "()I");
    g_helper_cache.batt_charging = (*env)->GetStaticMethodID(env, helper, "battCharging", "()Z");
    g_helper_cache.dev_interactive = (*env)->GetStaticMethodID(env, helper, "devInteractive", "()Z");
    g_helper_cache.locale_tag = (*env)->GetStaticMethodID(env, helper, "localeTag", "()Ljava/lang/String;");
    g_helper_cache.timezone_id = (*env)->GetStaticMethodID(env, helper, "timezoneId", "()Ljava/lang/String;");
    g_helper_cache.net_operator = (*env)->GetStaticMethodID(env, helper, "netOperator", "()Ljava/lang/String;");
    g_helper_cache.wake_acquire = (*env)->GetStaticMethodID(env, helper, "wakeAcquire", "(J)V");
    g_helper_cache.wake_release = (*env)->GetStaticMethodID(env, helper, "wakeRelease", "()V");
    g_helper_cache.torch_set = (*env)->GetStaticMethodID(env, helper, "torchSet", "(Z)Z");
    if (!g_helper_cache.notify_channel)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.notify_show)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.notify_cancel)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.notify_cancel_all)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.notify_enabled)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.vibrate_effect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.clip_set)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.clip_get)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.share_text)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.ui_immersive)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.ui_keep_screen_on)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.orient_lock)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.orient_set)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.orient_unlock)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.orient_locked)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.pick_image)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.capture_photo)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.open_document)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sensor_start)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sensor_stop)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sensor_available)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.tts_speak)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.tts_stop)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.tts_is_speaking)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_start_scan)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_stop_scan)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_connect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_disconnect)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_is_connected)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_discover)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_read)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_write)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.btle_notify)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.dev_manufacturer)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.dev_model)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.dev_os_version)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.dev_sdk_int)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.app_package)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.app_version_name)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.app_version_code)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.app_install_time)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.mem_avail_mb)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.mem_low)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.net_connected)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.net_type)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.batt_charging)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.dev_interactive)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.locale_tag)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.timezone_id)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.net_operator)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.wake_acquire)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.wake_release)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.torch_set)    g_helper_cache.nfc_start = (*env)->GetStaticMethodID(env, helper, "nfcStart", "(Landroid/app/Activity;)V");
    g_helper_cache.nfc_stop = (*env)->GetStaticMethodID(env, helper, "nfcStop", "(Landroid/app/Activity;)V");
    g_helper_cache.nfc_available = (*env)->GetStaticMethodID(env, helper, "nfcAvailable", "()Z");
    g_helper_cache.nfc_enabled = (*env)->GetStaticMethodID(env, helper, "nfcEnabled", "()Z");
    g_helper_cache.bio_available = (*env)->GetStaticMethodID(env, helper, "bioAvailable", "()I");
    g_helper_cache.bio_auth = (*env)->GetStaticMethodID(env, helper, "bioAuth", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V");
    g_helper_cache.loc_start = (*env)->GetStaticMethodID(env, helper, "locStart", "(JF)Z");
    g_helper_cache.loc_stop = (*env)->GetStaticMethodID(env, helper, "locStop", "()V");
    g_helper_cache.loc_available = (*env)->GetStaticMethodID(env, helper, "locAvailable", "()Z");
    g_helper_cache.rec_start = (*env)->GetStaticMethodID(env, helper, "recStart", "(Ljava/lang/String;)Z");
    g_helper_cache.rec_stop = (*env)->GetStaticMethodID(env, helper, "recStop", "()Z");
    g_helper_cache.sc_add = (*env)->GetStaticMethodID(env, helper, "scAdd", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
    g_helper_cache.sc_remove = (*env)->GetStaticMethodID(env, helper, "scRemove", "(Ljava/lang/String;)Z");
    g_helper_cache.sc_count = (*env)->GetStaticMethodID(env, helper, "scCount", "()I");
    g_helper_cache.pip_enter = (*env)->GetStaticMethodID(env, helper, "pipEnter", "(Landroid/app/Activity;II)Z");
    g_helper_cache.pip_available = (*env)->GetStaticMethodID(env, helper, "pipAvailable", "()Z");
    g_helper_cache.wp_set_image = (*env)->GetStaticMethodID(env, helper, "wpSetImage", "(Ljava/lang/String;)Z");
    g_helper_cache.vol_music_get = (*env)->GetStaticMethodID(env, helper, "volMusicGet", "()I");
    g_helper_cache.vol_music_max = (*env)->GetStaticMethodID(env, helper, "volMusicMax", "()I");
    g_helper_cache.vol_music_set = (*env)->GetStaticMethodID(env, helper, "volMusicSet", "(I)V");
    g_helper_cache.ringer_get = (*env)->GetStaticMethodID(env, helper, "ringerGet", "()I");
    g_helper_cache.storage_free_mb = (*env)->GetStaticMethodID(env, helper, "storageFreeMb", "()J");
    g_helper_cache.storage_total_mb = (*env)->GetStaticMethodID(env, helper, "storageTotalMb", "()J");
    g_helper_cache.sysbar_status_px = (*env)->GetStaticMethodID(env, helper, "sysbarStatusPx", "(Landroid/app/Activity;)I");
    g_helper_cache.sysbar_nav_px = (*env)->GetStaticMethodID(env, helper, "sysbarNavPx", "(Landroid/app/Activity;)I");
    g_helper_cache.kb_visible = (*env)->GetStaticMethodID(env, helper, "kbVisible", "(Landroid/app/Activity;)Z");
    g_helper_cache.app_installed = (*env)->GetStaticMethodID(env, helper, "appInstalled", "(Ljava/lang/String;)Z");
    g_helper_cache.app_open = (*env)->GetStaticMethodID(env, helper, "appOpen", "(Ljava/lang/String;)Z");
    g_helper_cache.contacts_pick = (*env)->GetStaticMethodID(env, helper, "contactsPick", "(Landroid/app/Activity;I)V");
    g_helper_cache.shot_capture = (*env)->GetStaticMethodID(env, helper, "shotCapture", "(Landroid/app/Activity;)V");
    g_helper_cache.http_download = (*env)->GetStaticMethodID(env, helper, "downloadToFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
    g_helper_cache.open_url = (*env)->GetStaticMethodID(env, helper, "openUrl", "(Ljava/lang/String;)Z");
    g_helper_cache.show_ime = (*env)->GetStaticMethodID(env, helper, "showIme", "(Landroid/app/Activity;)V");
    g_helper_cache.hide_ime = (*env)->GetStaticMethodID(env, helper, "hideIme", "(Landroid/app/Activity;)V");
    g_helper_cache.ir_available = (*env)->GetStaticMethodID(env, helper, "irAvailable", "()Z");
    g_helper_cache.ir_transmit = (*env)->GetStaticMethodID(env, helper, "irTransmit", "(I[I)Z");
    if (!g_helper_cache.nfc_start)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.nfc_stop)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.nfc_available)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.nfc_enabled)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bio_available)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.bio_auth)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.loc_start)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.loc_stop)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.loc_available)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.rec_start)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.rec_stop)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sc_add)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sc_remove)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sc_count)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.pip_enter)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.pip_available)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.wp_set_image)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.vol_music_get)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.vol_music_max)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.vol_music_set)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.ringer_get)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.storage_free_mb)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.storage_total_mb)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sysbar_status_px)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.sysbar_nav_px)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.kb_visible)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.app_installed)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.app_open)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.contacts_pick)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.shot_capture)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.ir_available)
        (*env)->ExceptionClear(env);
    if (!g_helper_cache.ir_transmit)
        (*env)->ExceptionClear(env);

    if (g_helper_cache.init && activity)
    {
        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.init, activity);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
        }
    }

    jclass nativeCallbackClass = load_helper_class(env, classLoader, loadClass, pkg_name, "AromaHelper$NativeCallback");
    if (!nativeCallbackClass)
    {
        nativeCallbackClass = find_class_safe(env, "AromaHelper$NativeCallback");
    }

    if (!nativeCallbackClass)
    {
        LOG_ERROR("Failed to find native callback class");
        (*env)->DeleteLocalRef(env, helper);
        (*env)->DeleteLocalRef(env, classLoaderClass);
        (*env)->DeleteLocalRef(env, classLoader);
        (*env)->DeleteLocalRef(env, activityClass);
        return false;
    }

    g_helper_cache.native_callback_class = (jclass)(*env)->NewGlobalRef(env, nativeCallbackClass);

    JNINativeMethod methods[] = {
        {"onDeviceDiscovered", "(Ljava/lang/String;Ljava/lang/String;II)V", (void *)native_on_device_discovered},
        {"onScanFinished", "()V", (void *)native_on_scan_finished},
        {"onPairingResult", "(ZLjava/lang/String;Ljava/lang/String;)V", (void *)native_on_pairing_result},
        {"onConnectionResult", "(ZLjava/lang/String;II)V", (void *)native_on_connection_result},
        {"onDataReceived", "([BI)V", (void *)native_on_data_received},
        {"onConnectionStateChanged", "(I)V", (void *)native_on_connection_state_changed},
        {"onBleDevice", "(Ljava/lang/String;Ljava/lang/String;I)V", (void *)native_on_ble_device},
        {"onBleScanFinished", "()V", (void *)native_on_ble_scan_finished},
        {"onBleConnection", "(Ljava/lang/String;IZ)V", (void *)native_on_ble_connection},
        {"onBleServices", "(Ljava/lang/String;Ljava/lang/String;)V", (void *)native_on_ble_services},
        {"onBleData", "(Ljava/lang/String;Ljava/lang/String;[BI)V", (void *)native_on_ble_data},
        {"onBleWrite", "(Ljava/lang/String;Ljava/lang/String;I)V", (void *)native_on_ble_write},
        {"onSensorChanged", "(IFFFJ)V", (void *)native_on_sensor_changed},
        {"onNfcTag", "(Ljava/lang/String;)V", (void *)native_on_nfc_tag},
        {"onRfidTag", "(Ljava/lang/String;)V", (void *)native_on_rfid_tag},
        {"onBiometric", "(Z)V", (void *)native_on_biometric},
        {"onLocation", "(DDFJ)V", (void *)native_on_location},
        {"onScreenshot", "(Ljava/lang/String;)V", (void *)native_on_screenshot}};

    jint register_result = (*env)->RegisterNatives(env, nativeCallbackClass, methods, 18);
    if (register_result != JNI_OK)
    {
        LOG_ERROR("Failed to register native methods: %d", register_result);
    }

    jmethodID constructor = (*env)->GetMethodID(env, nativeCallbackClass, "<init>", "()V");
    if (!constructor)
    {
        LOG_ERROR("Failed to find constructor for native callback class");
        (*env)->DeleteLocalRef(env, nativeCallbackClass);
        (*env)->DeleteLocalRef(env, helper);
        (*env)->DeleteLocalRef(env, classLoaderClass);
        (*env)->DeleteLocalRef(env, classLoader);
        (*env)->DeleteLocalRef(env, activityClass);
        return false;
    }

    jobject callbackObj = (*env)->NewObject(env, nativeCallbackClass, constructor);
    if (!callbackObj)
    {
        LOG_ERROR("Failed to create callback object");
        (*env)->DeleteLocalRef(env, nativeCallbackClass);
        (*env)->DeleteLocalRef(env, helper);
        (*env)->DeleteLocalRef(env, classLoaderClass);
        (*env)->DeleteLocalRef(env, classLoader);
        (*env)->DeleteLocalRef(env, activityClass);
        return false;
    }

    g_helper_cache.callback_obj = (*env)->NewGlobalRef(env, callbackObj);

    if (g_helper_cache.add_callback && g_helper_cache.callback_obj)
    {
        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.add_callback, g_helper_cache.callback_obj);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            LOG_ERROR("Exception while adding callback");
        }
        else
        {
            LOG_INFO("Callback added successfully");
        }
    }

    if (g_helper_cache.add_sensor_callback && g_helper_cache.callback_obj)
    {
        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.add_sensor_callback, g_helper_cache.callback_obj);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            LOG_ERROR("Exception while adding sensor callback");
        }
    }

    {
        jclass activity_class = load_helper_class(env, classLoader, loadClass, pkg_name, "AromaActivity");
        if (activity_class)
        {
            g_helper_cache.activity_class = (jclass)(*env)->NewGlobalRef(env, activity_class);
            JNINativeMethod activity_methods[] = {
                {"nativeOnPermissionResult", "(I[Ljava/lang/String;[I)V", (void *)native_on_permission_result},
                {"nativeOnActivityResult", "(IILjava/lang/String;Ljava/lang/String;)V", (void *)native_on_activity_result},
                {"nativeOnTextInput", "(Ljava/lang/String;)V", (void *)native_on_text_input},
                {"nativeOnBackspace", "()V", (void *)native_on_backspace},
                {"nativeOnEditorDone", "()V", (void *)native_on_editor_done}};
            if ((*env)->RegisterNatives(env, activity_class, activity_methods, 5) != JNI_OK)
            {
                LOG_ERROR("Failed to register activity native methods");
            }
            (*env)->DeleteLocalRef(env, activity_class);
        }
        else
        {
            LOG_ERROR("AromaActivity class not found, activity results unavailable");
        }
    }

    (*env)->DeleteLocalRef(env, callbackObj);
    (*env)->DeleteLocalRef(env, nativeCallbackClass);
    (*env)->DeleteLocalRef(env, helper);
    (*env)->DeleteLocalRef(env, classLoaderClass);
    (*env)->DeleteLocalRef(env, classLoader);
    (*env)->DeleteLocalRef(env, activityClass);

    g_helper_cache.initialized = true;
    LOG_INFO("AromaHelper initialized successfully");
    return true;
}

static void cache_physical_screen_info(struct android_app *app)
{
    if (g_phys_cached || !app || !app->activity)
        return;

    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
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
    jfieldID density_field = (*env)->GetFieldID(env, dm_class, "density", "F");
    jfieldID density_dpi_field = (*env)->GetFieldID(env, dm_class, "densityDpi", "I");
    jfieldID scaled_density_field = (*env)->GetFieldID(env, dm_class, "scaledDensity", "F");
    jfieldID xdpi_field = (*env)->GetFieldID(env, dm_class, "xdpi", "F");
    jfieldID ydpi_field = (*env)->GetFieldID(env, dm_class, "ydpi", "F");

    g_phys_width = (*env)->GetIntField(env, dm, w_field);
    g_phys_height = (*env)->GetIntField(env, dm, h_field);

    g_density = (*env)->GetFloatField(env, dm, density_field);
    g_density_dpi = (*env)->GetIntField(env, dm, density_dpi_field);
    g_scaled_density = (*env)->GetFloatField(env, dm, scaled_density_field);
    g_xdpi = (*env)->GetFloatField(env, dm, xdpi_field);
    g_ydpi = (*env)->GetFloatField(env, dm, ydpi_field);

    jmethodID get_metrics = (*env)->GetMethodID(env, display_class, "getMetrics", "(Landroid/util/DisplayMetrics;)V");
    (*env)->CallVoidMethod(env, display_obj, get_metrics, dm);

    g_avail_width = (*env)->GetIntField(env, dm, w_field);
    g_avail_height = (*env)->GetIntField(env, dm, h_field);

    LOG_INFO("Display Metrics:");
    LOG_INFO("  Physical: %dx%d", g_phys_width, g_phys_height);
    LOG_INFO("  Available: %dx%d", g_avail_width, g_avail_height);
    LOG_INFO("  Density: %f (%d dpi)", g_density, g_density_dpi);
    LOG_INFO("  Scaled Density: %f", g_scaled_density);
    LOG_INFO("  Physical DPI: %f x %f", g_xdpi, g_ydpi);

    g_phys_cached = true;

    (*env)->DeleteLocalRef(env, dm);
    (*env)->DeleteLocalRef(env, dm_class);
    (*env)->DeleteLocalRef(env, display_obj);
    (*env)->DeleteLocalRef(env, display_class);
    (*env)->DeleteLocalRef(env, wm);
    (*env)->DeleteLocalRef(env, wm_class);
    (*env)->DeleteLocalRef(env, activity_class);

    detach_jni_env(attach);
}

static int dp_to_px(int dp)
{
    return (int)(dp * g_density + 0.5f);
}

static int sp_to_px(int sp)
{
    return (int)(sp * g_scaled_density + 0.5f);
}

static int px_to_dp(int px)
{
    return (int)(px / g_density + 0.5f);
}

static int px_to_sp(int px)
{
    return (int)(px / g_scaled_density + 0.5f);
}

static float dp_to_px_f(float dp)
{
    return dp * g_density;
}

static float sp_to_px_f(float sp)
{
    return sp * g_scaled_density;
}

static float px_to_dp_f(float px)
{
    return px / g_density;
}

static void get_available_size_dp(int *width_dp, int *height_dp)
{
    *width_dp = px_to_dp(g_avail_width);
    *height_dp = px_to_dp(g_avail_height);
}

static void get_screen_size_inches(float *width_in, float *height_in)
{
    *width_in = g_phys_width / g_xdpi;
    *height_in = g_phys_height / g_ydpi;
}

static float get_screen_diagonal_inches(void)
{
    float width_in = g_phys_width / g_xdpi;
    float height_in = g_phys_height / g_ydpi;
    return sqrt(width_in * width_in + height_in * height_in);
}

static const char *get_screen_size_category(void)
{
    float diagonal_in = get_screen_diagonal_inches();

    if (diagonal_in < 3.5f)
        return "small";
    if (diagonal_in < 5.0f)
        return "normal";
    if (diagonal_in < 7.0f)
        return "large";
    if (diagonal_in < 10.0f)
        return "xlarge";
    return "xxlarge";
}

static float android_get_density(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return g_density;
}

static int android_get_density_dpi(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return g_density_dpi;
}

static float android_get_scaled_density(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return g_scaled_density;
}

static int android_dp_to_px(int dp)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return dp_to_px(dp);
}

static int android_px_to_dp(int px)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return px_to_dp(px);
}

static int android_sp_to_px(int sp)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return sp_to_px(sp);
}

static int android_px_to_sp(int px)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return px_to_sp(px);
}

static float android_dp_to_px_f(float dp)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return dp_to_px_f(dp);
}

static float android_sp_to_px_f(float sp)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return sp_to_px_f(sp);
}

static float android_px_to_dp_f(float px)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return px_to_dp_f(px);
}

static void android_get_available_size_dp(int *width_dp, int *height_dp)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    get_available_size_dp(width_dp, height_dp);
}

static void android_get_screen_size_inches(float *width_inches, float *height_inches)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    get_screen_size_inches(width_inches, height_inches);
}

static float android_get_screen_diagonal_inches(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return get_screen_diagonal_inches();
}

static const char *android_get_screen_size_category(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return get_screen_size_category();
}

static float android_get_xdpi(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return g_xdpi;
}

static float android_get_ydpi(void)
{
    if (!g_phys_cached && g_app)
    {
        cache_physical_screen_info(g_app);
    }
    return g_ydpi;
}

static void android_open_url(const char *url)
{
    if (!g_app || !g_app->activity || !url)
    {
        LOG_ERROR("Cannot open URL: Invalid state or URL");
        return;
    }

    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jclass activity_class = (*env)->GetObjectClass(env, g_app->activity->clazz);
    if (!activity_class)
    {
        LOG_ERROR("Failed to get activity class");
        detach_jni_env(attach);
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
        LOG_ERROR("Exception occurred while launching browser intent");
    }

    detach_jni_env(attach);
}

static void android_send_intent(int action_enum, const char *uri, const char *type, const void *extras, int extra_count)
{
    if (!g_app || !g_app->activity)
    {
        LOG_ERROR("Cannot send intent: Invalid state");
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

    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
    jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
    jclass activity_class = (*env)->GetObjectClass(env, g_app->activity->clazz);

    if (!intent_class || !uri_class || !activity_class)
    {
        LOG_ERROR("Failed to find required classes for intent");
        detach_jni_env(attach);
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

    jmethodID start_activity = (*env)->GetMethodID(env, activity_class, "startActivity", "(Landroid/content/Intent;)V");
    (*env)->CallVoidMethod(env, g_app->activity->clazz, start_activity, intent_obj);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        LOG_ERROR("Exception while sending intent");
    }

    if (uri_obj)
        (*env)->DeleteLocalRef(env, uri_obj);
    (*env)->DeleteLocalRef(env, intent_obj);
    (*env)->DeleteLocalRef(env, jaction);
    (*env)->DeleteLocalRef(env, activity_class);
    (*env)->DeleteLocalRef(env, uri_class);
    (*env)->DeleteLocalRef(env, intent_class);

    detach_jni_env(attach);
}

static void update_surface_size(void)
{
    int w = 0, h = 0;

    if (g_using_vulkan)
    {

        if (!g_app || !g_app->window)
            return;
        w = ANativeWindow_getWidth(g_app->window);
        h = ANativeWindow_getHeight(g_app->window);
    }
    else
    {
        if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE)
            return;
        EGLint ew = 0, eh = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &ew);
        eglQuerySurface(display, surface, EGL_HEIGHT, &eh);
        w = (int)ew;
        h = (int)eh;
    }

    if (w <= 0 || h <= 0)
        return;

    if (w == g_width && h == g_height)
        return;

    LOG_INFO("Window surface resized: %dx%d -> %dx%d", g_width, g_height, w, h);

    g_width = w;
    g_height = h;

    /* Display size / font scale can change without a process restart
     * (configChanges covers density|screenLayout). Metrics are cached
     * once at startup, so refresh them here: otherwise dp/sp conversions
     * and available-dp geometry stay stale and rendering is no longer
     * DPI aware after the change. */
    g_phys_cached = false;
    cache_physical_screen_info(g_app);

    if (!g_using_vulkan)
        glViewport(0, 0, g_width, g_height);

    int viewport_width = 0;
    int viewport_height = 0;
    get_layout_viewport(&viewport_width, &viewport_height);

    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
    {
        if (g_windows[i].root_node)
        {
            AromaEvent *resize_event = aroma_event_create_resize(
                g_windows[i].root_node->node_id, viewport_width, viewport_height);
            LOG_INFO("Updating layout for window %d size: %dx%d",
                     i, viewport_width, viewport_height);
            aroma_event_dispatch(resize_event);
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
    case AMOTION_EVENT_ACTION_CANCEL:

        for (int i = 0; i < AROMA_MAX_TOUCHES; i++)
            aroma_event_handle_touch(i, (int)x, (int)y, 0);
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

    request_frame();
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

    g_using_vulkan = is_vulkan_backend();

    if (g_using_vulkan)
    {

        g_width = ANativeWindow_getWidth(app->window);
        g_height = ANativeWindow_getHeight(app->window);
        g_has_window = true;
        context = (EGLContext)1;

        static bool vk_resources_initialized = false;
        if (!vk_resources_initialized)
        {
            AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
            if (gfx)
            {
                if (gfx->setup_shared_window_resources)
                    gfx->setup_shared_window_resources();
                if (gfx->setup_separate_window_resources)
                    gfx->setup_separate_window_resources(0);
            }
            vk_resources_initialized = true;
        }

        LOG_INFO("Vulkan: init_display complete (%dx%d)", g_width, g_height);
        return 0;
    }

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
        LOG_ERROR("Failed to choose config with preserved swap behavior, trying fallback");
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
            LOG_ERROR("Failed to choose fallback config");
            return -1;
        }
    }

    EGLint format;
    eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);

    EGLSurface surf = eglCreateWindowSurface(dpy, config, app->window, NULL);
    if (surf == EGL_NO_SURFACE)
    {
        LOG_ERROR("Failed to create window surface");
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
            LOG_ERROR("Failed to create context");
            return -1;
        }
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx))
    {
        LOG_ERROR("Failed to make current");
        return -1;
    }

    eglSurfaceAttrib(dpy, surf, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
    glDisable(GL_SCISSOR_TEST);

    display = dpy;
    surface = surf;
    context = ctx;
    g_has_window = true;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx)
    {
        if (gfx->setup_shared_window_resources)
            gfx->setup_shared_window_resources();
        if (gfx->setup_separate_window_resources)
            gfx->setup_separate_window_resources(0);
    }
    update_surface_size();

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



static int android_get_current_orientation(void)
{
    if (!g_app || !g_app->activity)
        return -1;

    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return -1;

    jobject activity = g_app->activity->clazz;
    jclass activityClass = (*env)->GetObjectClass(env, activity);

    jmethodID getResources = (*env)->GetMethodID(env, activityClass,
                                                 "getResources", "()Landroid/content/res/Resources;");
    jobject resources = (*env)->CallObjectMethod(env, activity, getResources);

    jclass resourcesClass = (*env)->GetObjectClass(env, resources);
    jmethodID getConfiguration = (*env)->GetMethodID(env, resourcesClass,
                                                     "getConfiguration", "()Landroid/content/res/Configuration;");
    jobject config = (*env)->CallObjectMethod(env, resources, getConfiguration);

    jclass configClass = (*env)->GetObjectClass(env, config);
    jfieldID orientationField = (*env)->GetFieldID(env, configClass, "orientation", "I");

    int orientation = (*env)->GetIntField(env, config, orientationField);

    (*env)->DeleteLocalRef(env, configClass);
    (*env)->DeleteLocalRef(env, config);
    (*env)->DeleteLocalRef(env, resourcesClass);
    (*env)->DeleteLocalRef(env, resources);
    (*env)->DeleteLocalRef(env, activityClass);

    detach_jni_env(attach);
    return orientation;
}

static bool android_is_orientation_locked(void)
{
    return impl_android_orient_locked_new();
}

static void android_lock_orientation(void)
{
    impl_android_orient_lock_new();
}

static void android_unlock_orientation(void)
{
    impl_android_orient_unlock_new();
}

static void android_set_orientation_portrait(void)
{
    impl_android_orient_portrait_new();
}

static void android_set_orientation_landscape(void)
{
    impl_android_orient_landscape_new();
}

static void android_set_orientation_sensor(void)
{
    impl_android_orient_sensor_new();
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
            request_frame();
        }
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        g_phys_cached = false;
        LOG_INFO("Configuration changed, invalidating physical screen cache");
        LOG_INFO("New config: %dx%d, orientation=%d", g_avail_width, g_avail_height, android_get_current_orientation());
        cache_physical_screen_info(app);
        term_display_surface_only();
        if (app->window)
            init_display(app);

        {
            extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
            for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
            {
                if (g_windows[i].root_node)
                    aroma_node_invalidate(g_windows[i].root_node);
            }
        }
        request_frame();
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
        request_frame();
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

    g_choreographer = AChoreographer_getInstance();
    if (!g_choreographer)
        LOG_WARNING("AChoreographer not available — falling back to polled rendering");

    return 1;
}

void shutdown(void)
{
    term_display();

    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (env && g_helper_cache.initialized)
    {
        if (g_helper_cache.remove_callback && g_helper_cache.callback_obj)
        {
            (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.remove_callback, g_helper_cache.callback_obj);
        }
        if (g_helper_cache.callback_obj)
        {
            (*env)->DeleteGlobalRef(env, g_helper_cache.callback_obj);
            g_helper_cache.callback_obj = NULL;
        }
        if (g_helper_cache.native_callback_class)
        {
            (*env)->DeleteGlobalRef(env, g_helper_cache.native_callback_class);
            g_helper_cache.native_callback_class = NULL;
        }
        if (g_helper_cache.helper_class)
        {
            (*env)->DeleteGlobalRef(env, g_helper_cache.helper_class);
            g_helper_cache.helper_class = NULL;
        }
        memset(&g_helper_cache, 0, sizeof(g_helper_cache));
    }
    detach_jni_env(attach);
}

size_t create_window(const char *title, int x, int y, int width, int height)
{
    return 0;
}

void make_context_current(size_t window_id)
{
    if (g_using_vulkan)
        return;

    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT)
    {
        if (eglGetCurrentContext() != context || eglGetCurrentSurface(EGL_DRAW) != surface)
        {
            if (!eglMakeCurrent(display, surface, surface, context))
            {
                EGLint error = eglGetError();
                LOG_ERROR("make_context_current failed: 0x%x", error);
            }
        }
    }
    else
    {
        LOG_ERROR("Attempted to make context current but display/surface/context is invalid");
    }
}

void set_window_update_callback(void (*callback)(size_t, void *), void *data)
{
    g_update_callback = callback;
    g_update_callback_data = data;
}

void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    (void)window_id;
    if (!g_has_window)
    {
        *window_width = 0;
        *window_height = 0;
        return;
    }

    /*
     * The native surface can include the system navigation area when the
     * activity uses an edge-to-edge window. Layouts must use the available
     * application area instead, while rendering still clears the complete
     * surface so the excluded area stays blank.
     */
    int viewport_width = 0;
    int viewport_height = 0;
    get_layout_viewport(&viewport_width, &viewport_height);

    *window_width = viewport_width;
    *window_height = viewport_height;
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

void request_window_update(size_t window_id)
{
    (void)window_id;

    g_frame_needed = true;
    request_frame();
}

bool run_event_loop(void)
{
    int events;
    struct android_poll_source *source;

    while (ALooper_pollAll(g_frame_needed ? 0 : 4, NULL, &events, (void **)&source) >= 0)
    {
        if (source)
            source->process(g_app, source);
        if (g_app->destroyRequested)
            return false;
    }

    return true;
}

void swap_buffers(size_t window_id)
{
    if (g_using_vulkan)
    {

        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->graphics_flush)
            gfx->graphics_flush();
        return;
    }

    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT)
        return;

    if (eglGetCurrentContext() != context)
    {
        if (!eglMakeCurrent(display, surface, surface, context))
        {
            LOG_ERROR("swap_buffers: failed to make context current: 0x%x", eglGetError());
            return;
        }
    }

    if (!eglSwapBuffers(display, surface))
    {
        EGLint err = eglGetError();
        if (err == EGL_BAD_SURFACE)
        {
            LOG_WARNING("swap_buffers: EGL_BAD_SURFACE, surface may have been lost");
        }
        else
        {
            LOG_ERROR("swap_buffers: eglSwapBuffers failed: 0x%x", err);
        }
    }
}

void *get_tft_context(void) { return NULL; }
void call_flush_function_ptr(void (*flush_fn)(struct AromaDrawList *, size_t, int, int, int, int), void *list) {}
void tft_mark_tiles_dirty(int y, int h) {}
void set_clear_color(uint16_t color) {}

static void android_show_keyboard(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !g_app || !g_app->activity)
        return;
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.show_ime)
    {
        detach_jni_env(attach);
        return;
    }
    jobject activity = g_app->activity->clazz;
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.show_ime, activity);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    detach_jni_env(attach);
}

static void android_hide_keyboard(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !g_app || !g_app->activity)
        return;
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.hide_ime)
    {
        detach_jni_env(attach);
        return;
    }
    jobject activity = g_app->activity->clazz;
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.hide_ime, activity);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    detach_jni_env(attach);
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

static bool impl_android_check_permission(const char *permission_name)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return false;

    jobject activity = aroma_android_get_activity();
    jclass contextClass = GetContextClass(env);
    if (!contextClass)
    {
        detach_jni_env(attach);
        return false;
    }

    jmethodID checkSelfPermission = (*env)->GetMethodID(env, contextClass, "checkSelfPermission", "(Ljava/lang/String;)I");

    bool granted = false;
    if (checkSelfPermission)
    {
        jstring perm = str_to_jstring(env, permission_name);
        jint res = (*env)->CallIntMethod(env, activity, checkSelfPermission, perm);
        granted = (res == 0);
        (*env)->DeleteLocalRef(env, perm);
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, contextClass);
    detach_jni_env(attach);
    return granted;
}

static void impl_android_request_permission(const char **permissions, int permCount)
{
    request_permissions_code(permissions, permCount, 1000);
}

static void *impl_android_get_system_service(const char *service_name)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return NULL;

    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return NULL;
    }

    jclass activityClass = (*env)->GetObjectClass(env, activity);
    jmethodID getSystemService = (*env)->GetMethodID(env, activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");

    jobject service = NULL;
    if (getSystemService)
    {
        jstring name = str_to_jstring(env, service_name);
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
    detach_jni_env(attach);
    return service;
}

static void impl_android_vibrate(int ms)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jobject vibrator = (jobject)impl_android_get_system_service("vibrator");
    if (!vibrator)
    {
        detach_jni_env(attach);
        return;
    }

    jclass vibratorClass = (*env)->GetObjectClass(env, vibrator);
    jmethodID vibrate = (*env)->GetMethodID(env, vibratorClass, "vibrate", "(J)V");

    if (vibrate)
    {
        (*env)->CallVoidMethod(env, vibrator, vibrate, (jlong)ms);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
        }
    }
    else
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, vibrator);
    (*env)->DeleteLocalRef(env, vibratorClass);
    detach_jni_env(attach);
}

static void impl_android_toast(const char *msg, bool long_duration)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env))
    {
        LOG_ERROR("AromaHelper not available for toast");
        detach_jni_env(attach);
        return;
    }

    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return;
    }

    if (g_helper_cache.show_toast)
    {
        jstring jmsg = (*env)->NewStringUTF(env, msg);
        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.show_toast, activity, jmsg, (jboolean)long_duration);
        (*env)->DeleteLocalRef(env, jmsg);
    }
    else
    {
        LOG_ERROR("showToast method not available");
    }

    detach_jni_env(attach);
}

static void impl_android_launch_camera(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jclass intentClass = (*env)->FindClass(env, "android/content/Intent");
    if (!intentClass)
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
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
    detach_jni_env(attach);
}

static bool impl_android_is_wifi_enabled(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return false;

    jobject wifiMgr = (jobject)impl_android_get_system_service("wifi");
    if (!wifiMgr)
    {
        detach_jni_env(attach);
        return false;
    }

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
    detach_jni_env(attach);
    return res;
}

static void impl_android_set_wifi_enabled(bool enabled)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jobject wifiMgr = (jobject)impl_android_get_system_service("wifi");
    if (!wifiMgr)
    {
        detach_jni_env(attach);
        return;
    }

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
    detach_jni_env(attach);
}

static bool impl_android_is_bluetooth_enabled(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return false;

    jclass adapterClass = (*env)->FindClass(env, "android/bluetooth/BluetoothAdapter");
    if (!adapterClass)
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
        return false;
    }

    jmethodID getDefaultAdapter = (*env)->GetStaticMethodID(env, adapterClass, "getDefaultAdapter", "()Landroid/bluetooth/BluetoothAdapter;");
    if (!getDefaultAdapter)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, adapterClass);
        detach_jni_env(attach);
        return false;
    }

    jobject adapter = (*env)->CallStaticObjectMethod(env, adapterClass, getDefaultAdapter);

    if (!adapter)
    {
        (*env)->DeleteLocalRef(env, adapterClass);
        detach_jni_env(attach);
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
    detach_jni_env(attach);
    return res;
}

void impl_bt_register_callbacks(
    void (*device_cb)(const char *, const char *, int, int),
    void (*scan_finished_cb)(void),
    void (*pairing_cb)(bool, const char *, const char *),
    void (*connection_cb)(bool, const char *, int, int),
    void (*data_cb)(const char *, int))
{
    g_bt_callbacks.device_discovered_cb = device_cb;
    g_bt_callbacks.scan_finished_cb = scan_finished_cb;
    g_bt_callbacks.pairing_result_cb = pairing_cb;
    g_bt_callbacks.connection_result_cb = connection_cb;
    g_bt_callbacks.data_received_cb = data_cb;

    LOG_INFO("All Bluetooth callbacks registered");
}

static int impl_android_bt_scan(int scan_mode, void (*callback)(const char *addr, const char *name, int type, int rssi))
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return 0;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_scan)
    {
        LOG_ERROR("bt_scan not available");
        detach_jni_env(attach);
        return 0;
    }

    if (callback)
    {
        g_bt_callbacks.device_discovered_cb = callback;
    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_scan, (jint)scan_mode);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
        return 0;
    }

    detach_jni_env(attach);
    return 1;
}

static void impl_android_bt_stop_scan(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_stop_scan)
    {
        LOG_ERROR("bt_stop_scan not available");
        detach_jni_env(attach);
        return;
    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_stop_scan);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    detach_jni_env(attach);
}

static int impl_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return 0;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_paired)
    {
        LOG_ERROR("bt_get_paired not available");
        detach_jni_env(attach);
        return 0;
    }

    jobjectArray arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_paired);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
        return 0;
    }

    if (!arr)
    {
        detach_jni_env(attach);
        return 0;
    }

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
    detach_jni_env(attach);
    return written;
}

static bool impl_android_bt_pair(const char *addr)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !addr)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_pair)
    {
        LOG_ERROR("bt_pair not available");
        detach_jni_env(attach);
        return false;
    }

    jstring jaddr = (*env)->NewStringUTF(env, addr);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_pair, jaddr);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }

    (*env)->DeleteLocalRef(env, jaddr);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_bt_unpair(const char *addr)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !addr)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_unpair)
    {
        LOG_ERROR("bt_unpair not available");
        detach_jni_env(attach);
        return false;
    }

    jstring jaddr = (*env)->NewStringUTF(env, addr);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_unpair, jaddr);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }

    (*env)->DeleteLocalRef(env, jaddr);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static int impl_android_bt_get_pair_state(const char *addr)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !addr)
        return 0;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_pair_state)
    {
        LOG_ERROR("bt_get_pair_state not available");
        detach_jni_env(attach);
        return 0;
    }

    jstring jaddr = (*env)->NewStringUTF(env, addr);
    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_pair_state, jaddr);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }

    (*env)->DeleteLocalRef(env, jaddr);
    detach_jni_env(attach);
    return (int)res;
}

static bool impl_android_bt_connect(const char *addr)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !addr)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_connect)
    {
        LOG_ERROR("bt_connect not available");
        detach_jni_env(attach);
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
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_bt_connect_with_mode(const char *addr, int mode)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !addr)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_connect_with_mode)
    {
        LOG_ERROR("bt_connect_with_mode not available");
        detach_jni_env(attach);
        return false;
    }

    jstring jaddr = (*env)->NewStringUTF(env, addr);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_connect_with_mode, jaddr, (jint)mode);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }

    (*env)->DeleteLocalRef(env, jaddr);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static void impl_android_bt_disconnect(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_disconnect)
    {
        LOG_ERROR("bt_disconnect not available");
        detach_jni_env(attach);
        return;
    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_disconnect);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    detach_jni_env(attach);
}

static int impl_android_bt_send(const char *data, int len)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !data || len <= 0)
        return -1;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_send)
    {
        LOG_ERROR("bt_send not available");
        detach_jni_env(attach);
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
    detach_jni_env(attach);
    return (int)written;
}

static bool impl_android_bt_is_connected(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return false;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_is_connected)
    {
        LOG_ERROR("bt_is_connected not available");
        detach_jni_env(attach);
        return false;
    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_is_connected);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }

    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static int impl_android_bt_get_device_type(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return 0;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_device_type)
    {
        LOG_ERROR("bt_get_device_type not available");
        detach_jni_env(attach);
        return 0;
    }

    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_device_type);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }

    detach_jni_env(attach);
    return (int)res;
}

static const char *impl_android_bt_get_device_name(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return NULL;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_device_name)
    {
        LOG_ERROR("bt_get_device_name not available");
        detach_jni_env(attach);
        return NULL;
    }

    jstring name = (jstring)(*env)->CallStaticObjectMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_device_name);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
        return NULL;
    }

    static char name_buf[248];
    const char *utf = (*env)->GetStringUTFChars(env, name, NULL);
    strncpy(name_buf, utf, 247);
    name_buf[247] = '\0';
    (*env)->ReleaseStringUTFChars(env, name, utf);
    (*env)->DeleteLocalRef(env, name);

    detach_jni_env(attach);
    return name_buf;
}

static int impl_android_bt_get_current_mode(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return 0;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_current_mode)
    {
        LOG_ERROR("bt_get_current_mode not available");
        detach_jni_env(attach);
        return 0;
    }

    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_current_mode);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }

    detach_jni_env(attach);
    return (int)res;
}

static const char *impl_android_bt_get_mode_name(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return NULL;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bt_get_mode_name)
    {
        LOG_ERROR("bt_get_mode_name not available");
        detach_jni_env(attach);
        return NULL;
    }

    jstring name = (jstring)(*env)->CallStaticObjectMethod(env, g_helper_cache.helper_class, g_helper_cache.bt_get_mode_name);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
        return NULL;
    }

    static char name_buf[64];
    const char *utf = (*env)->GetStringUTFChars(env, name, NULL);
    strncpy(name_buf, utf, 63);
    name_buf[63] = '\0';
    (*env)->ReleaseStringUTFChars(env, name, utf);
    (*env)->DeleteLocalRef(env, name);

    detach_jni_env(attach);
    return name_buf;
}

static int impl_android_get_battery_level(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return -1;

    jobject batteryManager = (jobject)impl_android_get_system_service("batterymanager");
    if (!batteryManager)
    {
        detach_jni_env(attach);
        return -1;
    }

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
    detach_jni_env(attach);
    return capacity;
}

static void impl_android_open_settings(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jclass intentClass = (*env)->FindClass(env, "android/content/Intent");
    if (!intentClass)
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
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
    detach_jni_env(attach);
}

static void impl_android_launch_gallery(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    jclass intentClass = (*env)->FindClass(env, "android/content/Intent");
    if (!intentClass)
    {
        (*env)->ExceptionClear(env);
        detach_jni_env(attach);
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
    detach_jni_env(attach);
}

#ifdef AROMA_HAS_VULKAN

static bool android_create_vulkan_surface(size_t window_id, void *vk_instance, void *vk_surface_out)
{
    (void)window_id;
    if (!vk_instance || !vk_surface_out)
        return false;
    if (!g_app || !g_app->window)
    {
        LOG_ERROR("Vulkan: ANativeWindow not available for surface creation");
        return false;
    }

    VkInstance instance = *(VkInstance *)vk_instance;

    VkAndroidSurfaceCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .window = g_app->window,
    };

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = vkCreateAndroidSurfaceKHR(instance, &createInfo, NULL, &surface);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: vkCreateAndroidSurfaceKHR failed (%d)", (int)result);
        return false;
    }

    *(VkSurfaceKHR *)vk_surface_out = surface;
    LOG_INFO("Vulkan: Android surface created successfully");
    return true;
}

static const char *android_vulkan_extensions[] = {
    "VK_KHR_surface",
    "VK_KHR_android_surface",
};

static const char **android_get_vulkan_instance_extensions(uint32_t *count_out)
{
    *count_out = sizeof(android_vulkan_extensions) / sizeof(android_vulkan_extensions[0]);
    return android_vulkan_extensions;
}

#endif

void impl_android_setPref(const char *key, const char *value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.set_preference)
    {
        LOG_ERROR("set_preference not available");
        detach_jni_env(attach);
        return;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    jstring jvalue = (*env)->NewStringUTF(env, value);
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.set_preference, jkey, jvalue);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, jkey);
    (*env)->DeleteLocalRef(env, jvalue);
    detach_jni_env(attach);
}

const char* impl_android_getPref(const char *key, const char* default_value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return NULL;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.get_preference)
    {
        LOG_ERROR("get_preference not available");
        detach_jni_env(attach);
        return NULL;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    jstring jdefault_val = (*env)->NewStringUTF(env, default_value);
    jstring jvalue = (jstring)(*env)->CallStaticObjectMethod(env, g_helper_cache.helper_class, g_helper_cache.get_preference, jkey, jdefault_val);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jkey);
        (*env)->DeleteLocalRef(env, jdefault_val);

        detach_jni_env(attach);
        return NULL;
    }

    static char value_buf[1024];
    const char *value = NULL;
    if (jvalue)
    {
        const char *utf = (*env)->GetStringUTFChars(env, jvalue, NULL);
        if (utf)
        {
            strncpy(value_buf, utf, sizeof(value_buf) - 1);
            value_buf[sizeof(value_buf) - 1] = '\0';
            value = value_buf;
            (*env)->ReleaseStringUTFChars(env, jvalue, utf);
        }
    }

    (*env)->DeleteLocalRef(env, jkey);
    if (jvalue)
        (*env)->DeleteLocalRef(env, jvalue);
    detach_jni_env(attach);
    return value;
}

void impl_android_setPrefInt(const char *key, int value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.set_preference_int)
    {
        LOG_ERROR("set_preference_int not available");
        detach_jni_env(attach);
        return;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.set_preference_int, jkey, (jint)value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
}

int impl_android_getPrefInt(const char *key, int default_value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return default_value;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.get_preference_int)
    {
        LOG_ERROR("get_preference_int not available");
        detach_jni_env(attach);
        return default_value;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    jint value = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.get_preference_int, jkey, (jint)default_value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        value = (jint)default_value;
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
    return (int)value;
}

void impl_android_setPrefFloat(const char* key, float value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.set_preference_float)
    {
        LOG_ERROR("set_preference_float not available");
        detach_jni_env(attach);
        return;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.set_preference_float, jkey, (jfloat)value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
}

float impl_android_getPrefFloat(const char* key, float default_value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return default_value;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.get_preference_float)
    {
        LOG_ERROR("get_preference_float not available");
        detach_jni_env(attach);
        return default_value;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    jfloat value = (*env)->CallStaticFloatMethod(env, g_helper_cache.helper_class, g_helper_cache.get_preference_float, jkey, (jfloat)default_value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        value = (jfloat)default_value;
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
    return (float)value;
}

void impl_android_setPrefBool(const char* key, bool value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.set_preference_bool)
    {
        LOG_ERROR("set_preference_bool not available");
        detach_jni_env(attach);
        return;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.set_preference_bool, jkey, (jboolean)value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
}

bool impl_android_getPrefBool(const char* key, bool default_value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return default_value;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.get_preference_bool)
    {
        LOG_ERROR("get_preference_bool not available");
        detach_jni_env(attach);
        return default_value;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    jboolean value = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.get_preference_bool, jkey, (jboolean)default_value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        value = (jboolean)default_value;
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
    return value == JNI_TRUE;
}

void impl_android_setPrefLong(const char* key, long value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.set_preference_long)
    {
        LOG_ERROR("set_preference_long not available");
        detach_jni_env(attach);
        return;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.set_preference_long, jkey, (jlong)value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
}

long impl_android_getPrefLong(const char* key, long default_value)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
        return default_value;

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.get_preference_long)
    {
        LOG_ERROR("get_preference_long not available");
        detach_jni_env(attach);
        return default_value;
    }

    jstring jkey = (*env)->NewStringUTF(env, key);
    jlong value = (*env)->CallStaticLongMethod(env, g_helper_cache.helper_class, g_helper_cache.get_preference_long, jkey, (jlong)default_value);

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        value = (jlong)default_value;
    }

    (*env)->DeleteLocalRef(env, jkey);
    detach_jni_env(attach);
    return (long)value;
}

static bool helper_string_to_buf(JNIEnv *env, jmethodID mid, char *out, size_t out_len)

{

    jstring res = (jstring)(*env)->CallStaticObjectMethod(env, g_helper_cache.helper_class, mid);

    if ((*env)->ExceptionCheck(env) || !res)

    {

        (*env)->ExceptionClear(env);

        return false;

    }

    const char *utf = (*env)->GetStringUTFChars(env, res, NULL);

    if (utf)

    {

        strncpy(out, utf, out_len - 1);

        out[out_len - 1] = '\0';

        (*env)->ReleaseStringUTFChars(env, res, utf);

    }

    else

    {

        out[0] = '\0';

    }

    (*env)->DeleteLocalRef(env, res);

    return true;

}



static void request_permissions_code(const char **permissions, int permCount, int requestCode)

{

    if (permCount <= 0 || !permissions)

    {

        return;

    }

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        return;

    }

    jclass activityClass = (*env)->GetObjectClass(env, activity);

    if (!activityClass)

    {

        detach_jni_env(attach);

        return;

    }

    jmethodID checkSelfPermission = (*env)->GetMethodID(env, activityClass, "checkSelfPermission", "(Ljava/lang/String;)I");

    jmethodID requestPermissions = (*env)->GetMethodID(env, activityClass, "requestPermissions", "([Ljava/lang/String;I)V");

    if (!checkSelfPermission || !requestPermissions)

    {

        (*env)->ExceptionClear(env);

        (*env)->DeleteLocalRef(env, activityClass);

        detach_jni_env(attach);

        return;

    }

    int toRequestCount = 0;

    int i = 0;

    for (i = 0; i < permCount; i++)

    {

        jstring permStr = (*env)->NewStringUTF(env, permissions[i] ? permissions[i] : "");

        jint granted = (*env)->CallIntMethod(env, activity, checkSelfPermission, permStr);

        (*env)->DeleteLocalRef(env, permStr);

        if (granted != 0)

        {

            toRequestCount++;

        }

    }

    if (toRequestCount == 0)
    {
        (*env)->DeleteLocalRef(env, activityClass);
        detach_jni_env(attach);
        if (g_perm_request.active && g_perm_request.request_code == requestCode && g_perm_request.result_cb)
        {
            int j = 0;
            for (j = 0; j < g_perm_request.perm_count; j++)
            {
                g_perm_request.result_cb(g_perm_request.permissions[j], true);
            }
            g_perm_request.active = false;
        }
        return;
    }
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");

    jobjectArray permArray = (*env)->NewObjectArray(env, toRequestCount, stringClass, NULL);

    int idx = 0;

    for (i = 0; i < permCount; i++)

    {

        jstring permStr = (*env)->NewStringUTF(env, permissions[i] ? permissions[i] : "");

        jint granted = (*env)->CallIntMethod(env, activity, checkSelfPermission, permStr);

        (*env)->DeleteLocalRef(env, permStr);

        if (granted != 0)

        {

            jstring arrayPermStr = (*env)->NewStringUTF(env, permissions[i] ? permissions[i] : "");

            (*env)->SetObjectArrayElement(env, permArray, idx++, arrayPermStr);

            (*env)->DeleteLocalRef(env, arrayPermStr);

        }

    }

    (*env)->CallVoidMethod(env, activity, requestPermissions, permArray, (jint)requestCode);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, permArray);

    (*env)->DeleteLocalRef(env, stringClass);

    (*env)->DeleteLocalRef(env, activityClass);

    detach_jni_env(attach);

}



void impl_android_request_permission_cb(const char **permissions, int permCount, void (*cb)(const char *permission, bool granted))

{

    if (permCount <= 0 || !permissions)

    {

        return;

    }

    if (permCount > 16)

    {

        permCount = 16;

    }

    int i = 0;

    for (i = 0; i < permCount; i++)

    {

        const char *p = permissions[i] ? permissions[i] : "";

        strncpy(g_perm_request.permissions[i], p, 127);

        g_perm_request.permissions[i][127] = '\0';

    }

    g_perm_request.perm_count = permCount;

    g_perm_request.request_code = 1001;

    g_perm_request.result_cb = cb;

    g_perm_request.active = true;

    request_permissions_code(permissions, permCount, 1001);

}



static int media_slot_for_code(int code)

{

    if (code == 2001)

    {

        return 0;

    }

    if (code == 2002)

    {

        return 1;

    }

    if (code == 2003)

    {

        return 2;

    }

    return -1;

}



static void media_start_request(int code, void (*cb)(const char *path))

{

    int slot = media_slot_for_code(code);

    if (slot < 0)

    {

        return;

    }

    g_media_requests[slot].active = true;

    g_media_requests[slot].request_code = code;

    g_media_requests[slot].result_cb = cb;

}



void impl_android_pick_image(void (*cb)(const char *path))

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.pick_image)

    {

        detach_jni_env(attach);

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    media_start_request(2001, cb);

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.pick_image, activity, (jint)2001);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



void impl_android_capture_photo(void (*cb)(const char *path))

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.capture_photo)

    {

        detach_jni_env(attach);

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    media_start_request(2002, cb);

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.capture_photo, activity, (jint)2002);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



void impl_android_open_document(const char *mime, void (*cb)(const char *path))

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.open_document)

    {

        detach_jni_env(attach);

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        if (cb)

        {

            cb(NULL);

        }

        return;

    }

    media_start_request(2003, cb);

    jstring jmime = (*env)->NewStringUTF(env, mime ? mime : "*/*");

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.open_document, activity, jmime, (jint)2003);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jmime);

    detach_jni_env(attach);

}



static void impl_android_notify_channel(int id, const char *name, const char *desc, int importance)

{

    (void)id;

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.notify_channel)

    {

        detach_jni_env(attach);

        return;

    }

    char chan[64];

    snprintf(chan, sizeof(chan), "aroma-channel-%d", id);

    jstring jid = (*env)->NewStringUTF(env, chan);

    jstring jname = (*env)->NewStringUTF(env, name ? name : chan);

    jstring jdesc = (*env)->NewStringUTF(env, desc ? desc : "");

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.notify_channel, jid, jname, jdesc, (jint)importance);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jid);

    (*env)->DeleteLocalRef(env, jname);

    (*env)->DeleteLocalRef(env, jdesc);

    detach_jni_env(attach);

}



static void impl_android_notify_show(int id, const char *channel, const char *title, const char *text)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.notify_show)

    {

        detach_jni_env(attach);

        return;

    }

    char chan[64];

    if (channel && channel[0])

    {

        strncpy(chan, channel, sizeof(chan) - 1);

        chan[sizeof(chan) - 1] = '\0';

    }

    else

    {

        snprintf(chan, sizeof(chan), "aroma-channel-%d", id);

        impl_android_notify_channel(id, chan, "", 3);

    }

    jstring jchan = (*env)->NewStringUTF(env, chan);

    jstring jtitle = (*env)->NewStringUTF(env, title ? title : "");

    jstring jtext = (*env)->NewStringUTF(env, text ? text : "");

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.notify_show, (jint)id, jchan, jtitle, jtext);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jchan);

    (*env)->DeleteLocalRef(env, jtitle);

    (*env)->DeleteLocalRef(env, jtext);

    detach_jni_env(attach);

}



static void impl_android_notify_cancel(int id)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.notify_cancel)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.notify_cancel, (jint)id);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_notify_cancel_all(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.notify_cancel_all)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.notify_cancel_all);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static bool impl_android_notify_enabled(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.notify_enabled)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.notify_enabled);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static void impl_android_vibrate_effect(int ms, int amplitude)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.vibrate_effect)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.vibrate_effect, (jlong)ms, (jint)amplitude);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_set_clipboard_text(const char *text)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.clip_set)

    {

        detach_jni_env(attach);

        return;

    }

    jstring jtext = (*env)->NewStringUTF(env, text ? text : "");

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.clip_set, jtext);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jtext);

    detach_jni_env(attach);

}



static const char *impl_android_get_clipboard_text(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return NULL;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.clip_get)

    {

        detach_jni_env(attach);

        return NULL;

    }

    static char buf[4096];

    if (!helper_string_to_buf(env, g_helper_cache.clip_get, buf, sizeof(buf)))

    {

        detach_jni_env(attach);

        return NULL;

    }

    detach_jni_env(attach);

    return buf;

}



static void impl_android_share_text(const char *text, const char *title)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.share_text)

    {

        detach_jni_env(attach);

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        return;

    }

    jstring jtext = (*env)->NewStringUTF(env, text ? text : "");

    jstring jtitle = (*env)->NewStringUTF(env, title ? title : "");

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.share_text, activity, jtext, jtitle);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jtext);

    (*env)->DeleteLocalRef(env, jtitle);

    detach_jni_env(attach);

}



static void impl_android_set_immersive(bool enabled)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.ui_immersive)

    {

        detach_jni_env(attach);

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.ui_immersive, activity, (jboolean)(enabled ? JNI_TRUE : JNI_FALSE));

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_set_keep_screen_on(bool enabled)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.ui_keep_screen_on)

    {

        detach_jni_env(attach);

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.ui_keep_screen_on, activity, (jboolean)(enabled ? JNI_TRUE : JNI_FALSE));

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_orient_call(int which, int mode)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    jmethodID mid = NULL;

    if (which == 0)

    {

        mid = g_helper_cache.orient_lock;

    }

    else if (which == 1)

    {

        mid = g_helper_cache.orient_set;

    }

    else if (which == 2)

    {

        mid = g_helper_cache.orient_unlock;

    }

    if (!ensure_aroma_helper_initialized(env) || !mid)

    {

        detach_jni_env(attach);

        return;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        return;

    }

    if (which == 1)

    {

        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, mid, activity, (jint)mode);

    }

    else

    {

        (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, mid, activity);

    }

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_orient_lock_new(void)

{

    impl_android_orient_call(0, 0);

}



static void impl_android_orient_unlock_new(void)

{

    impl_android_orient_call(2, 0);

}



static void impl_android_orient_portrait_new(void)

{

    impl_android_orient_call(1, 1);

}



static void impl_android_orient_landscape_new(void)

{

    impl_android_orient_call(1, 0);

}



static void impl_android_orient_sensor_new(void)

{

    impl_android_orient_call(1, 2);

}



static bool impl_android_orient_locked_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.orient_locked)

    {

        detach_jni_env(attach);

        return false;

    }

    jobject activity = aroma_android_get_activity();

    if (!activity)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.orient_locked, activity);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



void impl_android_sensor_register_callbacks(void (*cb)(int, float, float, float, long long))

{

    g_sensor_callbacks.sensor_cb = cb;

}



static void impl_android_sensor_start_new(int type, int rate_us, void (*cb)(int, float, float, float, long long))

{

    if (cb)

    {

        g_sensor_callbacks.sensor_cb = cb;

    }

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.sensor_start)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.sensor_start, (jint)type, (jint)rate_us);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_sensor_stop_new(int type)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.sensor_stop)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.sensor_stop, (jint)type);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static bool impl_android_sensor_available_new(int type)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.sensor_available)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.sensor_available, (jint)type);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static void impl_android_tts_speak_new(const char *text)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.tts_speak)

    {

        detach_jni_env(attach);

        return;

    }

    jstring jtext = (*env)->NewStringUTF(env, text ? text : "");

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.tts_speak, jtext);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jtext);

    detach_jni_env(attach);

}



static void impl_android_tts_stop_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.tts_stop)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.tts_stop);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static bool impl_android_tts_is_speaking_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.tts_is_speaking)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.tts_is_speaking);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static const char *impl_android_simple_string_new(int which)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return NULL;

    }

    jmethodID mid = NULL;

    static char bufs[12][256];

    int idx = 0;

    if (which == 0)

    {

        mid = g_helper_cache.dev_manufacturer;

        idx = 0;

    }

    else if (which == 1)

    {

        mid = g_helper_cache.dev_model;

        idx = 1;

    }

    else if (which == 2)

    {

        mid = g_helper_cache.dev_os_version;

        idx = 2;

    }

    else if (which == 3)

    {

        mid = g_helper_cache.app_package;

        idx = 3;

    }

    else if (which == 4)

    {

        mid = g_helper_cache.app_version_name;

        idx = 4;

    }

    else if (which == 5)

    {

        mid = g_helper_cache.locale_tag;

        idx = 5;

    }

    else if (which == 6)

    {

        mid = g_helper_cache.timezone_id;

        idx = 6;

    }

    else if (which == 7)

    {

        mid = g_helper_cache.net_operator;

        idx = 7;

    }

    else if (which == 8)

    {

        mid = g_helper_cache.clip_get;

        idx = 8;

    }

    if (!ensure_aroma_helper_initialized(env) || !mid)

    {

        detach_jni_env(attach);

        return NULL;

    }

    if (!helper_string_to_buf(env, mid, bufs[idx], sizeof(bufs[idx])))

    {

        detach_jni_env(attach);

        return NULL;

    }

    detach_jni_env(attach);

    return bufs[idx];

}



static long impl_android_simple_long_new(int which)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return 0;

    }

    jmethodID mid = NULL;

    if (which == 0)

    {

        mid = g_helper_cache.app_version_code;

    }

    else if (which == 1)

    {

        mid = g_helper_cache.app_install_time;

    }

    else if (which == 2)

    {

        mid = g_helper_cache.mem_avail_mb;

    }

    if (!ensure_aroma_helper_initialized(env) || !mid)

    {

        detach_jni_env(attach);

        return 0;

    }

    jlong res = (*env)->CallStaticLongMethod(env, g_helper_cache.helper_class, mid);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = 0;

    }

    detach_jni_env(attach);

    return (long)res;

}



static int impl_android_simple_int_new(int which)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return 0;

    }

    jmethodID mid = NULL;

    if (which == 0)

    {

        mid = g_helper_cache.dev_sdk_int;

    }

    else if (which == 1)

    {

        mid = g_helper_cache.net_type;

    }

    if (!ensure_aroma_helper_initialized(env) || !mid)

    {

        detach_jni_env(attach);

        return 0;

    }

    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, mid);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = 0;

    }

    detach_jni_env(attach);

    return (int)res;

}



static bool impl_android_simple_bool_new(int which)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    jmethodID mid = NULL;

    if (which == 0)

    {

        mid = g_helper_cache.mem_low;

    }

    else if (which == 1)

    {

        mid = g_helper_cache.net_connected;

    }

    else if (which == 2)

    {

        mid = g_helper_cache.batt_charging;

    }

    else if (which == 3)

    {

        mid = g_helper_cache.dev_interactive;

    }

    else if (which == 4)

    {

        mid = g_helper_cache.notify_enabled;

    }

    if (!ensure_aroma_helper_initialized(env) || !mid)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, mid);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static void impl_android_wakelock_acquire_new(long timeout_ms)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.wake_acquire)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.wake_acquire, (jlong)timeout_ms);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_wakelock_release_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.wake_release)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.wake_release);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static bool impl_android_torch_set_new(bool enabled)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.torch_set)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.torch_set, (jboolean)(enabled ? JNI_TRUE : JNI_FALSE));

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



void impl_android_btle_register_callbacks(

    void (*device_cb)(const char *, const char *, int),

    void (*scan_finished_cb)(void),

    void (*connection_cb)(const char *, int, bool),

    void (*services_cb)(const char *, const char *),

    void (*data_cb)(const char *, const char *, const char *, int),

    void (*write_cb)(const char *, const char *, int))

{

    g_ble_callbacks.device_cb = device_cb;

    g_ble_callbacks.scan_finished_cb = scan_finished_cb;

    g_ble_callbacks.connection_cb = connection_cb;

    g_ble_callbacks.services_cb = services_cb;

    g_ble_callbacks.data_cb = data_cb;

    g_ble_callbacks.write_cb = write_cb;

}



static void impl_android_btle_scan_new(int timeout_ms, const char *service_uuids)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.btle_start_scan)

    {

        detach_jni_env(attach);

        return;

    }

    jstring juuids = NULL;

    if (service_uuids && service_uuids[0])

    {

        juuids = (*env)->NewStringUTF(env, service_uuids);

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.btle_start_scan, juuids, (jint)timeout_ms);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    if (juuids)

    {

        (*env)->DeleteLocalRef(env, juuids);

    }

    detach_jni_env(attach);

}



static void impl_android_btle_stop_scan_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.btle_stop_scan)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.btle_stop_scan);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static void impl_android_btle_connect_new(const char *addr)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env || !addr)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.btle_connect)

    {

        detach_jni_env(attach);

        return;

    }

    jstring jaddr = (*env)->NewStringUTF(env, addr);

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.btle_connect, jaddr);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    (*env)->DeleteLocalRef(env, jaddr);

    detach_jni_env(attach);

}



static void impl_android_btle_disconnect_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.btle_disconnect)

    {

        detach_jni_env(attach);

        return;

    }

    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.btle_disconnect);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

    }

    detach_jni_env(attach);

}



static bool impl_android_btle_is_connected_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.btle_is_connected)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.btle_is_connected);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static bool impl_android_btle_discover_new(void)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env)

    {

        return false;

    }

    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.btle_discover)

    {

        detach_jni_env(attach);

        return false;

    }

    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.btle_discover);

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static bool impl_android_btle_gatt_op_new(int op, const char *svc, const char *chr_uuid, const char *data, int len, int arg)

{

    int attach = 0;

    JNIEnv *env = get_jni_env(&attach);

    if (!env || !svc || !chr_uuid)

    {

        return false;

    }

    jmethodID mid = NULL;

    if (op == 0)

    {

        mid = g_helper_cache.btle_read;

    }

    else if (op == 1)

    {

        mid = g_helper_cache.btle_write;

    }

    else

    {

        mid = g_helper_cache.btle_notify;

    }

    if (!ensure_aroma_helper_initialized(env) || !mid)

    {

        detach_jni_env(attach);

        return false;

    }

    jstring jsvc = (*env)->NewStringUTF(env, svc);

    jstring jchr = (*env)->NewStringUTF(env, chr_uuid);

    jboolean res = JNI_FALSE;

    if (op == 0)

    {

        res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, mid, jsvc, jchr);

    }

    else if (op == 1)

    {

        jbyteArray arr = (*env)->NewByteArray(env, len > 0 ? len : 0);

        if (len > 0 && data)

        {

            (*env)->SetByteArrayRegion(env, arr, 0, len, (const jbyte *)data);

        }

        res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, mid, jsvc, jchr, arr, (jint)arg);

        (*env)->DeleteLocalRef(env, arr);

    }

    else

    {

        res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, mid, jsvc, jchr, (jboolean)(arg ? JNI_TRUE : JNI_FALSE));

    }

    if ((*env)->ExceptionCheck(env))

    {

        (*env)->ExceptionClear(env);

        res = JNI_FALSE;

    }

    (*env)->DeleteLocalRef(env, jsvc);

    (*env)->DeleteLocalRef(env, jchr);

    detach_jni_env(attach);

    return res == JNI_TRUE;

}



static bool impl_android_btle_read_new(const char *svc, const char *chr_uuid)

{

    return impl_android_btle_gatt_op_new(0, svc, chr_uuid, NULL, 0, 0);

}



static bool impl_android_btle_write_new(const char *svc, const char *chr_uuid, const char *data, int len, int write_type)

{

    return impl_android_btle_gatt_op_new(1, svc, chr_uuid, data, len, write_type);

}



static bool impl_android_btle_notify_new(const char *svc, const char *chr_uuid, bool enable)

{

    return impl_android_btle_gatt_op_new(2, svc, chr_uuid, NULL, 0, enable ? 1 : 0);

}



static const char * impl_android_get_locale_tag(void)
{
    return impl_android_simple_string_new(5);
}

static const char * impl_android_get_timezone_id(void)
{
    return impl_android_simple_string_new(6);
}

static const char * impl_android_get_manufacturer(void)
{
    return impl_android_simple_string_new(0);
}

static const char * impl_android_get_model(void)
{
    return impl_android_simple_string_new(1);
}

static const char * impl_android_get_os_version(void)
{
    return impl_android_simple_string_new(2);
}

static const char * impl_android_get_package_name(void)
{
    return impl_android_simple_string_new(3);
}

static const char * impl_android_get_version_name(void)
{
    return impl_android_simple_string_new(4);
}

static const char * impl_android_get_network_operator(void)
{
    return impl_android_simple_string_new(7);
}

static int impl_android_get_sdk_int(void)
{
    return impl_android_simple_int_new(0);
}

static int impl_android_get_network_type(void)
{
    return impl_android_simple_int_new(1);
}

static long impl_android_get_version_code(void)
{
    return impl_android_simple_long_new(0);
}

static long impl_android_get_install_time(void)
{
    return impl_android_simple_long_new(1);
}

static long impl_android_get_memory_avail_mb(void)
{
    return impl_android_simple_long_new(2);
}

static bool impl_android_is_memory_low(void)
{
    return impl_android_simple_bool_new(0);
}

static bool impl_android_is_network_connected(void)
{
    return impl_android_simple_bool_new(1);
}

static bool impl_android_is_charging(void)
{
    return impl_android_simple_bool_new(2);
}

static bool impl_android_is_interactive(void)
{
    return impl_android_simple_bool_new(3);
}

typedef struct
{
    void (*tag_cb)(const char *payload);
} AromaNfcCallbacks;

typedef struct
{
    void (*uid_cb)(const char *uid_hex);
} AromaRfidCallbacks;

typedef struct
{
    void (*auth_cb)(bool success);
} AromaBiometricCallbacks;

typedef struct
{
    void (*update_cb)(double lat, double lon, float accuracy, long long time_ms);
} AromaLocationCallbacks;

static AromaNfcCallbacks g_nfc_callbacks = {0};
static AromaRfidCallbacks g_rfid_callbacks = {0};
static AromaBiometricCallbacks g_bio_callbacks = {0};
static AromaLocationCallbacks g_loc_callbacks = {0};

static void JNICALL native_on_nfc_tag(JNIEnv *env, jobject thiz, jstring payload)
{
    (void)thiz;
    const char *text = payload ? (*env)->GetStringUTFChars(env, payload, NULL) : "";
    if (g_nfc_callbacks.tag_cb)
    {
        g_nfc_callbacks.tag_cb(text);
    }
    if (payload)
    {
        (*env)->ReleaseStringUTFChars(env, payload, text);
    }
}

static void JNICALL native_on_rfid_tag(JNIEnv *env, jobject thiz, jstring uid)
{
    (void)thiz;
    const char *text = uid ? (*env)->GetStringUTFChars(env, uid, NULL) : "";
    if (g_rfid_callbacks.uid_cb)
    {
        g_rfid_callbacks.uid_cb(text);
    }
    if (uid)
    {
        (*env)->ReleaseStringUTFChars(env, uid, text);
    }
}

static void JNICALL native_on_biometric(JNIEnv *env, jobject thiz, jboolean success)
{
    (void)env;
    (void)thiz;
    if (g_bio_callbacks.auth_cb)
    {
        g_bio_callbacks.auth_cb(success == JNI_TRUE);
    }
}

static void JNICALL native_on_location(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon, jfloat accuracy, jlong timeMs)
{
    (void)env;
    (void)thiz;
    if (g_loc_callbacks.update_cb)
    {
        g_loc_callbacks.update_cb((double)lat, (double)lon, (float)accuracy, (long long)timeMs);
    }
}

static void JNICALL native_on_screenshot(JNIEnv *env, jobject thiz, jstring path)
{
    (void)thiz;
    int i = 0;
    for (i = 0; i < 5; i++)
    {
        if (g_media_requests[i].active && g_media_requests[i].request_code == 2004)
        {
            g_media_requests[i].active = false;
            if (g_media_requests[i].result_cb)
            {
                if (path)
                {
                    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
                    g_media_requests[i].result_cb(p);
                    (*env)->ReleaseStringUTFChars(env, path, p);
                }
                else
                {
                    g_media_requests[i].result_cb(NULL);
                }
            }
            return;
        }
    }
}

void impl_android_nfc_register(void (*cb)(const char *payload))
{
    g_nfc_callbacks.tag_cb = cb;
}

void impl_android_rfid_register(void (*cb)(const char *uid_hex))
{
    g_rfid_callbacks.uid_cb = cb;
}

void impl_android_biometric_register(void (*cb)(bool success))
{
    g_bio_callbacks.auth_cb = cb;
}

void impl_android_location_register(void (*cb)(double lat, double lon, float accuracy, long long time_ms))
{
    g_loc_callbacks.update_cb = cb;
}

static void impl_android_nfc_start_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.nfc_start)
    {
        detach_jni_env(attach);
        return;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return;
    }
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.nfc_start, activity);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    detach_jni_env(attach);
}

static void impl_android_nfc_stop_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.nfc_stop)
    {
        detach_jni_env(attach);
        return;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return;
    }
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.nfc_stop, activity);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    detach_jni_env(attach);
}

static bool impl_android_nfc_available_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.nfc_available)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.nfc_available);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_nfc_enabled_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.nfc_enabled)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.nfc_enabled);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static int impl_android_bio_available_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return 1;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bio_available)
    {
        detach_jni_env(attach);
        return 1;
    }
    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.bio_available);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 1;
    }
    detach_jni_env(attach);
    return (int)res;
}

static void impl_android_bio_authenticate_new(const char *title, const char *subtitle)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.bio_auth)
    {
        detach_jni_env(attach);
        return;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return;
    }
    jstring jt = (*env)->NewStringUTF(env, title ? title : "");
    jstring js = (*env)->NewStringUTF(env, subtitle ? subtitle : "");
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.bio_auth, activity, jt, js);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, jt);
    (*env)->DeleteLocalRef(env, js);
    detach_jni_env(attach);
}

static bool impl_android_loc_available_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.loc_available)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.loc_available);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_loc_start_new(long min_time_ms, float min_dist_m)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.loc_start)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.loc_start, (jlong)min_time_ms, (jfloat)min_dist_m);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static void impl_android_loc_stop_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.loc_stop)
    {
        detach_jni_env(attach);
        return;
    }
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.loc_stop);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    detach_jni_env(attach);
}

static bool impl_android_rec_start_new(const char *path)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !path)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.rec_start)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jp = (*env)->NewStringUTF(env, path);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.rec_start, jp);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jp);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_rec_stop_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.rec_stop)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.rec_stop);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_sc_add_new(const char *id, const char *short_label, const char *long_label)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !id)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.sc_add)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jid = (*env)->NewStringUTF(env, id);
    jstring js = (*env)->NewStringUTF(env, short_label ? short_label : id);
    jstring jl = (*env)->NewStringUTF(env, long_label ? long_label : (short_label ? short_label : id));
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.sc_add, jid, js, jl);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jid);
    (*env)->DeleteLocalRef(env, js);
    (*env)->DeleteLocalRef(env, jl);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_sc_remove_new(const char *id)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !id)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.sc_remove)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jid = (*env)->NewStringUTF(env, id);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.sc_remove, jid);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jid);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static int impl_android_sc_count_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return 0;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.sc_count)
    {
        detach_jni_env(attach);
        return 0;
    }
    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, g_helper_cache.sc_count);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }
    detach_jni_env(attach);
    return (int)res;
}

static bool impl_android_pip_enter_new(int w, int h)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.pip_enter)
    {
        detach_jni_env(attach);
        return false;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.pip_enter, activity, (jint)w, (jint)h);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_pip_available_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.pip_available)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.pip_available);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_wp_set_image_new(const char *path)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !path)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.wp_set_image)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jp = (*env)->NewStringUTF(env, path);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.wp_set_image, jp);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jp);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static int impl_android_vol_get_new(int which)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return 0;
    }
    jmethodID mid = NULL;
    if (which == 0)
    {
        mid = g_helper_cache.vol_music_get;
    }
    else if (which == 1)
    {
        mid = g_helper_cache.vol_music_max;
    }
    else
    {
        mid = g_helper_cache.ringer_get;
    }
    if (!ensure_aroma_helper_initialized(env) || !mid)
    {
        detach_jni_env(attach);
        return 0;
    }
    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, mid);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }
    detach_jni_env(attach);
    return (int)res;
}

static void impl_android_vol_set_new(int level)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.vol_music_set)
    {
        detach_jni_env(attach);
        return;
    }
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.vol_music_set, (jint)level);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    detach_jni_env(attach);
}

static long impl_android_storage_mb_new(int which)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return 0;
    }
    jmethodID mid = which == 0 ? g_helper_cache.storage_free_mb : g_helper_cache.storage_total_mb;
    if (!ensure_aroma_helper_initialized(env) || !mid)
    {
        detach_jni_env(attach);
        return 0;
    }
    jlong res = (*env)->CallStaticLongMethod(env, g_helper_cache.helper_class, mid);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }
    detach_jni_env(attach);
    return (long)res;
}

static int impl_android_sysbar_px_new(int which)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return 0;
    }
    jmethodID mid = which == 0 ? g_helper_cache.sysbar_status_px : g_helper_cache.sysbar_nav_px;
    if (!ensure_aroma_helper_initialized(env) || !mid)
    {
        detach_jni_env(attach);
        return 0;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return 0;
    }
    jint res = (*env)->CallStaticIntMethod(env, g_helper_cache.helper_class, mid, activity);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = 0;
    }
    detach_jni_env(attach);
    return (int)res;
}

static bool impl_android_kb_visible_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.kb_visible)
    {
        detach_jni_env(attach);
        return false;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.kb_visible, activity);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_app_installed_new(const char *pkg)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !pkg)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.app_installed)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jp = (*env)->NewStringUTF(env, pkg);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.app_installed, jp);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jp);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_app_open_new(const char *pkg)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !pkg)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.app_open)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jp = (*env)->NewStringUTF(env, pkg);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.app_open, jp);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jp);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

void impl_android_contacts_pick(void (*cb)(const char *info))
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.contacts_pick)
    {
        detach_jni_env(attach);
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    int i = 0;
    for (i = 0; i < 5; i++)
    {
        if (!g_media_requests[i].active)
        {
            break;
        }
    }
    if (i >= 5)
    {
        detach_jni_env(attach);
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    g_media_requests[i].active = true;
    g_media_requests[i].request_code = 2005;
    g_media_requests[i].result_cb = cb;
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.contacts_pick, activity, (jint)2005);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    detach_jni_env(attach);
}

void impl_android_shot_capture(void (*cb)(const char *path))
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.shot_capture)
    {
        detach_jni_env(attach);
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        detach_jni_env(attach);
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    int i = 0;
    for (i = 0; i < 5; i++)
    {
        if (!g_media_requests[i].active)
        {
            break;
        }
    }
    if (i >= 5)
    {
        detach_jni_env(attach);
        if (cb)
        {
            cb(NULL);
        }
        return;
    }
    g_media_requests[i].active = true;
    g_media_requests[i].request_code = 2004;
    g_media_requests[i].result_cb = cb;
    (*env)->CallStaticVoidMethod(env, g_helper_cache.helper_class, g_helper_cache.shot_capture, activity);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    detach_jni_env(attach);
}

static bool impl_android_ir_available_new(void)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.ir_available)
    {
        detach_jni_env(attach);
        return false;
    }
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.ir_available);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_ir_transmit_new(int freq_hz, const int *pattern, int pattern_len)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !pattern || pattern_len <= 0)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.ir_transmit)
    {
        detach_jni_env(attach);
        return false;
    }
    jintArray arr = (*env)->NewIntArray(env, pattern_len);
    if (!arr)
    {
        detach_jni_env(attach);
        return false;
    }
    (*env)->SetIntArrayRegion(env, arr, 0, pattern_len, (const jint *)pattern);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.ir_transmit, (jint)freq_hz, arr);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, arr);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static int impl_android_vol_music_get_new(void)
{
    return impl_android_vol_get_new(0);
}

static int impl_android_vol_music_max_new(void)
{
    return impl_android_vol_get_new(1);
}

static int impl_android_ringer_get_new(void)
{
    return impl_android_vol_get_new(2);
}

static bool impl_android_http_fetch_new(const char *url, const char *dest_path)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !url || !dest_path)
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.http_download)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jurl = (*env)->NewStringUTF(env, url);
    jstring jdest = (*env)->NewStringUTF(env, dest_path);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.http_download, jurl, jdest);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jurl);
    (*env)->DeleteLocalRef(env, jdest);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static bool impl_android_open_url_new(const char *url)
{
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env || !url || !url[0])
    {
        return false;
    }
    if (!ensure_aroma_helper_initialized(env) || !g_helper_cache.open_url)
    {
        detach_jni_env(attach);
        return false;
    }
    jstring jurl = (*env)->NewStringUTF(env, url);
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_helper_cache.helper_class, g_helper_cache.open_url, jurl);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        res = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jurl);
    detach_jni_env(attach);
    return res == JNI_TRUE;
}

static long impl_android_storage_free_mb_new(void)
{
    return impl_android_storage_mb_new(0);
}

static long impl_android_storage_total_mb_new(void)
{
    return impl_android_storage_mb_new(1);
}

static int impl_android_sysbar_status_new(void)
{
    return impl_android_sysbar_px_new(0);
}

static int impl_android_sysbar_nav_new(void)
{
    return impl_android_sysbar_px_new(1);
}

static jobject g_asset_manager_obj = NULL;
static AAssetManager *g_asset_manager = NULL;

static bool ensure_asset_manager(JNIEnv *env)
{
    if (g_asset_manager)
    {
        return true;
    }
    jobject activity = aroma_android_get_activity();
    if (!activity)
    {
        return false;
    }
    jclass activityClass = (*env)->GetObjectClass(env, activity);
    if (!activityClass)
    {
        return false;
    }
    jmethodID getAssets = (*env)->GetMethodID(env, activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
    if (!getAssets)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        return false;
    }
    jobject mgr = (*env)->CallObjectMethod(env, activity, getAssets);
    if ((*env)->ExceptionCheck(env) || !mgr)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, activityClass);
        return false;
    }
    g_asset_manager_obj = (*env)->NewGlobalRef(env, mgr);
    (*env)->DeleteLocalRef(env, mgr);
    (*env)->DeleteLocalRef(env, activityClass);
    if (!g_asset_manager_obj)
    {
        return false;
    }
    g_asset_manager = AAssetManager_fromJava(env, g_asset_manager_obj);
    return g_asset_manager != NULL;
}

static bool impl_android_asset_exists_new(const char *name)
{
    if (!name || !name[0])
    {
        return false;
    }
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return false;
    }
    bool found = false;
    if (ensure_asset_manager(env))
    {
        AAsset *asset = AAssetManager_open(g_asset_manager, name, AASSET_MODE_UNKNOWN);
        if (asset)
        {
            found = true;
            AAsset_close(asset);
        }
    }
    detach_jni_env(attach);
    return found;
}

static long impl_android_asset_size_new(const char *name)
{
    if (!name || !name[0])
    {
        return -1;
    }
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return -1;
    }
    long size = -1;
    if (ensure_asset_manager(env))
    {
        AAsset *asset = AAssetManager_open(g_asset_manager, name, AASSET_MODE_UNKNOWN);
        if (asset)
        {
            size = (long)AAsset_getLength(asset);
            AAsset_close(asset);
        }
    }
    detach_jni_env(attach);
    return size;
}

static long impl_android_asset_read_new(const char *name, char *out, long max_len)
{
    if (!name || !name[0] || !out || max_len <= 0)
    {
        return -1;
    }
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return -1;
    }
    long total = -1;
    if (ensure_asset_manager(env))
    {
        AAsset *asset = AAssetManager_open(g_asset_manager, name, AASSET_MODE_STREAMING);
        if (asset)
        {
            total = 0;
            int chunk = 0;
            while (total < max_len)
            {
                chunk = AAsset_read(asset, out + total, (size_t)(max_len - total));
                if (chunk <= 0)
                {
                    break;
                }
                total += chunk;
            }
            AAsset_close(asset);
        }
    }
    detach_jni_env(attach);
    return total;
}

static int asset_list_dir(const char *base, char out_names[][256], int *count, int max, int depth)
{
    if (depth > 6 || *count >= max)
    {
        return 0;
    }
    AAssetDir *dir = AAssetManager_openDir(g_asset_manager, base ? base : "");
    if (!dir)
    {
        return 0;
    }
    const char *fn = NULL;
    while (*count < max && (fn = AAssetDir_getNextFileName(dir)) != NULL)
    {
        char full[512];
        if (base && base[0])
        {
            snprintf(full, sizeof(full), "%s/%s", base, fn);
        }
        else
        {
            snprintf(full, sizeof(full), "%s", fn);
        }
        AAsset *probe = AAssetManager_open(g_asset_manager, full, AASSET_MODE_UNKNOWN);
        if (probe)
        {
            AAsset_close(probe);
            strncpy(out_names[*count], full, 255);
            out_names[*count][255] = '\0';
            (*count)++;
        }
        else
        {
            asset_list_dir(full, out_names, count, max, depth + 1);
        }
    }
    AAssetDir_close(dir);
    return 0;
}

static int impl_android_asset_list_new(const char *dir, char out_names[][256], int max)
{
    if (!out_names || max <= 0)
    {
        return 0;
    }
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return 0;
    }
    int count = 0;
    if (ensure_asset_manager(env))
    {
        asset_list_dir(dir, out_names, &count, max, 0);
    }
    detach_jni_env(attach);
    return count;
}

static bool asset_mkdir_chain(const char *path)
{
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    size_t i = 0;
    for (i = 1; tmp[i]; i++)
    {
        if (tmp[i] == '/')
        {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static const char *impl_android_asset_cache_path_new(const char *name)
{
    static char out_path[1024];
    if (!name || !name[0] || strstr(name, ".."))
    {
        return NULL;
    }
    int attach = 0;
    JNIEnv *env = get_jni_env(&attach);
    if (!env)
    {
        return NULL;
    }
    const char *result = NULL;
    if (ensure_asset_manager(env))
    {
        AAsset *asset = AAssetManager_open(g_asset_manager, name, AASSET_MODE_STREAMING);
        if (asset)
        {
            jobject activity = aroma_android_get_activity();
            if (activity)
            {
                jclass activityClass = (*env)->GetObjectClass(env, activity);
                jmethodID getCacheDir = (*env)->GetMethodID(env, activityClass, "getCacheDir", "()Ljava/io/File;");
                if (getCacheDir)
                {
                    jobject cacheDir = (*env)->CallObjectMethod(env, activity, getCacheDir);
                    if (cacheDir && !(*env)->ExceptionCheck(env))
                    {
                        jclass fileClass = (*env)->GetObjectClass(env, cacheDir);
                        jmethodID getPath = (*env)->GetMethodID(env, fileClass, "getAbsolutePath", "()Ljava/lang/String;");
                        if (getPath)
                        {
                            jstring jpath = (jstring)(*env)->CallObjectMethod(env, cacheDir, getPath);
                            if (jpath && !(*env)->ExceptionCheck(env))
                            {
                                const char *cdir = (*env)->GetStringUTFChars(env, jpath, NULL);
                                if (cdir)
                                {
                                    snprintf(out_path, sizeof(out_path), "%s/aroma_assets/%s", cdir, name);
                                    out_path[sizeof(out_path) - 1] = '\0';
                                    char parent[1024];
                                    strncpy(parent, out_path, sizeof(parent) - 1);
                                    parent[sizeof(parent) - 1] = '\0';
                                    char *slash = strrchr(parent, '/');
                                    if (slash)
                                    {
                                        *slash = '\0';
                                        asset_mkdir_chain(parent);
                                    }
                                    FILE *fp = fopen(out_path, "wb");
                                    if (fp)
                                    {
                                        char buf[8192];
                                        int n = 0;
                                        bool ok = true;
                                        while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0)
                                        {
                                            if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n)
                                            {
                                                ok = false;
                                                break;
                                            }
                                        }
                                        fclose(fp);
                                        if (ok)
                                        {
                                            result = out_path;
                                        }
                                    }
                                    (*env)->ReleaseStringUTFChars(env, jpath, cdir);
                                }
                                (*env)->DeleteLocalRef(env, jpath);
                            }
                            else
                            {
                                (*env)->ExceptionClear(env);
                            }
                        }
                        else
                        {
                            (*env)->ExceptionClear(env);
                        }
                        (*env)->DeleteLocalRef(env, cacheDir);
                    }
                    else
                    {
                        (*env)->ExceptionClear(env);
                    }
                }
                else
                {
                    (*env)->ExceptionClear(env);
                }
                (*env)->DeleteLocalRef(env, activityClass);
            }
            AAsset_close(asset);
        }
    }
    detach_jni_env(attach);
    return result;
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
    .android_bt_scan = impl_android_bt_scan,
    .android_bt_stop_scan = impl_android_bt_stop_scan,
    .android_bt_get_paired = impl_android_bt_get_paired,
    .android_bt_pair = impl_android_bt_pair,
    .android_bt_unpair = impl_android_bt_unpair,
    .android_bt_get_pair_state = impl_android_bt_get_pair_state,
    .android_bt_connect = impl_android_bt_connect,
    .android_bt_connect_with_mode = impl_android_bt_connect_with_mode,
    .android_bt_disconnect = impl_android_bt_disconnect,
    .android_bt_send = impl_android_bt_send,
    .android_bt_is_connected = impl_android_bt_is_connected,
    .android_bt_get_device_type = impl_android_bt_get_device_type,
    .android_bt_get_device_name = impl_android_bt_get_device_name,
    .android_bt_get_current_mode = impl_android_bt_get_current_mode,
    .android_bt_get_mode_name = impl_android_bt_get_mode_name,
    .android_bt_register_callbacks = impl_bt_register_callbacks,
    .android_launch_camera = impl_android_launch_camera,
    .android_launch_gallery = impl_android_launch_gallery,
    .android_get_system_service = impl_android_get_system_service,
    .android_get_internal_path = impl_android_get_internal_path,
    .android_get_external_path = impl_android_get_external_path,
    .android_get_density = android_get_density,
    .android_get_density_dpi = android_get_density_dpi,
    .android_get_scaled_density = android_get_scaled_density,
    .android_dp_to_px = android_dp_to_px,
    .android_px_to_dp = android_px_to_dp,
    .android_sp_to_px = android_sp_to_px,
    .android_px_to_sp = android_px_to_sp,
    .android_dp_to_px_f = android_dp_to_px_f,
    .android_sp_to_px_f = android_sp_to_px_f,
    .android_px_to_dp_f = android_px_to_dp_f,
    .android_get_available_size_dp = android_get_available_size_dp,
    .android_get_screen_size_inches = android_get_screen_size_inches,
    .android_get_screen_diagonal_inches = android_get_screen_diagonal_inches,
    .android_get_screen_size_category = android_get_screen_size_category,
    .android_get_xdpi = android_get_xdpi,
    .android_get_ydpi = android_get_ydpi,
    .android_lock_orientation = android_lock_orientation,
    .android_unlock_orientation = android_unlock_orientation,
    .android_set_orientation_portrait = android_set_orientation_portrait,
    .android_set_orientation_landscape = android_set_orientation_landscape,
    .android_set_orientation_sensor = android_set_orientation_sensor,
    .android_get_current_orientation = android_get_current_orientation,
    .android_is_orientation_locked = android_is_orientation_locked,
#ifdef AROMA_HAS_VULKAN
    .create_vulkan_surface = android_create_vulkan_surface,
    .get_vulkan_instance_extensions = android_get_vulkan_instance_extensions,
#else
    .create_vulkan_surface = NULL,
    .get_vulkan_instance_extensions = NULL,
#endif
    .android_get_preference_string = impl_android_getPref,
    .android_set_preference_string = impl_android_setPref,
    .android_get_preference_int = impl_android_getPrefInt,
    .android_set_preference_int = impl_android_setPrefInt,
    .android_get_preference_float = impl_android_getPrefFloat,
    .android_set_preference_float = impl_android_setPrefFloat,
    .android_get_preference_bool = impl_android_getPrefBool,
    .android_set_preference_bool = impl_android_setPrefBool,
    .android_get_preference_long = impl_android_getPrefLong,
    .android_set_preference_long = impl_android_setPrefLong,
    .android_request_permission_cb = impl_android_request_permission_cb,
    .android_pick_image = impl_android_pick_image,
    .android_capture_photo = impl_android_capture_photo,
    .android_open_document = impl_android_open_document,
    .android_notify_channel = impl_android_notify_channel,
    .android_notify_show = impl_android_notify_show,
    .android_notify_cancel = impl_android_notify_cancel,
    .android_notify_cancel_all = impl_android_notify_cancel_all,
    .android_notify_enabled = impl_android_notify_enabled,
    .android_vibrate_effect = impl_android_vibrate_effect,
    .android_set_clipboard_text = impl_android_set_clipboard_text,
    .android_get_clipboard_text = impl_android_get_clipboard_text,
    .android_share_text = impl_android_share_text,
    .android_set_immersive = impl_android_set_immersive,
    .android_set_keep_screen_on = impl_android_set_keep_screen_on,
    .android_get_locale_tag = impl_android_get_locale_tag,
    .android_get_timezone_id = impl_android_get_timezone_id,
    .android_get_manufacturer = impl_android_get_manufacturer,
    .android_get_model = impl_android_get_model,
    .android_get_os_version = impl_android_get_os_version,
    .android_get_sdk_int = impl_android_get_sdk_int,
    .android_get_package_name = impl_android_get_package_name,
    .android_get_version_name = impl_android_get_version_name,
    .android_get_version_code = impl_android_get_version_code,
    .android_get_install_time = impl_android_get_install_time,
    .android_get_memory_avail_mb = impl_android_get_memory_avail_mb,
    .android_is_memory_low = impl_android_is_memory_low,
    .android_is_network_connected = impl_android_is_network_connected,
    .android_get_network_type = impl_android_get_network_type,
    .android_is_charging = impl_android_is_charging,
    .android_is_interactive = impl_android_is_interactive,
    .android_get_network_operator = impl_android_get_network_operator,
    .android_wakelock_acquire = impl_android_wakelock_acquire_new,
    .android_wakelock_release = impl_android_wakelock_release_new,
    .android_set_torch_enabled = impl_android_torch_set_new,
    .android_tts_speak = impl_android_tts_speak_new,
    .android_tts_stop = impl_android_tts_stop_new,
    .android_tts_is_speaking = impl_android_tts_is_speaking_new,
    .android_sensor_register_callbacks = impl_android_sensor_register_callbacks,
    .android_sensor_start = impl_android_sensor_start_new,
    .android_sensor_stop = impl_android_sensor_stop_new,
    .android_sensor_available = impl_android_sensor_available_new,
    .android_btle_scan = impl_android_btle_scan_new,
    .android_btle_stop_scan = impl_android_btle_stop_scan_new,
    .android_btle_register_callbacks = impl_android_btle_register_callbacks,
    .android_btle_connect = impl_android_btle_connect_new,
    .android_btle_disconnect = impl_android_btle_disconnect_new,
    .android_btle_is_connected = impl_android_btle_is_connected_new,
    .android_btle_discover = impl_android_btle_discover_new,
    .android_btle_read = impl_android_btle_read_new,
    .android_btle_write = impl_android_btle_write_new,
    .android_btle_notify = impl_android_btle_notify_new,
    .android_nfc_register = impl_android_nfc_register,
    .android_nfc_start = impl_android_nfc_start_new,
    .android_nfc_stop = impl_android_nfc_stop_new,
    .android_nfc_available = impl_android_nfc_available_new,
    .android_nfc_enabled = impl_android_nfc_enabled_new,
    .android_rfid_register = impl_android_rfid_register,
    .android_rfid_start = impl_android_nfc_start_new,
    .android_rfid_stop = impl_android_nfc_stop_new,
    .android_biometric_register = impl_android_biometric_register,
    .android_biometric_available = impl_android_bio_available_new,
    .android_biometric_authenticate = impl_android_bio_authenticate_new,
    .android_location_register = impl_android_location_register,
    .android_location_available = impl_android_loc_available_new,
    .android_location_start = impl_android_loc_start_new,
    .android_location_stop = impl_android_loc_stop_new,
    .android_record_start = impl_android_rec_start_new,
    .android_record_stop = impl_android_rec_stop_new,
    .android_shortcut_add = impl_android_sc_add_new,
    .android_shortcut_remove = impl_android_sc_remove_new,
    .android_shortcut_count = impl_android_sc_count_new,
    .android_pip_enter = impl_android_pip_enter_new,
    .android_pip_available = impl_android_pip_available_new,
    .android_wallpaper_set_image = impl_android_wp_set_image_new,
    .android_volume_music_get = impl_android_vol_music_get_new,
    .android_volume_music_max = impl_android_vol_music_max_new,
    .android_volume_music_set = impl_android_vol_set_new,
    .android_ringer_get = impl_android_ringer_get_new,
    .android_storage_free_mb = impl_android_storage_free_mb_new,
    .android_storage_total_mb = impl_android_storage_total_mb_new,
    .android_sysbar_status_px = impl_android_sysbar_status_new,
    .android_sysbar_nav_px = impl_android_sysbar_nav_new,
    .android_keyboard_visible = impl_android_kb_visible_new,
    .android_app_installed = impl_android_app_installed_new,
    .android_app_open = impl_android_app_open_new,
    .android_contacts_pick = impl_android_contacts_pick,
    .android_shot_capture = impl_android_shot_capture,
    .android_http_fetch = impl_android_http_fetch_new,
    .android_open_url = impl_android_open_url_new,
    .android_ir_available = impl_android_ir_available_new,
    .android_ir_transmit = impl_android_ir_transmit_new,
    .android_asset_exists = impl_android_asset_exists_new,
    .android_asset_size = impl_android_asset_size_new,
    .android_asset_read = impl_android_asset_read_new,
    .android_asset_list = impl_android_asset_list_new,
    .android_asset_cache_path = impl_android_asset_cache_path_new,
};

#endif