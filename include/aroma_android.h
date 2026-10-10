










#ifndef AROMA_ANDROID_H
#define AROMA_ANDROID_H

#ifdef __ANDROID__

#include <jni.h>
#include <stdbool.h>
#include "../src/backends/platforms/aroma_platform_interface.h"

#ifdef __cplusplus
extern "C" {
#endif




#define AROMA_BT_SCAN_MODE_PAIRED 0
#define AROMA_BT_SCAN_MODE_NEW    1
#define AROMA_BT_SCAN_MODE_ALL    2




#define AROMA_BT_TYPE_UNKNOWN    0
#define AROMA_BT_TYPE_HEADSET    1
#define AROMA_BT_TYPE_PHONE      2
#define AROMA_BT_TYPE_SPEAKER    3
#define AROMA_BT_TYPE_WEARABLE   4
#define AROMA_BT_TYPE_KEYBOARD   5
#define AROMA_BT_TYPE_MOUSE      6
#define AROMA_BT_TYPE_PRINTER    7
#define AROMA_BT_TYPE_CAR        8
#define AROMA_BT_TYPE_MEDICAL    9
#define AROMA_BT_TYPE_ARDUINO    10
#define AROMA_BT_TYPE_RASPBERRY  11




#define AROMA_BT_MODE_DATA  0
#define AROMA_BT_MODE_AUDIO 1
#define AROMA_BT_MODE_HID   2
#define AROMA_BT_MODE_AUTO  3




#define AROMA_BT_BOND_NONE     10
#define AROMA_BT_BOND_BONDING  11
#define AROMA_BT_BOND_BONDED   12







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







static inline void aroma_android_request_permission(const char** permissions, int permCount) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_request_permission) {
        platform->android_request_permission(permissions, permCount);
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








static inline int aroma_android_bt_scan(int scan_mode, void (*callback)(const char* addr, const char* name, int type, int rssi)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_scan) {
        return platform->android_bt_scan(scan_mode, callback);
    }
    return 0;
}





static inline void aroma_android_bt_stop_scan(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_stop_scan) {
        platform->android_bt_stop_scan();
    }
}










static inline void aroma_android_bt_register_callbacks(void (*device_cb)(const char*, const char*, int, int),
                                      void (*scan_finished_cb)(void),
                                      void (*pairing_cb)(bool, const char*, const char*),
                                      void (*connection_cb)(bool, const char*, int, int),
                                      void (*data_cb)(const char*, int)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_register_callbacks) {
        platform->android_bt_register_callbacks(device_cb, scan_finished_cb, pairing_cb, connection_cb, data_cb);
    }
}









static inline int aroma_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_paired) {
        return platform->android_bt_get_paired(out_addrs, out_names, max_devices);
    }
    return 0;
}







static inline bool aroma_android_bt_pair(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_pair) {
        return platform->android_bt_pair(addr);
    }
    return false;
}







static inline bool aroma_android_bt_unpair(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_unpair) {
        return platform->android_bt_unpair(addr);
    }
    return false;
}







static inline int aroma_android_bt_get_pair_state(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_pair_state) {
        return platform->android_bt_get_pair_state(addr);
    }
    return AROMA_BT_BOND_NONE;
}







static inline bool aroma_android_bt_connect(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_connect) {
        return platform->android_bt_connect(addr);
    }
    return false;
}








static inline bool aroma_android_bt_connect_with_mode(const char* addr, int mode) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_connect_with_mode) {
        return platform->android_bt_connect_with_mode(addr, mode);
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






static inline int aroma_android_bt_get_device_type(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_device_type) {
        return platform->android_bt_get_device_type();
    }
    return AROMA_BT_TYPE_UNKNOWN;
}






static inline const char* aroma_android_bt_get_device_name(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_device_name) {
        return platform->android_bt_get_device_name();
    }
    return NULL;
}






static inline int aroma_android_bt_get_current_mode(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_current_mode) {
        return platform->android_bt_get_current_mode();
    }
    return AROMA_BT_MODE_AUTO;
}






static inline const char* aroma_android_bt_get_mode_name(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_mode_name) {
        return platform->android_bt_get_mode_name();
    }
    return NULL;
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









static inline float aroma_android_get_density(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_density) {
        return platform->android_get_density();
    }
    return 1.0f;
}






