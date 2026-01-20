#include "aroma.h"
#include <stdio.h>
#include <unistd.h>

static AromaFont* font = NULL;
static AromaNode* checkbox = NULL;

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;

    aroma_ui_begin_frame(window_id);
    aroma_ui_render_dirty_window(window_id, 0xFFFBFE);
    if (font) {
        aroma_graphics_render_text(window_id, font, "Checkbox", 40, 50, 0x6750A4);
    }

    aroma_ui_end_frame(window_id);
    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 16);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16);

    AromaWindow* window = aroma_ui_create_window("Widget Example - Checkbox", 480, 320);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    checkbox = aroma_checkbox_create((AromaNode*)window, "Enable notifications", 40, 90, 280, 40);
    if (checkbox && font) {
        aroma_checkbox_set_font(checkbox, font);
        aroma_checkbox_set_checked(checkbox, true);
        aroma_node_invalidate(checkbox);
    }

    aroma_platform_set_window_update_callback(window_update_callback, NULL);
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
