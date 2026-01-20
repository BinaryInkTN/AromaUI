#include "aroma.h"
#include "widgets/aroma_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static AromaFont* font = NULL;
static AromaImage* img = NULL;

void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;
    aroma_graphics_clear(window_id, 0xF0F0F0);
    if (font) {
        aroma_graphics_render_text(window_id, font, "Hello World!", 100, 100, 0x333333);
    }
    if (img) {
        // Example: draw image at (200, 200)
        aroma_image_draw((void*)(uintptr_t)window_id, img, 200, 200);
    }
    aroma_dirty_list_clear();
    aroma_graphics_swap_buffers(window_id);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (!aroma_ui_init()) return 1;
    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf", 16);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16);
    img = aroma_image_load("/path/to/your/image.png");
    AromaWindow* window = aroma_ui_create_window("Minimal - Image", 640, 480);
    if (!window) { aroma_ui_shutdown(); return 1; }
    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);
    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);
    int frame_count = 0;
    while (aroma_ui_is_running() && frame_count < 100) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(4000);
        frame_count++;
    }
    aroma_ui_destroy_window(window);
    if (img) aroma_image_free(img);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();
    return 0;
}