static inline int aroma_android_get_density_dpi(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_density_dpi) {
        return platform->android_get_density_dpi();
    }
    return 160;
}






static inline float aroma_android_get_scaled_density(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_scaled_density) {
        return platform->android_get_scaled_density();
    }
    return 1.0f;
}







static inline int aroma_android_dp_to_px(int dp) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_dp_to_px) {
        return platform->android_dp_to_px(dp);
    }
    return dp;
}







static inline int aroma_android_px_to_dp(int px) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_px_to_dp) {
        return platform->android_px_to_dp(px);
    }
    return px;
}







static inline int aroma_android_sp_to_px(int sp) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sp_to_px) {
        return platform->android_sp_to_px(sp);
    }
    return sp;
}







static inline int aroma_android_px_to_sp(int px) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_px_to_sp) {
        return platform->android_px_to_sp(px);
    }
    return px;
}








static inline float aroma_android_dp_to_px_f(float dp) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_dp_to_px_f) {
        return platform->android_dp_to_px_f(dp);
    }
    if (platform && platform->android_get_density) {
        return dp * platform->android_get_density();
    }
    return dp;
}







static inline float aroma_android_sp_to_px_f(float sp) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sp_to_px_f) {
        return platform->android_sp_to_px_f(sp);
    }
    if (platform && platform->android_get_scaled_density) {
        return sp * platform->android_get_scaled_density();
    }
    return sp;
}





static inline float aroma_android_px_to_dp_f(float px) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_px_to_dp_f) {
        return platform->android_px_to_dp_f(px);
    }
    if (platform && platform->android_get_density) {
        float d = platform->android_get_density();
        if (d > 0.0f) return px / d;
    }
    return px;
}







static inline void aroma_android_get_available_size_dp(int *width_dp, int *height_dp) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_available_size_dp) {
        platform->android_get_available_size_dp(width_dp, height_dp);
    } else {
        *width_dp = 0;
        *height_dp = 0;
    }
}







static inline void aroma_android_get_screen_size_inches(float *width_inches, float *height_inches) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_screen_size_inches) {
        platform->android_get_screen_size_inches(width_inches, height_inches);
    } else {
        *width_inches = 0.0f;
        *height_inches = 0.0f;
    }
}






static inline float aroma_android_get_screen_diagonal_inches(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_screen_diagonal_inches) {
        return platform->android_get_screen_diagonal_inches();
    }
    return 0.0f;
}






static inline const char* aroma_android_get_screen_size_category(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_screen_size_category) {
        return platform->android_get_screen_size_category();
    }
    return "normal";
}






static inline float aroma_android_get_xdpi(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_xdpi) {
        return platform->android_get_xdpi();
    }
    return 160.0f;
}






static inline float aroma_android_get_ydpi(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_ydpi) {
        return platform->android_get_ydpi();
    }
    return 160.0f;
}










static inline void aroma_android_lock_orientation(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_lock_orientation) {
        platform->android_lock_orientation();
    }
}





static inline void aroma_android_unlock_orientation(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_unlock_orientation) {
        platform->android_unlock_orientation();
    }
}





static inline void aroma_android_set_orientation_portrait(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_orientation_portrait) {
        platform->android_set_orientation_portrait();
    }
}





static inline void aroma_android_set_orientation_landscape(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_orientation_landscape) {
        platform->android_set_orientation_landscape();
    }
}





static inline void aroma_android_set_orientation_sensor(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_orientation_sensor) {
        platform->android_set_orientation_sensor();
    }
}






static inline int aroma_android_get_current_orientation(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_current_orientation) {
        return platform->android_get_current_orientation();
    }
    return -1;
}






static inline bool aroma_android_is_orientation_locked(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_orientation_locked) {
        return platform->android_is_orientation_locked();
    }
    return false;
}

static inline const char* aroma_android_get_preference_string(const char* key, const char* default_value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_preference_string) {
        return platform->android_get_preference_string(key, default_value);
    }
    return default_value;
}

static inline void aroma_android_set_preference_string(const char* key, const char* value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_preference_string) {
        platform->android_set_preference_string(key, value);
    }
}

static inline int aroma_android_get_preference_int(const char* key, int default_value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_preference_int) {
        return platform->android_get_preference_int(key, default_value);
    }
    return default_value;
}

static inline void aroma_android_set_preference_int(const char* key, int value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_preference_int) {
        platform->android_set_preference_int(key, value);
    }
}

