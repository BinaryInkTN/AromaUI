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
    void (*call_flush_function_ptr)(void (*flush_fn)(struct AromaDrawList* list, size_t window_id, int x, int y, int width, int height), void* list);    


    // Dirty region set
    // [TFT]
    void (*tft_mark_tiles_dirty)(int y, int h);
    void (*set_clear_color)(uint16_t color);

    // [Android]
    void (*set_android_app)(void* app_state);
    void (*set_fullscreen)(size_t window_id, bool enabled);
    void (*open_url)(const char* url);
    // [Android] Generic Intent
    void (*android_send_intent)(int action, const char* uri, const char* type, const void* extras, int extra_count);

    void (*show_keyboard)(void);
    void (*hide_keyboard)(void);

    // [Android Extensions]
    bool (*android_check_permission)(const char* permission_name);
    void (*android_request_permission)(const char* permission_name);
    void (*android_toast)(const char* msg, bool long_duration);
    void (*android_open_settings)(void);
    void (*android_vibrate)(int ms);
    int (*android_get_battery_level)(void);
    bool (*android_is_wifi_enabled)(void);
    void (*android_set_wifi_enabled)(bool enabled);
    bool (*android_is_bluetooth_enabled)(void);
    void (*android_launch_camera)(void);
    void (*android_launch_gallery)(void);
    void* (*android_get_system_service)(const char* service_name);

    // [Android Utils]
    const char* (*android_get_internal_path)(void);
    const char* (*android_get_external_path)(void);

} AromaPlatformInterface;

AromaPlatformInterface* aroma_get_platform_interface(void);

extern AromaPlatformInterface aroma_platform_glps;

extern AromaPlatformInterface aroma_platform_tft;

extern AromaPlatformInterface aroma_platform_android;

#ifdef __cplusplus
}
#endif

#endif 

