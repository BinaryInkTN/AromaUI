#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/**
 * @file aroma_backend_interface.h
 * @brief Definition of the Aroma Backend Interface.
 *
 * This header defines the interface for different backend implementations
 * in the Aroma UI framework. Each backend must implement the functions
 * defined in the AromaPlatformInterface structure.
 */

typedef enum AromaPlatformBackendType {
    PLATFORM_BACKEND_GLPS,
} AromaPlatformBackendType;

// This header defines the interface for aroma backend implementations.
typedef struct AromaPlatformInterface {
    // Initialize the backend. Returns 0 on success, non-zero on failure.
    int (*initialize)(void);
    // Create a window with specified parameters.
    size_t (*create_window)(const char* title, int x, int y, int width, int height);
    void (*make_context_current)(size_t window_id);
    void (*set_window_update_callback)(void (*callback)(size_t window_id, void *data), void* data);
    void (*get_window_size)(size_t window_id, int *window_width, int *window_height);
    void (*request_window_update)(size_t window_id);
    // Run the main event loop of the backend.
    bool (*run_event_loop)(void);
    void (*swap_buffers)(size_t window_id);
    // Shutdown the backend.
    void (*shutdown)(void);
} AromaPlatformInterface;

extern AromaPlatformInterface aroma_platform_glps;

#endif
