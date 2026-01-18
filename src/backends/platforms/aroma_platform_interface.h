#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum AromaPlatformBackendType {
    PLATFORM_BACKEND_GLPS,
} AromaPlatformBackendType;

typedef struct AromaPlatformInterface {

    int (*initialize)(void);

    size_t (*create_window)(const char* title, int x, int y, int width, int height);
    void (*make_context_current)(size_t window_id);
    void (*set_window_update_callback)(void (*callback)(size_t window_id, void *data), void* data);
    void (*get_window_size)(size_t window_id, int *window_width, int *window_height);
    void (*request_window_update)(size_t window_id);

    bool (*run_event_loop)(void);
    void (*swap_buffers)(size_t window_id);

    void (*shutdown)(void);
} AromaPlatformInterface;

extern AromaPlatformInterface aroma_platform_glps;

#endif

