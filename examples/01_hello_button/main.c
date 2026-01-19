#include "aroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int click_count = 0;
static AromaButton* btn_primary = NULL;
static AromaButton* btn_secondary = NULL;
static AromaButton* btn_small = NULL;
static AromaFont* font = NULL;

static bool on_primary_clicked(AromaButton* button, void* user_data) {
    (void)button;
    (void)user_data;
    click_count++;
    printf("Clicks: %d\n", click_count);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_reset_clicked(AromaButton* button, void* user_data) {
    (void)button;
    (void)user_data;
    printf("Reset from: %d\n", click_count);
    click_count = 0;
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_exit_clicked(AromaButton* button, void* user_data) {
    (void)button;
    (void)user_data;
    exit(0);
    return true;
}

void window_update_callback(size_t window_id, void* data) {
    (void)data;

    if (!aroma_ui_consume_redraw()) return;

    aroma_graphics_clear(window_id, 0xF0F0F0);

    size_t dirty_count = 0;
    AromaNode** dirty_nodes = aroma_dirty_list_get(&dirty_count);

    if (dirty_count > 0) {
        if (btn_primary) aroma_button_draw((AromaNode*)btn_primary, window_id);
        if (btn_secondary) aroma_button_draw((AromaNode*)btn_secondary, window_id);
        if (btn_small) aroma_button_draw((AromaNode*)btn_small, window_id);

        if (font) {
            aroma_graphics_render_text(window_id, font, "Click Me!", 265, 220, 0xFFFFFF);
            aroma_graphics_render_text(window_id, font, "Reset Counter", 250, 290, 0xFFFFFF);
            aroma_graphics_render_text(window_id, font, "Exit", 553, 32, 0xFFFFFF);

            char buf[64];
            snprintf(buf, sizeof(buf), "Clicks: %d", click_count);
            aroma_graphics_render_text(window_id, font, buf, 100, 120, 0x333333);
            aroma_graphics_render_text(window_id, font, "Windows 7 Aero", 150, 430, 0x0078D7);
        }

        aroma_dirty_list_clear();
    }

    aroma_graphics_swap_buffers(window_id);
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

    AromaWindow* window = aroma_ui_create_window("Material Design 3 - Hello Button", 640, 480);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        aroma_ui_shutdown();
        return 1;
    }

    aroma_event_set_root((AromaNode*)window);
    printf("DEBUG: Event root set\n");
    fflush(stdout);

    if (font) aroma_ui_prepare_font_for_window(0, font);
    printf("DEBUG: Font prepared\n");
    fflush(stdout);

    btn_primary = aroma_ui_create_button(window, "Click Me!", 245, 200, 150, 50);
    printf("DEBUG: Button 1 created at %p\n", (void*)btn_primary);
    fflush(stdout);
    if (btn_primary) {
        printf("DEBUG: Setting callback for button 1\n");
        fflush(stdout);
        aroma_ui_on_button_click(btn_primary, on_primary_clicked, NULL);
        printf("DEBUG: Button 1 callback set\n");
        fflush(stdout);
    }

    btn_secondary = aroma_ui_create_button(window, "Reset Counter", 245, 270, 150, 50);
    printf("DEBUG: Button 2 created at %p\n", (void*)btn_secondary);
    fflush(stdout);
    if (btn_secondary) {
        aroma_ui_on_button_click(btn_secondary, on_reset_clicked, NULL);
    }

    btn_small = aroma_ui_create_button(window, "Exit", 540, 20, 80, 35);
    printf("DEBUG: Button 3 created at %p\n", (void*)btn_small);
    fflush(stdout);
    if (btn_small) {
        aroma_ui_on_button_click(btn_small, on_exit_clicked, NULL);
    }

    printf("DEBUG: All buttons created, setting callback\n");
    fflush(stdout);
    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    printf("DEBUG: Callback set, entering main loop\n");
    fflush(stdout);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(4000);
    }

    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();

    return 0;
}