static inline float aroma_android_get_preference_float(const char* key, float default_value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_preference_float) {
        return platform->android_get_preference_float(key, default_value);
    }
    return default_value;
}

static inline void aroma_android_set_preference_float(const char* key, float value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_preference_float) {
        platform->android_set_preference_float(key, value);
    }
}

static inline bool aroma_android_get_preference_bool(const char* key, bool default_value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_preference_bool) {
        return platform->android_get_preference_bool(key, default_value);
    }
    return default_value;
}

static inline void aroma_android_set_preference_bool(const char* key, bool value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_preference_bool) {
        platform->android_set_preference_bool(key, value);
    }
}

static inline long aroma_android_get_preference_long(const char* key, long default_value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_preference_long) {
        return platform->android_get_preference_long(key, default_value);
    }
    return default_value;
}

static inline void aroma_android_set_preference_long(const char* key, long value) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_preference_long) {
        platform->android_set_preference_long(key, value);
    }
}

#define AROMA_BTLE_WRITE_WITH_RESPONSE 2
#define AROMA_BTLE_WRITE_NO_RESPONSE 1
#define AROMA_SENSOR_ACCELEROMETER 1
#define AROMA_SENSOR_MAGNETIC_FIELD 2
#define AROMA_SENSOR_GYROSCOPE 4
#define AROMA_NET_NONE 0
#define AROMA_NET_WIFI 1
#define AROMA_NET_MOBILE 2
#define AROMA_NET_OTHER 3
#define AROMA_NOTIFY_IMPORTANCE_NONE 0
#define AROMA_NOTIFY_IMPORTANCE_MIN 1
#define AROMA_NOTIFY_IMPORTANCE_LOW 2
#define AROMA_NOTIFY_IMPORTANCE_DEFAULT 3
#define AROMA_NOTIFY_IMPORTANCE_HIGH 4
#define AROMA_ACTIVITY_REQ_PERMISSION_CB 1001
#define AROMA_ACTIVITY_REQ_PICK_IMAGE 2001
#define AROMA_ACTIVITY_REQ_CAPTURE_PHOTO 2002
#define AROMA_ACTIVITY_REQ_OPEN_DOCUMENT 2003


static inline void aroma_android_request_permissions_cb(const char** permissions, int permCount, void (*result_cb)(const char* permission, bool granted)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_request_permission_cb) {
            platform->android_request_permission_cb(permissions, permCount, result_cb);
    }
}

static inline void aroma_android_pick_image(void (*result_cb)(const char* path)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_pick_image) {
            platform->android_pick_image(result_cb);
    }
}

static inline void aroma_android_capture_photo(void (*result_cb)(const char* path)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_capture_photo) {
            platform->android_capture_photo(result_cb);
    }
}

static inline void aroma_android_open_document(const char* mime, void (*result_cb)(const char* path)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_open_document) {
            platform->android_open_document(mime, result_cb);
    }
}

static inline void aroma_android_notify_channel(int id, const char* name, const char* desc, int importance) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_notify_channel) {
            platform->android_notify_channel(id, name, desc, importance);
    }
}

static inline void aroma_android_notify_show(int id, const char* channel, const char* title, const char* text) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_notify_show) {
            platform->android_notify_show(id, channel, title, text);
    }
}

static inline void aroma_android_notify_cancel(int id) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_notify_cancel) {
            platform->android_notify_cancel(id);
    }
}

static inline void aroma_android_notify_cancel_all(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_notify_cancel_all) {
            platform->android_notify_cancel_all();
    }
}

static inline bool aroma_android_notify_enabled(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_notify_enabled) {
            return platform->android_notify_enabled();
    }
    return false;
}

static inline void aroma_android_vibrate_effect(int ms, int amplitude) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_vibrate_effect) {
            platform->android_vibrate_effect(ms, amplitude);
    }
}

static inline void aroma_android_set_clipboard_text(const char* text) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_clipboard_text) {
            platform->android_set_clipboard_text(text);
    }
}

static inline const char* aroma_android_get_clipboard_text(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_clipboard_text) {
            return platform->android_get_clipboard_text();
    }
    return NULL;
}

static inline void aroma_android_share_text(const char* text, const char* title) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_share_text) {
            platform->android_share_text(text, title);
    }
}

static inline void aroma_android_set_immersive(bool enabled) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_immersive) {
            platform->android_set_immersive(enabled);
    }
}

