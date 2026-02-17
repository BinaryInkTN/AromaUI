
/**
 * @file aroma_android.h
 * @brief Android platform integration API for AromaUI.
 *
 * This header provides a set of constants and inline functions to interact with Android-specific features
 * such as permissions, Bluetooth, WiFi, battery, vibration, system services, and more. All functions are
 * wrappers around the AromaPlatformInterface, which must be implemented for the Android platform.
 *
 * Usage of these APIs is only valid when compiling for Android (__ANDROID__ defined).
 */

#ifndef AROMA_ANDROID_H
#define AROMA_ANDROID_H

#ifdef __ANDROID__

#include <jni.h>
#include <stdbool.h>
#include "../src/backends/platforms/aroma_platform_interface.h"

#ifdef __cplusplus
extern "C" {
#endif


/** @name Bluetooth Scan Modes */
/**@{*/
#define AROMA_BT_SCAN_MODE_PAIRED 0   /**< Scan for paired devices only. */
#define AROMA_BT_SCAN_MODE_NEW    1   /**< Scan for new (unpaired) devices only. */
#define AROMA_BT_SCAN_MODE_ALL    2   /**< Scan for all devices. */
/**@}*/

/** @name Bluetooth Device Types */
/**@{*/
#define AROMA_BT_TYPE_UNKNOWN    0   /**< Unknown device type. */
#define AROMA_BT_TYPE_HEADSET    1   /**< Headset device. */
#define AROMA_BT_TYPE_PHONE      2   /**< Phone device. */
#define AROMA_BT_TYPE_SPEAKER    3   /**< Speaker device. */
#define AROMA_BT_TYPE_WEARABLE   4   /**< Wearable device. */
#define AROMA_BT_TYPE_KEYBOARD   5   /**< Keyboard device. */
#define AROMA_BT_TYPE_MOUSE      6   /**< Mouse device. */
#define AROMA_BT_TYPE_PRINTER    7   /**< Printer device. */
#define AROMA_BT_TYPE_CAR        8   /**< Car device. */
#define AROMA_BT_TYPE_MEDICAL    9   /**< Medical device. */
#define AROMA_BT_TYPE_ARDUINO    10  /**< Arduino device. */
#define AROMA_BT_TYPE_RASPBERRY  11  /**< Raspberry Pi device. */
/**@}*/

/** @name Bluetooth Connection Modes */
/**@{*/
#define AROMA_BT_MODE_DATA  0   /**< Data mode. */
#define AROMA_BT_MODE_AUDIO 1   /**< Audio mode. */
#define AROMA_BT_MODE_HID   2   /**< Human Interface Device mode. */
#define AROMA_BT_MODE_AUTO  3   /**< Auto-select mode. */
/**@}*/

/** @name Bluetooth Bond States */
/**@{*/
#define AROMA_BT_BOND_NONE     10  /**< Not bonded. */
#define AROMA_BT_BOND_BONDING  11  /**< Bonding in progress. */
#define AROMA_BT_BOND_BONDED   12  /**< Bonded. */
/**@}*/


/**
 * @brief Get the current JNI environment pointer.
 * @return JNIEnv* pointer for the current thread.
 */
JNIEnv* aroma_android_get_env();

/**
 * @brief Get the current Android Activity as a jobject.
 * @return jobject representing the current Activity.
 */
jobject aroma_android_get_activity();

/**
 * @brief Get the JavaVM pointer for the current process.
 * @return JavaVM* pointer.
 */
JavaVM* aroma_android_get_jvm();


/**
 * @brief Check if a specific Android permission is granted.
 * @param permission_name The name of the permission (e.g., "android.permission.BLUETOOTH").
 * @return true if granted, false otherwise.
 */
static inline bool aroma_android_check_permission(const char* permission_name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_check_permission) {
        return platform->android_check_permission(permission_name);
    }
    return false;
}


