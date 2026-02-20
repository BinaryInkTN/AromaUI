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
    void (*set_window_update_callback)(void (*callback)(size_t window_id, void *data), void* data);
    void (*get_window_size)(size_t window_id, int *window_width, int *window_height);
    void (*request_window_update)(size_t window_id);
    bool (*run_event_loop)(void);
    void (*swap_buffers)(size_t window_id);
    void* (*get_tft_context)(void);
    void (*call_flush_function_ptr)(void (*flush_fn)(struct AromaDrawList* list, size_t window_id, int x, int y, int width, int height), void* list);    
    void (*tft_mark_tiles_dirty)(int y, int h);
    void (*set_clear_color)(uint16_t color);
    void (*set_android_app)(void* app_state);
    void (*set_fullscreen)(size_t window_id, bool enabled);
    void (*open_url)(const char* url);
    void (*android_send_intent)(int action, const char* uri, const char* type, const void* extras, int extra_count);
    void (*show_keyboard)(void);
    void (*hide_keyboard)(void);
    bool (*android_check_permission)(const char* permission_name);
    void (*android_request_permission)(const char** permissions, int permCount);
    void (*android_toast)(const char* msg, bool long_duration);
    void (*android_open_settings)(void);
    void (*android_vibrate)(int ms);
    int (*android_get_battery_level)(void);
    bool (*android_is_wifi_enabled)(void);
    void (*android_set_wifi_enabled)(bool enabled);
    bool (*android_is_bluetooth_enabled)(void);
    int (*android_bt_scan)(int scan_mode, void (*callback)(const char* addr, const char* name, int type, int rssi));
    void (*android_bt_stop_scan)(void);
    int (*android_bt_get_paired)(char out_addrs[][18], char out_names[][248], int max_devices);
    bool (*android_bt_pair)(const char* addr);
    bool (*android_bt_unpair)(const char* addr);
    int (*android_bt_get_pair_state)(const char* addr);
    bool (*android_bt_connect)(const char* addr);
    bool (*android_bt_connect_with_mode)(const char* addr, int mode);
    void (*android_bt_disconnect)(void);
    void (*android_bt_register_callbacks)(void (*device_cb)(const char*, const char*, int, int),
                                      void (*scan_finished_cb)(void),
                                      void (*pairing_cb)(bool, const char*, const char*),
                                      void (*connection_cb)(bool, const char*, int, int),
                                      void (*data_cb)(const char*, int));
    int (*android_bt_send)(const char* data, int len);
    bool (*android_bt_is_connected)(void);
    int (*android_bt_get_device_type)(void);
    const char* (*android_bt_get_device_name)(void);
    int (*android_bt_get_current_mode)(void);
    const char* (*android_bt_get_mode_name)(void);
    void (*android_launch_camera)(void);
    void (*android_launch_gallery)(void);
    void* (*android_get_system_service)(const char* service_name);
    const char* (*android_get_internal_path)(void);
    const char* (*android_get_external_path)(void);

    float (*android_get_density)(void);
    int (*android_get_density_dpi)(void);
    float (*android_get_scaled_density)(void);
    int (*android_dp_to_px)(int dp);
    int (*android_px_to_dp)(int px);
    int (*android_sp_to_px)(int sp);
    int (*android_px_to_sp)(int px);
    void (*android_get_available_size_dp)(int *width_dp, int *height_dp);
    void (*android_get_screen_size_inches)(float *width_inches, float *height_inches);
    float (*android_get_screen_diagonal_inches)(void);
    const char* (*android_get_screen_size_category)(void);
    float (*android_get_xdpi)(void);
    float (*android_get_ydpi)(void);

} AromaPlatformInterface;

AromaPlatformInterface* aroma_get_platform_interface(void);

extern AromaPlatformInterface aroma_platform_glps;
extern AromaPlatformInterface aroma_platform_tft;
extern AromaPlatformInterface aroma_platform_android;

#ifdef __cplusplus
}
#endif

#endif