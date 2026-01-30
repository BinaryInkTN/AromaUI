#ifndef AROMA_ABI_H
#define AROMA_ABI_H

#include <stddef.h>

typedef enum AromaPlatformBackendType {
    PLATFORM_BACKEND_GLPS,
    PLATFORM_BACKEND_TFT_ESPI
} AromaPlatformBackendType;


typedef enum AromaGraphicsBackendType {
    GRAPHICS_BACKEND_GLES3,
    GRAPHICS_BACKEND_SOFTWARE,
    GRAPHICS_BACKEND_TFT_ESPI,
    GRAPHICS_BACKEND_STM_SPI
} AromaGraphicsBackendType;


typedef struct AromaGraphicsInterface AromaGraphicsInterface;
typedef struct AromaPlatformInterface AromaPlatformInterface;
typedef struct AromaFont AromaFont;

typedef struct AromaBackendABI {
    void (*set_graphics_backend_type)(AromaGraphicsBackendType type);
    void (*set_platform_backend_type)(AromaPlatformBackendType type);
    AromaGraphicsBackendType (*get_graphics_backend_type)(void);
    AromaGraphicsInterface* (*get_graphics_interface)(void);
    AromaPlatformInterface* (*get_platform_interface)(void);
} AromaBackendABI;

AromaGraphicsBackendType aroma_get_graphics_backend_type(void);

extern AromaBackendABI aroma_backend_abi;

void aroma_gles3_load_font_for_window(size_t window_id, AromaFont* font);

#endif