/**
 * @brief Request one or more Android permissions at runtime.
 * @param permissions Array of permission names.
 * @param permCount Number of permissions in the array.
 */
static inline void aroma_android_request_permission(const char** permissions, int permCount) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_request_permission) {
        platform->android_request_permission(permissions, permCount);
    }
}


/**
 * @brief Show a toast message on the Android device.
 * @param msg The message to display.
 * @param long_duration true for long duration, false for short.
 */
static inline void aroma_android_toast(const char* msg, bool long_duration) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_toast) {
        platform->android_toast(msg, long_duration);
    }
}


/**
 * @brief Open the Android system settings screen.
 */
static inline void aroma_android_open_settings() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_open_settings) {
        platform->android_open_settings();
    }
}


/**
 * @brief Vibrate the device for a specified duration.
 * @param ms Duration in milliseconds.
 */
static inline void aroma_android_vibrate(int ms) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_vibrate) {
        platform->android_vibrate(ms);
    }
}


/**
 * @brief Get the current battery level as a percentage.
 * @return Battery level (0-100), or -1 if unavailable.
 */
static inline int aroma_android_get_battery_level() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_battery_level) {
        return platform->android_get_battery_level();
    }
    return -1;
}


/**
 * @brief Check if WiFi is enabled on the device.
 * @return true if WiFi is enabled, false otherwise.
 */
static inline bool aroma_android_is_wifi_enabled() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_wifi_enabled) {
        return platform->android_is_wifi_enabled();
    }
    return false;
}


/**
 * @brief Enable or disable WiFi on the device.
 * @param enabled true to enable, false to disable.
 */
static inline void aroma_android_set_wifi_enabled(bool enabled) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_set_wifi_enabled) {
        platform->android_set_wifi_enabled(enabled);
    }
}


/**
 * @brief Check if Bluetooth is enabled on the device.
 * @return true if Bluetooth is enabled, false otherwise.
 */
static inline bool aroma_android_is_bluetooth_enabled() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_is_bluetooth_enabled) {
        return platform->android_is_bluetooth_enabled();
    }
    return false;
}


/**
 * @brief Start scanning for Bluetooth devices.
 * @param scan_mode One of AROMA_BT_SCAN_MODE_*.
 * @param callback Function called for each discovered device.
 * @return Number of devices found (if available), or 0.
 */
static inline int aroma_android_bt_scan(int scan_mode, void (*callback)(const char* addr, const char* name, int type, int rssi)) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_scan) {
        return platform->android_bt_scan(scan_mode, callback);
    }
    return 0;
}


/**
 * @brief Stop ongoing Bluetooth device scan.
 */
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


/**
 * @brief Get a list of paired Bluetooth devices.
 * @param out_addrs Output array for device addresses (size: max_devices x 18).
 * @param out_names Output array for device names (size: max_devices x 248).
 * @param max_devices Maximum number of devices to retrieve.
 * @return Number of paired devices found.
 */
static inline int aroma_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_paired) {
        return platform->android_bt_get_paired(out_addrs, out_names, max_devices);
    }
    return 0;
}


/**
 * @brief Initiate pairing with a Bluetooth device.
 * @param addr Bluetooth address of the device.
 * @return true if pairing started, false otherwise.
 */
static inline bool aroma_android_bt_pair(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_pair) {
        return platform->android_bt_pair(addr);
    }
    return false;
}


/**
 * @brief Unpair a Bluetooth device.
 * @param addr Bluetooth address of the device.
 * @return true if unpairing started, false otherwise.
 */
static inline bool aroma_android_bt_unpair(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_unpair) {
        return platform->android_bt_unpair(addr);
    }
    return false;
}


/**
 * @brief Get the bond (pairing) state of a Bluetooth device.
 * @param addr Bluetooth address of the device.
 * @return One of AROMA_BT_BOND_*.
 */
static inline int aroma_android_bt_get_pair_state(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_pair_state) {
        return platform->android_bt_get_pair_state(addr);
    }
    return AROMA_BT_BOND_NONE;
}