static inline void aroma_android_set_keep_screen_on(bool enabled) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_keep_screen_on) {
            platform->android_set_keep_screen_on(enabled);
    }
}

static inline const char* aroma_android_get_locale_tag(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_locale_tag) {
            return platform->android_get_locale_tag();
    }
    return NULL;
}

static inline const char* aroma_android_get_timezone_id(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_timezone_id) {
            return platform->android_get_timezone_id();
    }
    return NULL;
}

static inline const char* aroma_android_get_manufacturer(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_manufacturer) {
            return platform->android_get_manufacturer();
    }
    return NULL;
}

static inline const char* aroma_android_get_model(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_model) {
            return platform->android_get_model();
    }
    return NULL;
}

static inline const char* aroma_android_get_os_version(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_os_version) {
            return platform->android_get_os_version();
    }
    return NULL;
}

static inline int aroma_android_get_sdk_int(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_sdk_int) {
            return platform->android_get_sdk_int();
    }
    return 0;
}

static inline const char* aroma_android_get_package_name(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_package_name) {
            return platform->android_get_package_name();
    }
    return NULL;
}

static inline const char* aroma_android_get_version_name(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_version_name) {
            return platform->android_get_version_name();
    }
    return NULL;
}

static inline long aroma_android_get_version_code(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_version_code) {
            return platform->android_get_version_code();
    }
    return 0;
}

static inline long aroma_android_get_install_time(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_install_time) {
            return platform->android_get_install_time();
    }
    return 0;
}

static inline long aroma_android_get_memory_avail_mb(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_memory_avail_mb) {
            return platform->android_get_memory_avail_mb();
    }
    return 0;
}

static inline bool aroma_android_is_memory_low(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_memory_low) {
            return platform->android_is_memory_low();
    }
    return false;
}

static inline bool aroma_android_is_network_connected(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_network_connected) {
            return platform->android_is_network_connected();
    }
    return false;
}

static inline int aroma_android_get_network_type(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_network_type) {
            return platform->android_get_network_type();
    }
    return 0;
}

static inline bool aroma_android_is_charging(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_charging) {
            return platform->android_is_charging();
    }
    return false;
}

static inline bool aroma_android_is_interactive(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_interactive) {
            return platform->android_is_interactive();
    }
    return false;
}

static inline const char* aroma_android_get_network_operator(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_network_operator) {
            return platform->android_get_network_operator();
    }
    return NULL;
}

static inline void aroma_android_wakelock_acquire(long timeout_ms) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_wakelock_acquire) {
            platform->android_wakelock_acquire(timeout_ms);
    }
}

static inline void aroma_android_wakelock_release(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_wakelock_release) {
            platform->android_wakelock_release();
    }
}

static inline bool aroma_android_set_torch_enabled(bool enabled) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_torch_enabled) {
            return platform->android_set_torch_enabled(enabled);
    }
    return false;
}

static inline void aroma_android_tts_speak(const char* text) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_tts_speak) {
            platform->android_tts_speak(text);
    }
}

static inline void aroma_android_tts_stop(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_tts_stop) {
            platform->android_tts_stop();
    }
}

static inline bool aroma_android_tts_is_speaking(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_tts_is_speaking) {
            return platform->android_tts_is_speaking();
    }
    return false;
}

static inline void aroma_android_sensor_register_callbacks(void (*cb)(int type, float x, float y, float z, long long timestamp_ns)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sensor_register_callbacks) {
            platform->android_sensor_register_callbacks(cb);
    }
}

static inline void aroma_android_sensor_start(int type, int rate_us, void (*cb)(int type, float x, float y, float z, long long timestamp_ns)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sensor_start) {
            platform->android_sensor_start(type, rate_us, cb);
    }
}

static inline void aroma_android_sensor_stop(int type) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sensor_stop) {
            platform->android_sensor_stop(type);
    }
}

static inline bool aroma_android_sensor_available(int type) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sensor_available) {
            return platform->android_sensor_available(type);
    }
    return false;
}

static inline void aroma_android_btle_scan(int timeout_ms, const char* service_uuids) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_scan) {
            platform->android_btle_scan(timeout_ms, service_uuids);
    }
}

static inline void aroma_android_btle_stop_scan(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_stop_scan) {
            platform->android_btle_stop_scan();
    }
}

