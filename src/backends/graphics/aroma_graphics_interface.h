#ifndef AROMA_GRAPHICS_INTERFACE_H
#define AROMA_GRAPHICS_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum AromaGraphicsBackendType {
    GRAPHICS_BACKEND_GLES3,
    GRAPHICS_BACKEND_SOFTWARE,
    GRAPHICS_BACKEND_TFT_ESPI,
    GRAPHICS_BACKEND_STM_SPI
} AromaGraphicsBackendType;


typedef struct AromaGraphicsInterface {
    int (*setup_shared_window_resources)(void);
    int (*setup_separate_window_resources)(size_t window_id);
    void (*clear)(size_t window_id, uint32_t color);
    void* (*draw_rectangle)(int width, int height);
    void (*fill_rectangle)(size_t window_id, int x, int y, int width, int height, uint32_t color, bool isRounded, float cornerRadius);
    void (*shutdown)(void);
} AromaGraphicsInterface;

// Graphics backend implementations
extern AromaGraphicsInterface aroma_graphics_gles3;

#endif