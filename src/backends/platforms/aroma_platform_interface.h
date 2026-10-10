








#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif




typedef struct AromaDrawList AromaDrawList;










typedef struct AromaPlatformInterface {







    int  (*initialize)(void);




    void (*shutdown)(void);













    size_t (*create_window)(
        const char* title,
        int x, int y,
        int width, int height
    );




    void (*make_context_current)(size_t window_id);







    void (*set_window_update_callback)(
        void (*callback)(size_t window_id, void *data),
        void* data
    );




    void (*get_window_size)(
        size_t window_id,
        int *window_width,
        int *window_height
    );




    void (*request_window_update)(size_t window_id);






    bool (*run_event_loop)(void);




    void (*swap_buffers)(size_t window_id);






    void* (*get_tft_context)(void);






    void (*call_flush_function_ptr)(
        void (*flush_fn)(
            struct AromaDrawList* list,
            size_t window_id,
            int x, int y,
            int width, int height
        ),
        void* list
    );




    void (*tft_mark_tiles_dirty)(int y, int h);




    void (*set_clear_color)(uint16_t color);






    void (*set_android_app)(void* app_state);




    void (*set_fullscreen)(size_t window_id, bool enabled);




    void (*open_url)(const char* url);




    void (*android_send_intent)(
        int action,
        const char* uri,
        const char* type,
        const void* extras,
        int extra_count
    );




    void (*show_keyboard)(void);




    void (*hide_keyboard)(void);




    bool (*android_check_permission)(const char* permission_name);




    void (*android_request_permission)(
        const char** permissions,
        int permCount
    );




    void (*android_toast)(const char* msg, bool long_duration);




    void (*android_open_settings)(void);




    void (*android_vibrate)(int ms);




    int (*android_get_battery_level)(void);



    bool (*android_is_wifi_enabled)(void);
    void (*android_set_wifi_enabled)(bool enabled);

    bool (*android_is_bluetooth_enabled)(void);



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



    void (*android_launch_camera)(void);
    void (*android_launch_gallery)(void);
    void* (*android_get_system_service)(const char* service_name);
    const char* (*android_get_internal_path)(void);
    const char* (*android_get_external_path)(void);



    float (*android_get_density)(void);
    int   (*android_get_density_dpi)(void);
    float (*android_get_scaled_density)(void);

    int (*android_dp_to_px)(int dp);
    int (*android_px_to_dp)(int px);
    int (*android_sp_to_px)(int sp);
    int (*android_px_to_sp)(int px);



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






    void (*android_lock_orientation)(void);




    void (*android_unlock_orientation)(void);




    void (*android_set_orientation_portrait)(void);




    void (*android_set_orientation_landscape)(void);




    void (*android_set_orientation_sensor)(void);





    int (*android_get_current_orientation)(void);





    bool (*android_is_orientation_locked)(void);











    bool (*create_vulkan_surface)(size_t window_id, void* vk_instance, void** vk_surface_out);







    const char** (*get_vulkan_instance_extensions)(uint32_t* count_out);











    const char* (*android_get_preference_string)(const char* key, const char* default_value);








    void (*android_set_preference_string)(const char* key, const char* value);








    bool (*android_get_preference_bool)(const char* key, bool default_value);







    void (*android_set_preference_bool)(const char* key, bool value);










    int (*android_get_preference_int)(const char* key, int default_value);








    void (*android_set_preference_int)(const char* key, int value);








    float (*android_get_preference_float)(const char* key, float default_value);








    void (*android_set_preference_float)(const char* key, float value);








    void (*android_set_preference_long)(const char* key, long value);









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
    void (*android_rfid_register)(void (*cb)(const char* uid_hex));
    void (*android_rfid_start)(void);
    void (*android_rfid_stop)(void);
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
        void (*set_use_surfaceless)(bool use_surfaceless);

    void (*set_offscreen_mode)(bool offscreen);
    void (*read_pixels)(size_t window_id, void* buffer, int width, int height);

} AromaPlatformInterface;







AromaPlatformInterface* aroma_get_platform_interface(void);




extern AromaPlatformInterface aroma_platform_glps;




extern AromaPlatformInterface aroma_platform_tft;




extern AromaPlatformInterface aroma_platform_android;

extern AromaPlatformInterface aroma_platform_glfw;

#ifdef __EMSCRIPTEN__
extern AromaPlatformInterface aroma_platform_emscripten;
#endif

#ifdef __cplusplus
}
#endif

#endif