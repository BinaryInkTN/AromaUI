#ifndef AROMA_ANDROID_H
#define AROMA_ANDROID_H

#ifdef __ANDROID__

#include <jni.h>
#include <stdbool.h>
#include "aroma_platform_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

JNIEnv* aroma_android_get_env();
jobject aroma_android_get_activity();
JavaVM* aroma_android_get_jvm();

static inline bool aroma_android_check_permission(const char* permission_name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_check_permission) {
        return platform->android_check_permission(permission_name);
    }
    return false;
}

static inline void aroma_android_request_permission(const char* permission_name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_request_permission) {
        platform->android_request_permission(permission_name);
    }
}

static inline void aroma_android_toast(const char* msg, bool long_duration) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_toast) {
        platform->android_toast(msg, long_duration);
    }
}

static inline void aroma_android_open_settings() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_open_settings) {
        platform->android_open_settings();
    }
}

static inline void aroma_android_vibrate(int ms) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_vibrate) {
        platform->android_vibrate(ms);
    }
}

static inline int aroma_android_get_battery_level() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_battery_level) {
        return platform->android_get_battery_level();
    }
    return -1;
}

static inline bool aroma_android_is_wifi_enabled() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_wifi_enabled) {
        return platform->android_is_wifi_enabled();
    }
    return false;
}

static inline void aroma_android_set_wifi_enabled(bool enabled) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_wifi_enabled) {
        platform->android_set_wifi_enabled(enabled);
    }
}

static inline bool aroma_android_is_bluetooth_enabled() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_bluetooth_enabled) {
        return platform->android_is_bluetooth_enabled();
    }
    return false;
}

static inline void aroma_android_launch_camera() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_launch_camera) {
        platform->android_launch_camera();
    }
}

static inline void aroma_android_launch_gallery() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_launch_gallery) {
        platform->android_launch_gallery();
    }
}

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
