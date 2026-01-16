#ifndef AROMA_BACKEND_INTERFACE_H
#define AROMA_BACKEND_INTERFACE_H

// This header defines the interface for aroma backend implementations.
typedef struct AromaBackendInterface {
    // Initialize the backend. Returns 0 on success, non-zero on failure.
    int (*initialize)(void);
    // Create a window with specified parameters.
    void (*create_window)(const char* title, int x, int y, int width, int height);
    // Shutdown the backend, releasing any allocated resources.
    void (*shutdown)(void);
} AromaBackendInterface;

#endif