/**
 * @file aroma_platform_interface.h
 * @brief Abstract interface for platform-specific operations.
 *
 * Defines the virtual table (struct AromaPlatformInterface) that backends (Android, SDL, TFT, etc.)
 * must implement to handle window creation, input loops, rendering context management,
 * and platform-specific capabilities (permissions, hardware access).
 */
#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaDrawList AromaDrawList;

/**
 * @brief VTable for platform-specific implementations.
 */
typedef struct AromaPlatformInterface {

    /** @brief Initialize the backend. Returns 0 on success. */
    int  (*initialize)(void);
    
    /** @brief Shutdown the backend and cleanup. */
    void (*shutdown)(void);

    /**
     * @brief Create a platform window.
     * @param title Window title.
     * @param x Initial X coordinate.
     * @param y Initial Y coordinate.
     * @param width Width.
     * @param height Height.
     * @return ID of the created window (non-zero handle/index).
     */
    size_t (*create_window)(
        const char* title,
        int x, int y,
        int width, int height
    );

    /** @brief Activate the rendering context for valid window. */
    void (*make_context_current)(size_t window_id);

    /** @brief Register global window update callback. */
    void (*set_window_update_callback)(
        void (*callback)(size_t window_id, void *data),
        void* data
    );

    /** @brief Get current window dimensions. */
    void (*get_window_size)(
        size_t window_id,
        int *window_width,
        int *window_height
    );

    /** @brief Request a redraw for a specific window. */
    void (*request_window_update)(size_t window_id);

    /** @brief Run the main event loop (blocking). Returns false on exit. */
    bool (*run_event_loop)(void);
    
    /** @brief Swap front/back buffers. */
    void (*swap_buffers)(size_t window_id);

    /** @brief Get raw TFT context (embedded specific). */
    void* (*get_tft_context)(void);
    
    /** @brief Callback to flush custom draw lists (embedded specific). */
    void (*call_flush_function_ptr)(void (*flush_fn)(struct AromaDrawList* list, size_t window_id, int x, int y, int width, int height), void* list);    

    // Dirty region set
    
    /** @brief [Embedded] Mark TFT tile rows as dirty. */
    void (*tft_mark_tiles_dirty)(int y, int h);
    
    /** @brief [Embedded] Set the global clear color. */
    void (*set_clear_color)(uint16_t color);

    // [Android]
    
    /** @brief [Android] Set the native glue app pointer. */
    void (*set_android_app)(void* app_state);
    
    /** @brief Set fullscreen mode for window. */
    void (*set_fullscreen)(size_t window_id, bool enabled);
    
    /** @brief Open a URL in the system browser. */
    void (*open_url)(const char* url);
    
    /** @brief [Android] Send a generic Android Intent. */
    void (*android_send_intent)(int action, const char* uri, const char* type, const void* extras, int extra_count);

    /** @brief Request software keyboard. */
    void (*show_keyboard)(void);
    
    /** @brief Hide software keyboard. */
    void (*hide_keyboard)(void);

    // [Android Extensions]
    
    /** @brief [Android] Check runtime permission. */
    bool (*android_check_permission)(const char* permission_name);
    
    /** @brief [Android] Request runtime permission. */
    void (*android_request_permission)(const char* permission_name);
    
    /** @brief [Android] Show a toast message. */
    void (*android_toast)(const char* msg, bool long_duration);
    
    /** @brief [Android] Open system settings. */
    void (*android_open_settings)(void);
    
    /** @brief [Android] Vibrate device. */
    void (*android_vibrate)(int ms);
    
    /** @brief [Android] Get battery percentage. */
    int (*android_get_battery_level)(void);
    
    /** @brief [Android] Check if WiFi is enabled. */
    bool (*android_is_wifi_enabled)(void);
    
    /** @brief [Android] Toggle WiFi. */
    void (*android_set_wifi_enabled)(bool enabled);
    
    /** @brief [Android] Check if Bluetooth is enabled. */
    bool (*android_is_bluetooth_enabled)(void);
    /** @brief [Android] Get paired Bluetooth devices. Fills out_addrs[out_names] arrays. */
    int  (*android_bt_get_paired)(char out_addrs[][18], char out_names[][248], int max_devices);
    /** @brief [Android] Connect to Bluetooth device by address. */
    bool (*android_bt_connect)(const char* addr);
    /** @brief [Android] Disconnect current Bluetooth connection. */
    void (*android_bt_disconnect)(void);
    /** @brief [Android] Send data over active Bluetooth connection. Returns bytes written or -1. */
    int  (*android_bt_send)(const char* data, int len);
    /** @brief [Android] Check if bluetooth connection active. */
    bool (*android_bt_is_connected)(void);
    
    /** @brief [Android] Launch camera app. */
    void (*android_launch_camera)(void);
    
    /** @brief [Android] Launch gallery app. */
    void (*android_launch_gallery)(void);
    
    /** @brief [Android] Get generic system service (JObject). */
    void* (*android_get_system_service)(const char* service_name);

    // [Android Utils]
    
    /** @brief [Android] Get internal storage path. */
    const char* (*android_get_internal_path)(void);
    
    /** @brief [Android] Get external storage path. */
    const char* (*android_get_external_path)(void);

} AromaPlatformInterface;

/**
 * @brief Get the active platform implementation.
 * @return Pointer to the active AromaPlatformInterface.
 */
AromaPlatformInterface* aroma_get_platform_interface(void);

/** @brief GL/SDL/Simulator platform instance. */
extern AromaPlatformInterface aroma_platform_glps;

/** @brief Bare metal / TFT platform instance. */
extern AromaPlatformInterface aroma_platform_tft;

/** @brief Android platform instance. */
extern AromaPlatformInterface aroma_platform_android;

#ifdef __cplusplus
}
#endif

#endif 

