#include "aroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static AromaFont* font = NULL;

void window_update_callback(size_t window_id, void* data) {
    (void)data;
    printf("window_update_callback called\n");
    fflush(stdout);

    if (!aroma_ui_consume_redraw()) {
        printf("  -> not dirty, returning\n");
        fflush(stdout);
        return;
    }

    printf("  -> clearing\n");
    fflush(stdout);
    aroma_graphics_clear(window_id, 0xF0F0F0);

    if (font) {
        printf("  -> rendering text\n");
        fflush(stdout);
        aroma_graphics_render_text(window_id, font, "Hello World!", 100, 100, 0x333333);
        printf("  -> text rendered\n");
        fflush(stdout);
    }

    aroma_dirty_list_clear();

    printf("  -> swapping buffers\n");
    fflush(stdout);
    aroma_graphics_swap_buffers(window_id);
    printf("  -> buffers swapped\n");
    fflush(stdout);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (!aroma_ui_init()) {
        fprintf(stderr, "Failed to initialize Aroma UI\n");
        return 1;
    }

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf", 16);
    if (!font) {
        font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16);
    }

    AromaWindow* window = aroma_ui_create_window("Minimal - No Buttons", 640, 480);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        aroma_ui_shutdown();
        return 1;
    }

    printf("Window created successfully\n");
    fflush(stdout);

    printf("About to set root\n");
    fflush(stdout);
    aroma_event_set_root((AromaNode*)window);
    printf("Root set\n");
    fflush(stdout);

    if (font) {
        printf("About to prepare font\n");
        fflush(stdout);
        aroma_ui_prepare_font_for_window(0, font);
        printf("Font prepared\n");
        fflush(stdout);
    }

    printf("About to set callback\n");
    fflush(stdout);
    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);
    printf("Callback set\n");
    fflush(stdout);

    printf("Starting event loop\n");
    fflush(stdout);

    int frame_count = 0;
    while (aroma_ui_is_running() && frame_count < 100) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(4000);
        frame_count++;
    }

    printf("Event loop exited after %d frames\n", frame_count);
    fflush(stdout);

    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();

    return 0;
}

