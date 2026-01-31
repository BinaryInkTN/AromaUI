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

    void (*tft_mark_tiles_dirty)(int y, int h);

} AromaPlatformInterface;

extern AromaPlatformInterface aroma_platform_tft;

#ifndef ESP32
extern AromaPlatformInterface aroma_platform_glps;
#endif

#ifdef __cplusplus
}
#endif

#endif 

