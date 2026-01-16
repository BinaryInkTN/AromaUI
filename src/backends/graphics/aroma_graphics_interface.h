#ifndef AROMA_GRAPHICS_INTERFACE_H
#define AROMA_GRAPHICS_INTERFACE_H

typedef enum AromaGraphicsBackendType {
    GRAPHICS_BACKEND_OPENGL,
    GRAPHICS_BACKEND_SOFTWARE,
    GRAPHICS_BACKEND_TFT_ESPI,
    GRAPHICS_BACKEND_STM_SPI
} AromaGraphicsBackendType;


typedef struct AromaGraphicsInterface {
    int (*initialize)(void);
    void (*shutdown)(void);
    void* (*draw_rectangle)(int width, int height);
    void (*fill_rectangle)(void* surface);
} AromaGraphicsInterface;


#endif