static inline void aroma_android_btle_register_callbacks(void (*device_cb)(const char*, const char*, int), void (*scan_finished_cb)(void), void (*connection_cb)(const char*, int, bool), void (*services_cb)(const char*, const char*), void (*data_cb)(const char*, const char*, const char*, int), void (*write_cb)(const char*, const char*, int)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_register_callbacks) {
            platform->android_btle_register_callbacks(device_cb, scan_finished_cb, connection_cb, services_cb, data_cb, write_cb);
    }
}

static inline void aroma_android_btle_connect(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_connect) {
            platform->android_btle_connect(addr);
    }
}

static inline void aroma_android_btle_disconnect(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_disconnect) {
            platform->android_btle_disconnect();
    }
}

static inline bool aroma_android_btle_is_connected(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_is_connected) {
            return platform->android_btle_is_connected();
    }
    return false;
}

static inline bool aroma_android_btle_discover(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_discover) {
            return platform->android_btle_discover();
    }
    return false;
}

static inline bool aroma_android_btle_read(const char* service_uuid, const char* char_uuid) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_read) {
            return platform->android_btle_read(service_uuid, char_uuid);
    }
    return false;
}

static inline bool aroma_android_btle_write(const char* service_uuid, const char* char_uuid, const char* data, int len, int write_type) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_write) {
            return platform->android_btle_write(service_uuid, char_uuid, data, len, write_type);
    }
    return false;
}

static inline bool aroma_android_btle_notify(const char* service_uuid, const char* char_uuid, bool enable) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_btle_notify) {
            return platform->android_btle_notify(service_uuid, char_uuid, enable);
    }
    return false;
}

#define AROMA_ACTIVITY_REQ_SCREENSHOT 2004
#define AROMA_ACTIVITY_REQ_PICK_CONTACT 2005
#define AROMA_BIO_UNAVAILABLE 1
#define AROMA_BIO_AVAILABLE 0
#define AROMA_RINGER_SILENT 0
#define AROMA_RINGER_VIBRATE 1
#define AROMA_RINGER_NORMAL 2


static inline void aroma_android_nfc_register(void (*cb)(const char* payload)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_nfc_register) {
        platform->android_nfc_register(cb);
    }
}

static inline void aroma_android_nfc_start(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_nfc_start) {
        platform->android_nfc_start();
    }
}

static inline void aroma_android_nfc_stop(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_nfc_stop) {
        platform->android_nfc_stop();
    }
}

static inline void aroma_android_rfid_register(void (*cb)(const char* uid_hex)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_rfid_register) {
        platform->android_rfid_register(cb);
    }
}

static inline void aroma_android_rfid_start(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_rfid_start) {
        platform->android_rfid_start();
    }
}

static inline void aroma_android_rfid_stop(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_rfid_stop) {
        platform->android_rfid_stop();
    }
}

static inline bool aroma_android_nfc_available(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_nfc_available) {
        return platform->android_nfc_available();
    }
    return false;
}

static inline bool aroma_android_nfc_enabled(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_nfc_enabled) {
        return platform->android_nfc_enabled();
    }
    return false;
}

static inline void aroma_android_biometric_register(void (*cb)(bool success)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_biometric_register) {
        platform->android_biometric_register(cb);
    }
}

static inline int aroma_android_biometric_available(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_biometric_available) {
        return platform->android_biometric_available();
    }
    return 1;
}

static inline void aroma_android_biometric_authenticate(const char* title, const char* subtitle) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_biometric_authenticate) {
        platform->android_biometric_authenticate(title, subtitle);
    }
}

static inline void aroma_android_location_register(void (*cb)(double lat, double lon, float accuracy, long long time_ms)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_location_register) {
        platform->android_location_register(cb);
    }
}

static inline bool aroma_android_location_available(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_location_available) {
        return platform->android_location_available();
    }
    return false;
}

static inline bool aroma_android_location_start(long min_time_ms, float min_dist_m) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_location_start) {
        return platform->android_location_start(min_time_ms, min_dist_m);
    }
    return false;
}

static inline void aroma_android_location_stop(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_location_stop) {
        platform->android_location_stop();
    }
}

static inline bool aroma_android_record_start(const char* path) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_record_start) {
        return platform->android_record_start(path);
    }
    return false;
}

static inline bool aroma_android_record_stop(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_record_stop) {
        return platform->android_record_stop();
    }
    return false;
}

