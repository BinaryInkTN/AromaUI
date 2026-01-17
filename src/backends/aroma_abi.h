#ifndef AROMA_ABI_H
#define AROMA_ABI_H

#include <stddef.h>

typedef enum AromaGraphicsBackendType AromaGraphicsBackendType;
typedef enum AromaPlatformBackendType AromaPlatformBackendType;
typedef struct AromaGraphicsInterface AromaGraphicsInterface;
typedef struct AromaPlatformInterface AromaPlatformInterface;
typedef struct AromaFont AromaFont;

typedef struct AromaBackendABI {
    void (*set_graphics_backend_type)(AromaGraphicsBackendType type);
    void (*set_platform_backend_type)(AromaPlatformBackendType type);
    AromaGraphicsInterface* (*get_graphics_interface)(void);
    AromaPlatformInterface* (*get_platform_interface)(void);
} AromaBackendABI;

extern AromaBackendABI aroma_backend_abi;

// GLES3 specific functions
void aroma_gles3_load_font_for_window(size_t window_id, AromaFont* font);

#endif