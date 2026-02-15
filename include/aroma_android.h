

#ifndef AROMA_ANDROID_H
#define AROMA_ANDROID_H

/**
 * @file aroma_android.h
 * @brief Android-specific platform APIs.
 *
 * These functions are available only when compiling for Android (__ANDROID__ is defined).
 * They provide access to Android system services, permissions, and intents.
 */

#ifdef __ANDROID__

#include <jni.h>
#include <stdbool.h>
#include "../src/backends/platforms/aroma_platform_interface.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Get the JNI environment for the current thread.
 * @return Pointer to JNIEnv, or NULL if not attached.
 */
JNIEnv* aroma_android_get_env();

/**
 * @brief Get the Android Activity object.
 * @return Global reference to the Activity object (jobject).
 */
jobject aroma_android_get_activity();

/**
 * @brief Get the Java VM instance.
 * @return Pointer to JavaVM.
 */
JavaVM* aroma_android_get_jvm();

/**
 * @brief Check if the application has a specific permission.
 * @param permission_name The full Java class name of the permission (e.g., "android.permission.CAMERA").
 * @return true if permission is granted, false otherwise.
 */
static inline bool aroma_android_check_permission(const char* permission_name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_check_permission) {
        return platform->android_check_permission(permission_name);
    }
    return false;
}

/**
 * @brief Request a specific permission from the user.
 * 
 * This is an asynchronous operation. The result is delivered to the Activity.
 * @param permission_name The full Java class name of the permission.
 */
static inline void aroma_android_request_permission(const char** permissions, int permCount) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_request_permission) {
        platform->android_request_permission(permissions, permCount);
    }
}

/**
 * @brief Show a Toast message on the screen.
 * @param msg The message to display.
 * @param long_duration true for long duration, false for short duration.
 */
static inline void aroma_android_toast(const char* msg, bool long_duration) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_toast) {
        platform->android_toast(msg, long_duration);
    }
}

/**
 * @brief Open the Android System Settings.
 */
static inline void aroma_android_open_settings() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_open_settings) {
        platform->android_open_settings();
    }
}

/**
 * @brief Vibrate the device.
 * @param ms Duration in milliseconds.
 */
static inline void aroma_android_vibrate(int ms) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_vibrate) {
        platform->android_vibrate(ms);
    }
}

/**
 * @brief Get the current battery level.
 * @return Battery level as percentage (0-100), or -1 if unavailable.
 */
static inline int aroma_android_get_battery_level() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_battery_level) {
        return platform->android_get_battery_level();
    }
    return -1;
}

/**
 * @brief Check if Wi-Fi is enabled.
 * @return true if enabled, false otherwise.
 */
static inline bool aroma_android_is_wifi_enabled() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_wifi_enabled) {
        return platform->android_is_wifi_enabled();
    }
    return false;
}

/**
 * @brief Enable or disable Wi-Fi.
 * @note This may require special permissions or be restricted in newer Android versions.
 * @param enabled true to enable, false to disable.
 */
static inline void aroma_android_set_wifi_enabled(bool enabled) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_wifi_enabled) {
        platform->android_set_wifi_enabled(enabled);
    }
}

/**
 * @brief Check if Bluetooth is enabled.
 * @return true if enabled, false otherwise.
 */
static inline bool aroma_android_is_bluetooth_enabled() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_bluetooth_enabled) {
        return platform->android_is_bluetooth_enabled();
    }
    return false;
}

/**
 * @brief Get paired Bluetooth devices (address and name arrays).
 * @param out_addrs Caller-provided buffer of size [max_devices][18].
 * @param out_names Caller-provided buffer of size [max_devices][248].
 * @param max_devices Maximum devices to fill.
 * @return Number of devices written.
 */
static inline int aroma_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_paired) {
        return platform->android_bt_get_paired(out_addrs, out_names, max_devices);
    }
    return 0;
}

static inline bool aroma_android_bt_connect(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_connect) {
        return platform->android_bt_connect(addr);
    }
    return false;
}

static inline void aroma_android_bt_disconnect(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_disconnect) {
        platform->android_bt_disconnect();
    }
}

static inline int aroma_android_bt_send(const char* data, int len) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_send) {
        return platform->android_bt_send(data, len);
    }
    return -1;
}

static inline bool aroma_android_bt_is_connected(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_is_connected) {
        return platform->android_bt_is_connected();
    }
    return false;
}

/**
 * @brief Launch the default camera application to capture an image.
 */
static inline void aroma_android_launch_camera() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_launch_camera) {
        platform->android_launch_camera();
    }
}

/**
 * @brief Launch the gallery application to pick an image.
 */
static inline void aroma_android_launch_gallery() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_launch_gallery) {
        platform->android_launch_gallery();
    }
}

/**
 * @brief Get an Android system service object.
 * @param service_name The name of the service (e.g., "vibrator", "wifi").
 * @return Reference to the service object (jobject), or NULL if not found.
 */
static inline jobject aroma_android_get_system_service(const char* service_name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_system_service) {
        return (jobject)platform->android_get_system_service(service_name);
    }
    return NULL;
}

static inline const char* aroma_android_get_internal_path() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_internal_path) {
        return platform->android_get_internal_path();
    }
    return NULL;
}

static inline const char* aroma_android_get_external_path() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_external_path) {
        return platform->android_get_external_path();
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif 
#endif 