static inline bool aroma_android_shortcut_add(const char* id, const char* short_label, const char* long_label) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_shortcut_add) {
        return platform->android_shortcut_add(id, short_label, long_label);
    }
    return false;
}

static inline bool aroma_android_shortcut_remove(const char* id) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_shortcut_remove) {
        return platform->android_shortcut_remove(id);
    }
    return false;
}

static inline int aroma_android_shortcut_count(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_shortcut_count) {
        return platform->android_shortcut_count();
    }
    return 0;
}

static inline bool aroma_android_pip_enter(int w, int h) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_pip_enter) {
        return platform->android_pip_enter(w, h);
    }
    return false;
}

static inline bool aroma_android_pip_available(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_pip_available) {
        return platform->android_pip_available();
    }
    return false;
}

static inline bool aroma_android_wallpaper_set_image(const char* path) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_wallpaper_set_image) {
        return platform->android_wallpaper_set_image(path);
    }
    return false;
}

static inline int aroma_android_volume_music_get(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_volume_music_get) {
        return platform->android_volume_music_get();
    }
    return 0;
}

static inline int aroma_android_volume_music_max(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_volume_music_max) {
        return platform->android_volume_music_max();
    }
    return 0;
}

static inline void aroma_android_volume_music_set(int level) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_volume_music_set) {
        platform->android_volume_music_set(level);
    }
}

static inline int aroma_android_ringer_get(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_ringer_get) {
        return platform->android_ringer_get();
    }
    return 2;
}

static inline long aroma_android_storage_free_mb(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_storage_free_mb) {
        return platform->android_storage_free_mb();
    }
    return 0;
}

static inline long aroma_android_storage_total_mb(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_storage_total_mb) {
        return platform->android_storage_total_mb();
    }
    return 0;
}

static inline int aroma_android_sysbar_status_px(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sysbar_status_px) {
        return platform->android_sysbar_status_px();
    }
    return 0;
}

static inline int aroma_android_sysbar_nav_px(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_sysbar_nav_px) {
        return platform->android_sysbar_nav_px();
    }
    return 0;
}

static inline bool aroma_android_keyboard_visible(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_keyboard_visible) {
        return platform->android_keyboard_visible();
    }
    return false;
}

static inline bool aroma_android_app_installed(const char* pkg) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_app_installed) {
        return platform->android_app_installed(pkg);
    }
    return false;
}

static inline bool aroma_android_app_open(const char* pkg) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_app_open) {
        return platform->android_app_open(pkg);
    }
    return false;
}

static inline void aroma_android_contacts_pick(void (*cb)(const char* info)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_contacts_pick) {
        platform->android_contacts_pick(cb);
    }
}

static inline bool aroma_android_open_url(const char* url) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_open_url) {
        return platform->android_open_url(url);
    }
    return false;
}

static inline bool aroma_android_http_fetch(const char* url, const char* dest_path) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_http_fetch) {
        return platform->android_http_fetch(url, dest_path);
    }
    return false;
}

static inline void aroma_android_shot_capture(void (*cb)(const char* path)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_shot_capture) {
        platform->android_shot_capture(cb);
    }
}

static inline bool aroma_android_ir_available(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_ir_available) {
        return platform->android_ir_available();
    }
    return false;
}

static inline bool aroma_android_ir_transmit(int freq_hz, const int* pattern, int pattern_len) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_ir_transmit) {
        return platform->android_ir_transmit(freq_hz, pattern, pattern_len);
    }
    return false;
}

static inline bool aroma_android_asset_exists(const char* name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_asset_exists) {
        return platform->android_asset_exists(name);
    }
    return false;
}

static inline long aroma_android_asset_size(const char* name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_asset_size) {
        return platform->android_asset_size(name);
    }
    return -1;
}

static inline long aroma_android_asset_read(const char* name, char* out, long max_len) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_asset_read) {
        return platform->android_asset_read(name, out, max_len);
    }
    return -1;
}

static inline int aroma_android_asset_list(const char* dir, char out_names[][256], int max) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_asset_list) {
        return platform->android_asset_list(dir, out_names, max);
    }
    return 0;
}

static inline const char* aroma_android_asset_cache_path(const char* name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_asset_cache_path) {
        return platform->android_asset_cache_path(name);
    }
    return NULL;
}





#ifdef __cplusplus
}
#endif

#endif
#endif