#include "aroma_abi.h"
#include "graphics/aroma_graphics_interface.h"
#include "platforms/aroma_platform_interface.h"
#include <aroma_drawlist.h>
#include <stdatomic.h>
#include <stddef.h>

static _Atomic(AromaGraphicsBackendType) current_graphics_backend = GRAPHICS_BACKEND_GLES3;
static _Atomic(AromaPlatformBackendType) current_platform_backend = PLATFORM_BACKEND_GLPS;

static AromaGraphicsInterface* get_real_graphics_interface(void) {
    AromaGraphicsBackendType backend = atomic_load(&current_graphics_backend);
    switch (backend) {
        case GRAPHICS_BACKEND_GLES3:
            return &aroma_graphics_gles3;
        default:
            return NULL;
    }
}

static void drawlist_proxy_clear(size_t window_id, uint32_t color)
{
    AromaDrawList* list = aroma_drawlist_get_active();
    if (list) {
        aroma_drawlist_cmd_clear(list, color);
        return;
    }
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->clear) {
        real->clear(window_id, color);
    }
}

static void drawlist_proxy_fill_rectangle(size_t window_id, int x, int y, int width, int height,
                                          uint32_t color, bool isRounded, float cornerRadius)
{
    AromaDrawList* list = aroma_drawlist_get_active();
    if (list) {
        aroma_drawlist_cmd_fill_rect(list, x, y, width, height, color, isRounded, cornerRadius);
        return;
    }
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->fill_rectangle) {
        real->fill_rectangle(window_id, x, y, width, height, color, isRounded, cornerRadius);
    }
}

static void drawlist_proxy_draw_hollow_rectangle(size_t window_id, int x, int y, int width, int height,
                                                 uint32_t color, int border_width, bool isRounded, float cornerRadius)
{
    AromaDrawList* list = aroma_drawlist_get_active();
    if (list) {
        aroma_drawlist_cmd_hollow_rect(list, x, y, width, height, color, border_width, isRounded, cornerRadius);
        return;
    }
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->draw_hollow_rectangle) {
        real->draw_hollow_rectangle(window_id, x, y, width, height, color, border_width, isRounded, cornerRadius);
    }
}

static void drawlist_proxy_draw_arc(size_t window_id, int cx, int cy, int radius,
                                    float start_angle, float end_angle, uint32_t color, int thickness)
{
    AromaDrawList* list = aroma_drawlist_get_active();
    if (list) {
        aroma_drawlist_cmd_arc(list, cx, cy, radius, start_angle, end_angle, color, thickness);
        return;
    }
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->draw_arc) {
        real->draw_arc(window_id, cx, cy, radius, start_angle, end_angle, color, thickness);
    }
}

static void drawlist_proxy_render_text(size_t window_id, AromaFont* font, const char* text, int x, int y, uint32_t color)
{
    AromaDrawList* list = aroma_drawlist_get_active();
    if (list) {
        aroma_drawlist_cmd_text(list, font, text, x, y, color);
        return;
    }
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->render_text) {
        real->render_text(window_id, font, text, x, y, color);
    }
}

static float drawlist_proxy_measure_text(size_t window_id, AromaFont* font, const char* text)
{
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->measure_text) {
        return real->measure_text(window_id, font, text);
    }
    return 0.0f;
}

static void drawlist_proxy_draw_rectangle(size_t window_id, int x, int y, int width, int height)
{
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->draw_rectangle) {
        real->draw_rectangle(window_id, x, y, width, height);
    }
}

static int drawlist_proxy_setup_shared_window_resources(void)
{
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->setup_shared_window_resources) {
        return real->setup_shared_window_resources();
    }
    return 0;
}

static int drawlist_proxy_setup_separate_window_resources(size_t window_id)
{
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->setup_separate_window_resources) {
        return real->setup_separate_window_resources(window_id);
    }
    return 0;
}

static void drawlist_proxy_shutdown(void)
{
    AromaGraphicsInterface* real = get_real_graphics_interface();
    if (real && real->shutdown) {
        real->shutdown();
    }
}

static AromaGraphicsInterface drawlist_proxy = {
    .setup_shared_window_resources = drawlist_proxy_setup_shared_window_resources,
    .setup_separate_window_resources = drawlist_proxy_setup_separate_window_resources,
    .clear = drawlist_proxy_clear,
    .draw_rectangle = drawlist_proxy_draw_rectangle,
    .fill_rectangle = drawlist_proxy_fill_rectangle,
    .draw_hollow_rectangle = drawlist_proxy_draw_hollow_rectangle,
    .draw_arc = drawlist_proxy_draw_arc,
    .render_text = drawlist_proxy_render_text,
    .measure_text = drawlist_proxy_measure_text,
    .shutdown = drawlist_proxy_shutdown
};

void set_graphics_backend_type(AromaGraphicsBackendType type) {
    atomic_store(&current_graphics_backend, type);
}
    
void set_platform_backend_type(AromaPlatformBackendType type) {
    atomic_store(&current_platform_backend, type);
}

AromaGraphicsBackendType aroma_get_graphics_backend_type(void) {
    return atomic_load(&current_graphics_backend);
}

AromaGraphicsInterface* get_graphics_interface(void) {
    return &drawlist_proxy;
}

AromaPlatformInterface* get_platform_interface(void) {
    AromaPlatformBackendType backend = atomic_load(&current_platform_backend);
    switch (backend) {
        case PLATFORM_BACKEND_GLPS:
            return &aroma_platform_glps;
        default:
            return NULL;
    }    
}

AromaBackendABI aroma_backend_abi = {
    .set_graphics_backend_type = set_graphics_backend_type,
    .set_platform_backend_type = set_platform_backend_type,
    .get_graphics_backend_type = aroma_get_graphics_backend_type,
    .get_graphics_interface = get_graphics_interface,
    .get_platform_interface = get_platform_interface
};