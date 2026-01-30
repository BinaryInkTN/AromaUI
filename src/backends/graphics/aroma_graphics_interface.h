#ifndef AROMA_GRAPHICS_INTERFACE_H
#define AROMA_GRAPHICS_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaFont AromaFont;

typedef struct AromaGraphicsInterface {

    int  (*setup_shared_window_resources)(void);
    int  (*setup_separate_window_resources)(size_t window_id);
    void (*shutdown)(void);

    void (*clear)(size_t window_id, uint32_t color);

    void (*draw_rectangle)(
        size_t window_id,
        int x, int y,
        int width, int height
    );

    void (*fill_rectangle)(
        size_t window_id,
        int x, int y,
        int width, int height,
        uint32_t color,
        bool isRounded,
        float cornerRadius
    );

    void (*draw_hollow_rectangle)(
        size_t window_id,
        int x, int y,
        int width, int height,
        uint32_t color,
        int border_width,
        bool isRounded,
        float cornerRadius
    );

    void (*draw_arc)(
        size_t window_id,
        int cx, int cy,
        int radius,
        float start_angle,
        float end_angle,
        uint32_t color,
        int thickness
    );

    void (*render_text)(
        size_t window_id,
        AromaFont* font,
        const char* text,
        int x, int y,
        uint32_t color,
        float scale
    );

    float (*measure_text)(
        size_t window_id,
        AromaFont* font,
        const char* text,
        float scale
    );

    void (*unload_image)(unsigned int texture_id);
    unsigned int (*load_image)(const char* image_path);
    unsigned int (*load_image_from_memory)(
        unsigned char* data,
        long unsigned int binary_length
    );

    void (*draw_image)(
        size_t window_id,
        int x, int y,
        int width, int height,
        unsigned int texture_id
    );

    void (*graphics_set_tft_context)(void* tft);

    void (*graphics_set_sprite_mode)(
        bool enable,
        void* sprite
    );

} AromaGraphicsInterface;

#ifndef ESP32
extern AromaGraphicsInterface aroma_graphics_gles3;
#endif

extern AromaGraphicsInterface aroma_graphics_tft;

#ifdef __cplusplus
}
#endif

#endif 