/**
 * @brief Connect to a Bluetooth device using default mode.
 * @param addr Bluetooth address of the device.
 * @return true if connection started, false otherwise.
 */
static inline bool aroma_android_bt_connect(const char* addr) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_connect) {
        return platform->android_bt_connect(addr);
    }
    return false;
}


/**
 * @brief Connect to a Bluetooth device with a specific mode.
 * @param addr Bluetooth address of the device.
 * @param mode One of AROMA_BT_MODE_*.
 * @return true if connection started, false otherwise.
 */
static inline bool aroma_android_bt_connect_with_mode(const char* addr, int mode) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_connect_with_mode) {
        return platform->android_bt_connect_with_mode(addr, mode);
    }
    return false;
}

/**
 * @brief Disconnect from the currently connected Bluetooth device.
 */
static inline void aroma_android_bt_disconnect(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_disconnect) {
        platform->android_bt_disconnect();
    }
}


/**
 * @brief Send data to the connected Bluetooth device.
 * @param data Pointer to data buffer.
 * @param len Length of data in bytes.
 * @return Number of bytes sent, or -1 on error.
 */
static inline int aroma_android_bt_send(const char* data, int len) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_send) {
        return platform->android_bt_send(data, len);
    }
    return -1;
}


/**
 * @brief Check if a Bluetooth device is currently connected.
 * @return true if connected, false otherwise.
 */
static inline bool aroma_android_bt_is_connected(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_is_connected) {
        return platform->android_bt_is_connected();
    }
    return false;
}


/**
 * @brief Get the type of the currently connected Bluetooth device.
 * @return One of AROMA_BT_TYPE_*.
 */
static inline int aroma_android_bt_get_device_type(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_device_type) {
        return platform->android_bt_get_device_type();
    }
    return AROMA_BT_TYPE_UNKNOWN;
}


/**
 * @brief Get the name of the currently connected Bluetooth device.
 * @return Device name string, or NULL if unavailable.
 */
static inline const char* aroma_android_bt_get_device_name(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_device_name) {
        return platform->android_bt_get_device_name();
    }
    return NULL;
}


/**
 * @brief Get the current Bluetooth connection mode.
 * @return One of AROMA_BT_MODE_*.
 */
static inline int aroma_android_bt_get_current_mode(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_current_mode) {
        return platform->android_bt_get_current_mode();
    }
    return AROMA_BT_MODE_AUTO;
}


/**
 * @brief Get the name of the current Bluetooth mode.
 * @return Mode name string, or NULL if unavailable.
 */
static inline const char* aroma_android_bt_get_mode_name(void) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_bt_get_mode_name) {
        return platform->android_bt_get_mode_name();
    }
    return NULL;
}


/**
 * @brief Launch the device camera application.
 */
static inline void aroma_android_launch_camera() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_launch_camera) {
        platform->android_launch_camera();
    }
}


/**
 * @brief Launch the device gallery application.
 */
static inline void aroma_android_launch_gallery() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_launch_gallery) {
        platform->android_launch_gallery();
    }
}


/**
 * @brief Get a system service by name.
 * @param service_name Name of the Android system service.
 * @return jobject representing the service, or NULL if unavailable.
 */
static inline jobject aroma_android_get_system_service(const char* service_name) {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_system_service) {
        return (jobject)platform->android_get_system_service(service_name);
    }
    return NULL;
}


/**
 * @brief Get the path to the app's internal storage directory.
 * @return Path string, or NULL if unavailable.
 */
static inline const char* aroma_android_get_internal_path() {
    AromaPlatformInterface* platform = aroma_get_platform_interface();
    if (platform && platform->android_get_internal_path) {
        return platform->android_get_internal_path();
    }
    return NULL;
}


/**
 * @brief Get the path to the app's external storage directory.
 * @return Path string, or NULL if unavailable.
 */
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

#endif // __ANDROID__
#endif // AROMA_ANDROID_H