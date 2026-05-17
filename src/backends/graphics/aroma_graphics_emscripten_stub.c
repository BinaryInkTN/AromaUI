#include "aroma_graphics_interface.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static int stub_setup_shared_window_resources(void) { return 0; }
static int stub_setup_separate_window_resources(size_t window_id) { (void)window_id; return 0; }
static void stub_shutdown(void) { }
static void stub_clear(size_t window_id, uint32_t color) { (void)window_id; (void)color; }
static void stub_draw_rectangle(size_t window_id, int x, int y, int width, int height) { (void)window_id; (void)x; (void)y; (void)width; (void)height; }
static void stub_fill_rectangle(size_t window_id, int x, int y, int width, int height, uint32_t color, bool isRounded, float cornerRadius) { (void)window_id; (void)x; (void)y; (void)width; (void)height; (void)color; (void)isRounded; (void)cornerRadius; }
static void stub_draw_hollow_rectangle(size_t window_id, int x, int y, int width, int height, uint32_t color, int border_width, bool isRounded, float cornerRadius) { (void)window_id; (void)x; (void)y; (void)width; (void)height; (void)color; (void)border_width; (void)isRounded; (void)cornerRadius; }
static void stub_draw_arc(size_t window_id, int cx, int cy, int radius, float start_angle, float end_angle, uint32_t color, int thickness) { (void)window_id; (void)cx; (void)cy; (void)radius; (void)start_angle; (void)end_angle; (void)color; (void)thickness; }
static void stub_render_text(size_t window_id, AromaFont *font, const char *text, int x, int y, uint32_t color, float scale) { (void)window_id; (void)font; (void)text; (void)x; (void)y; (void)color; (void)scale; }
static float stub_measure_text(size_t window_id, AromaFont *font, const char *text, float scale) { (void)window_id; (void)font; (void)text; (void)scale; return 0.0f; }
static void stub_unload_image(unsigned int texture_id) { (void)texture_id; }
static unsigned int stub_load_image(const char *image_path) { (void)image_path; return 0u; }
static unsigned int stub_load_image_from_rgba(unsigned char *data, int width, int height) { (void)data; (void)width; (void)height; return 0u; }
static unsigned int stub_load_image_from_memory(unsigned char *data, unsigned long binary_length) { (void)data; (void)binary_length; return 0u; }
static void stub_draw_image(size_t window_id, int x, int y, int width, int height, unsigned int texture_id) { (void)window_id; (void)x; (void)y; (void)width; (void)height; (void)texture_id; }
static void stub_graphics_set_tft_context(void *tft) { (void)tft; }
static void stub_graphics_set_sprite_mode(bool enable, void *sprite) { (void)enable; (void)sprite; }
static void stub_graphics_set_clip(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void stub_graphics_clear_clip(void) { }
static void stub_graphics_flush(void) { }
static void stub_notify_dirty_region(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static bool stub_get_pending_dirty_rect(int *x, int *y, int *w, int *h) { (void)x; (void)y; (void)w; (void)h; return false; }

AromaGraphicsInterface aroma_graphics_vulkan = {
    .setup_shared_window_resources = stub_setup_shared_window_resources,
    .setup_separate_window_resources = stub_setup_separate_window_resources,
    .shutdown = stub_shutdown,
    .clear = stub_clear,
    .draw_rectangle = stub_draw_rectangle,
    .fill_rectangle = stub_fill_rectangle,
    .draw_hollow_rectangle = stub_draw_hollow_rectangle,
    .draw_arc = stub_draw_arc,
    .render_text = stub_render_text,
    .measure_text = stub_measure_text,
    .unload_image = stub_unload_image,
    .load_image = stub_load_image,
    .load_image_from_rgba = stub_load_image_from_rgba,
    .load_image_from_memory = stub_load_image_from_memory,
    .draw_image = stub_draw_image,
    .graphics_set_tft_context = stub_graphics_set_tft_context,
    .graphics_set_sprite_mode = stub_graphics_set_sprite_mode,
    .graphics_set_clip = stub_graphics_set_clip,
    .graphics_clear_clip = stub_graphics_clear_clip,
    .graphics_flush = stub_graphics_flush,
    .notify_dirty_region = stub_notify_dirty_region,
    .get_pending_dirty_rect = stub_get_pending_dirty_rect
};
