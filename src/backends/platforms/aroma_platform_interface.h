/**
 * @file aroma_backend_interface.h
 * @brief Platform abstraction interface for AromaUI backends.
 *
 * This interface defines the platform layer used by AromaUI.
 * Each backend (GLPS, TFT, Android, etc.) must implement this interface.
 *
 */

#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Forward declaration of AromaDrawList.
 */
typedef struct AromaDrawList AromaDrawList;

/**
 * @brief Platform abstraction interface.
 *
 * This structure contains all function pointers required to implement
 * a backend for AromaUI.
 *
 * Each platform must provide implementations for relevant functions.
 * Unsupported features may be set to NULL.
 */
typedef struct AromaPlatformInterface {

    /* ======================== Core Lifecycle ======================== */

    /**
     * @brief Initialize the platform backend.
     * @return 0 on success, non-zero on failure.
     */
    int  (*initialize)(void);

    /**
     * @brief Shutdown and cleanup the backend.
     */
    void (*shutdown)(void);

    /* ======================== Window Management ======================== */

    /**
     * @brief Create a new window.
     *
     * @param title Window title.
     * @param x Initial X position.
     * @param y Initial Y position.
     * @param width Window width in pixels.
     * @param height Window height in pixels.
     * @return Unique window identifier.
     */
    size_t (*create_window)(
        const char* title,
        int x, int y,
        int width, int height
    );

    /**
     * @brief Make rendering context current for a window.
     */
    void (*make_context_current)(size_t window_id);

    /**
     * @brief Register window update callback.
     *
     * @param callback Function called when window needs redraw.
     * @param data User-defined pointer passed to callback.
     */
    void (*set_window_update_callback)(
        void (*callback)(size_t window_id, void *data),
        void* data
    );

    /**
     * @brief Retrieve window size.
     */
    void (*get_window_size)(
        size_t window_id,
        int *window_width,
        int *window_height
    );

    /**
     * @brief Request window redraw/update.
     */
    void (*request_window_update)(size_t window_id);

    /**
     * @brief Run platform event loop.
     *
     * @return false when application should exit.
     */
    bool (*run_event_loop)(void);

    /**
     * @brief Swap rendering buffers.
     */
    void (*swap_buffers)(size_t window_id);

    /* ======================== TFT Specific ======================== */

    /**
     * @brief Get TFT context pointer.
     */
    void* (*get_tft_context)(void);

    /**
     * @brief Call backend flush function.
     *
     * Used for pushing drawlist tiles to display.
     */
    void (*call_flush_function_ptr)(
        void (*flush_fn)(
            struct AromaDrawList* list,
            size_t window_id,
            int x, int y,
            int width, int height
        ),
        void* list
    );

    /**
     * @brief Mark vertical tile region dirty (TFT optimization).
     */
    void (*tft_mark_tiles_dirty)(int y, int h);

    /**
     * @brief Set clear color (typically RGB565 for embedded).
     */
    void (*set_clear_color)(uint16_t color);

    /* ======================== Android Core ======================== */

    /**
     * @brief Set Android native app state pointer.
     */
    void (*set_android_app)(void* app_state);

    /**
     * @brief Enable or disable fullscreen mode.
     */
    void (*set_fullscreen)(size_t window_id, bool enabled);

    /**
     * @brief Open a URL using platform default handler.
     */
    void (*open_url)(const char* url);

    /**
     * @brief Send Android intent.
     */
    void (*android_send_intent)(
        int action,
        const char* uri,
        const char* type,
        const void* extras,
        int extra_count
    );

    /**
     * @brief Show virtual keyboard.
     */
    void (*show_keyboard)(void);

    /**
     * @brief Hide virtual keyboard.
     */
    void (*hide_keyboard)(void);

    /**
     * @brief Check Android permission.
     */
    bool (*android_check_permission)(const char* permission_name);

    /**
     * @brief Request Android permissions.
     */
    void (*android_request_permission)(
        const char** permissions,
        int permCount
    );

    /**
     * @brief Show Android toast message.
     */
    void (*android_toast)(const char* msg, bool long_duration);

    /**
     * @brief Open Android system settings.
     */
    void (*android_open_settings)(void);

    /**
     * @brief Vibrate device.
     */
    void (*android_vibrate)(int ms);

    /**
     * @brief Get battery level (0-100).
     */
    int (*android_get_battery_level)(void);

    /* ======================== Connectivity ======================== */

    bool (*android_is_wifi_enabled)(void);
    void (*android_set_wifi_enabled)(bool enabled);

    bool (*android_is_bluetooth_enabled)(void);

    /* ======================== Bluetooth ======================== */

    int (*android_bt_scan)(
        int scan_mode,
        void (*callback)(
            const char* addr,
            const char* name,
            int type,
            int rssi
        )
    );

    void (*android_bt_stop_scan)(void);

    int (*android_bt_get_paired)(
        char out_addrs[][18],
        char out_names[][248],
        int max_devices
    );

    bool (*android_bt_pair)(const char* addr);
    bool (*android_bt_unpair)(const char* addr);
    int  (*android_bt_get_pair_state)(const char* addr);

    bool (*android_bt_connect)(const char* addr);
    bool (*android_bt_connect_with_mode)(const char* addr, int mode);
    void (*android_bt_disconnect)(void);

    void (*android_bt_register_callbacks)(
        void (*device_cb)(const char*, const char*, int, int),
        void (*scan_finished_cb)(void),
        void (*pairing_cb)(bool, const char*, const char*),
        void (*connection_cb)(bool, const char*, int, int),
        void (*data_cb)(const char*, int)
    );

    int  (*android_bt_send)(const char* data, int len);
    bool (*android_bt_is_connected)(void);
    int  (*android_bt_get_device_type)(void);
    const char* (*android_bt_get_device_name)(void);
    int  (*android_bt_get_current_mode)(void);
    const char* (*android_bt_get_mode_name)(void);

    /* ======================== Media & Storage ======================== */

    void (*android_launch_camera)(void);
    void (*android_launch_gallery)(void);
    void* (*android_get_system_service)(const char* service_name);
    const char* (*android_get_internal_path)(void);
    const char* (*android_get_external_path)(void);

    /* ======================== Display Metrics ======================== */

    float (*android_get_density)(void);
    int   (*android_get_density_dpi)(void);
    float (*android_get_scaled_density)(void);

    int (*android_dp_to_px)(int dp);
    int (*android_px_to_dp)(int px);
    int (*android_sp_to_px)(int sp);
    int (*android_px_to_sp)(int px);

    /* Float variants: preserve fractional dp/sp so hairlines, radii and
     * small paddings don't quantize to zero on high-density screens. */
    float (*android_dp_to_px_f)(float dp);
    float (*android_sp_to_px_f)(float sp);
    float (*android_px_to_dp_f)(float px);

    void (*android_get_available_size_dp)(
        int *width_dp,
        int *height_dp
    );

    void (*android_get_screen_size_inches)(
        float *width_inches,
        float *height_inches
    );

    float (*android_get_screen_diagonal_inches)(void);
    const char* (*android_get_screen_size_category)(void);

    float (*android_get_xdpi)(void);
    float (*android_get_ydpi)(void);

    /* ======================== Orientation Control ======================== */

    /**
     * @brief Lock current screen orientation.
     */
    void (*android_lock_orientation)(void);

    /**
     * @brief Unlock screen orientation (allow sensor rotation).
     */
    void (*android_unlock_orientation)(void);

    /**
     * @brief Force portrait orientation.
     */
    void (*android_set_orientation_portrait)(void);

    /**
     * @brief Force landscape orientation.
     */
    void (*android_set_orientation_landscape)(void);

    /**
     * @brief Set orientation to sensor-based (auto-rotate).
     */
    void (*android_set_orientation_sensor)(void);

    /**
     * @brief Get current orientation.
     * @return 1 for portrait, 2 for landscape, -1 if unknown.
     */
    int (*android_get_current_orientation)(void);

    /**
     * @brief Check if orientation is currently locked.
     * @return true if locked, false otherwise.
     */
    bool (*android_is_orientation_locked)(void);

    /* ======================== Vulkan Support ======================== */

    /**
     * @brief Create a Vulkan surface for the given window.
     *
     * @param window_id Window identifier.
     * @param vk_instance Pointer to a VkInstance.
     * @param vk_surface_out Pointer to a VkSurfaceKHR to receive the surface.
     * @return true on success.
     */
    bool (*create_vulkan_surface)(size_t window_id, void* vk_instance, void** vk_surface_out);

    /**
     * @brief Get the required Vulkan instance extensions for this platform.
     *
     * @param count_out Receives the number of extension strings.
     * @return Array of extension name strings (owned by platform, do not free).
     */
    const char** (*get_vulkan_instance_extensions)(uint32_t* count_out);


    /* ======================== Shared Preferences =================== */

    /**
     * @brief Get a string value from shared preferences.
     *
     * @param key Preference key.
     * @param default_value Value to return if key is not found.
     * @return Preference value (owned by platform, do not free).
     */
    const char* (*android_get_preference_string)(const char* key, const char* default_value);

    /**
     * @brief Set a string value in shared preferences.
     *
     * @param key Preference key.
     * @param value Value to set.
     */

    void (*android_set_preference_string)(const char* key, const char* value);

    /** 
     * @brief Get a boolean value from shared preferences.
     * 
     * @param key Preference key.
     * @param default_value Value to return if key is not found.
     * @return Preference value.
     */
    bool (*android_get_preference_bool)(const char* key, bool default_value);

    /**
     * @brief Set a boolean value in shared preferences.
     *
     * @param key Preference key.
     * @param value Value to set.
     */
    void (*android_set_preference_bool)(const char* key, bool value);

    /**
     * @ brief Get an integer value from shared preferences.
     * 
     * @param key Preference key.
     * @param default_value Value to return if key is not found.
     * @return Preference value.
     * 
     */

    int (*android_get_preference_int)(const char* key, int default_value);

    /**
     * @brief Set an integer value in shared preferences.
     *
     * @param key Preference key.
     * @param value Value to set.
     */

    void (*android_set_preference_int)(const char* key, int value);

    /**
     * @brief Get a float value from shared preferences.
     * 
     * @param key Preference key.
     * @param default_value Value to return if key is not found.
     * @return Preference value.
     */
    float (*android_get_preference_float)(const char* key, float default_value);

    /**
     * @brief Set a float value in shared preferences.
     *
     * @param key Preference key.
     * @param value Value to set.
     *
     */
    void (*android_set_preference_float)(const char* key, float value);

    /**
     * @brief Set a long value in shared preferences.
     * 
     * @param key Preference key.
     * @param value Value to set.
     * 
     */
    void (*android_set_preference_long)(const char* key, long value);

    /**
     * @brief Get a long value from shared preferences.
     * 
     * @param key Preference key.
     * @param default_value Value to return if key is not found.
     * @return Preference value.
     */

    long (*android_get_preference_long)(const char* key, long default_value);

    void (*android_request_permission_cb)(
        const char** permissions,
        int permCount,
        void (*result_cb)(const char* permission, bool granted)
    );
    void (*android_pick_image)(void (*result_cb)(const char* path));
    void (*android_capture_photo)(void (*result_cb)(const char* path));
    void (*android_open_document)(const char* mime, void (*result_cb)(const char* path));
    void (*android_notify_channel)(int id, const char* name, const char* desc, int importance);
    void (*android_notify_show)(int id, const char* channel, const char* title, const char* text);
    void (*android_notify_cancel)(int id);
    void (*android_notify_cancel_all)(void);
    bool (*android_notify_enabled)(void);
    void (*android_vibrate_effect)(int ms, int amplitude);
    void (*android_set_clipboard_text)(const char* text);
    const char* (*android_get_clipboard_text)(void);
    void (*android_share_text)(const char* text, const char* title);
    void (*android_set_immersive)(bool enabled);
    void (*android_set_keep_screen_on)(bool enabled);
    const char* (*android_get_locale_tag)(void);
    const char* (*android_get_timezone_id)(void);
    const char* (*android_get_manufacturer)(void);
    const char* (*android_get_model)(void);
    const char* (*android_get_os_version)(void);
    int (*android_get_sdk_int)(void);
    const char* (*android_get_package_name)(void);
    const char* (*android_get_version_name)(void);
    long (*android_get_version_code)(void);
    long (*android_get_install_time)(void);
    long (*android_get_memory_avail_mb)(void);
    bool (*android_is_memory_low)(void);
    bool (*android_is_network_connected)(void);
    int (*android_get_network_type)(void);
    bool (*android_is_charging)(void);
    bool (*android_is_interactive)(void);
    const char* (*android_get_network_operator)(void);
    void (*android_wakelock_acquire)(long timeout_ms);
    void (*android_wakelock_release)(void);
    bool (*android_set_torch_enabled)(bool enabled);
    void (*android_tts_speak)(const char* text);
    void (*android_tts_stop)(void);
    bool (*android_tts_is_speaking)(void);
    void (*android_sensor_register_callbacks)(void (*cb)(int type, float x, float y, float z, long long timestamp_ns));
    void (*android_sensor_start)(int type, int rate_us, void (*cb)(int type, float x, float y, float z, long long timestamp_ns));
    void (*android_sensor_stop)(int type);
    bool (*android_sensor_available)(int type);
    void (*android_btle_scan)(int timeout_ms, const char* service_uuids);
    void (*android_btle_stop_scan)(void);
    void (*android_btle_register_callbacks)(
        void (*device_cb)(const char*, const char*, int),
        void (*scan_finished_cb)(void),
        void (*connection_cb)(const char*, int, bool),
        void (*services_cb)(const char*, const char*),
        void (*data_cb)(const char*, const char*, const char*, int),
        void (*write_cb)(const char*, const char*, int)
    );
    void (*android_btle_connect)(const char* addr);
    void (*android_btle_disconnect)(void);
    bool (*android_btle_is_connected)(void);
    bool (*android_btle_discover)(void);
    bool (*android_btle_read)(const char* service_uuid, const char* char_uuid);
    bool (*android_btle_write)(const char* service_uuid, const char* char_uuid, const char* data, int len, int write_type);
    bool (*android_btle_notify)(const char* service_uuid, const char* char_uuid, bool enable);

    void (*android_nfc_register)(void (*cb)(const char* payload));
    void (*android_nfc_start)(void);
    void (*android_nfc_stop)(void);
    bool (*android_nfc_available)(void);
    bool (*android_nfc_enabled)(void);
    void (*android_biometric_register)(void (*cb)(bool success));
    int (*android_biometric_available)(void);
    void (*android_biometric_authenticate)(const char* title, const char* subtitle);
    void (*android_location_register)(void (*cb)(double lat, double lon, float accuracy, long long time_ms));
    bool (*android_location_available)(void);
    bool (*android_location_start)(long min_time_ms, float min_dist_m);
    void (*android_location_stop)(void);
    bool (*android_record_start)(const char* path);
    bool (*android_record_stop)(void);
    bool (*android_shortcut_add)(const char* id, const char* short_label, const char* long_label);
    bool (*android_shortcut_remove)(const char* id);
    int (*android_shortcut_count)(void);
    bool (*android_pip_enter)(int w, int h);
    bool (*android_pip_available)(void);
    bool (*android_wallpaper_set_image)(const char* path);
    int (*android_volume_music_get)(void);
    int (*android_volume_music_max)(void);
    void (*android_volume_music_set)(int level);
    int (*android_ringer_get)(void);
    long (*android_storage_free_mb)(void);
    long (*android_storage_total_mb)(void);
    int (*android_sysbar_status_px)(void);
    int (*android_sysbar_nav_px)(void);
    bool (*android_keyboard_visible)(void);
    bool (*android_app_installed)(const char* pkg);
    bool (*android_app_open)(const char* pkg);
    void (*android_contacts_pick)(void (*cb)(const char* info));
    void (*android_shot_capture)(void (*cb)(const char* path));
    bool (*android_http_fetch)(const char* url, const char* dest_path);
    bool (*android_open_url)(const char* url);
    bool (*android_ir_available)(void);
    bool (*android_ir_transmit)(int freq_hz, const int* pattern, int pattern_len);

    bool (*android_asset_exists)(const char* name);
    long (*android_asset_size)(const char* name);
    long (*android_asset_read)(const char* name, char* out, long max_len);
    int (*android_asset_list)(const char* dir, char out_names[][256], int max);
    const char* (*android_asset_cache_path)(const char* name);

    void* (*get_native_window_ptr)(size_t window_id);
    void* (*get_native_display_ptr)(void);
        void (*set_use_surfaceless)(bool use_surfaceless);  // For GLFW backend to enable surfaceless mode if supported
    /* ======================== Offscreen Rendering ======================== */
    void (*set_offscreen_mode)(bool offscreen);
    void (*read_pixels)(size_t window_id, void* buffer, int width, int height);

} AromaPlatformInterface;


/**
 * @brief Retrieve active platform interface.
 *
 * @return Pointer to current platform backend implementation.
 */
AromaPlatformInterface* aroma_get_platform_interface(void);

/**
 * @brief OpenGL/GLPS backend instance.
 */
extern AromaPlatformInterface aroma_platform_glps;

/**
 * @brief TFT (embedded) backend instance.
 */
extern AromaPlatformInterface aroma_platform_tft;

/**
 * @brief Android backend instance.
 */
extern AromaPlatformInterface aroma_platform_android;

extern AromaPlatformInterface aroma_platform_glfw;

#ifdef __EMSCRIPTEN__
extern AromaPlatformInterface aroma_platform_emscripten;
#endif

#ifdef __cplusplus
}
#endif

#endif