#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H
#include <stdint.h>
#include <stdbool.h>
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
    uint16_t (*create_window)(const char* title, int x, int y, int width, int height);

    // Run the main event loop of the backend.
    bool (*run_event_loop)(void);

    // Shutdown the backend.
    void (*shutdown)(void);
} AromaPlatformInterface;

extern AromaPlatformInterface aroma_platform_glps;

#endif
