#include "aroma.h"
#include <stdio.h>
#include <unistd.h>

static AromaFont* font = NULL;
static AromaNode* radio1 = NULL;
static AromaNode* radio2 = NULL;
static AromaNode* radio3 = NULL;

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;

    aroma_graphics_clear(window_id, 0xFFFBFE);

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count > 0) {
        if (radio1) aroma_radiobutton_draw(radio1, window_id);
        if (radio2) aroma_radiobutton_draw(radio2, window_id);
        if (radio3) aroma_radiobutton_draw(radio3, window_id);
        if (font) {
            aroma_graphics_render_text(window_id, font, "Radio Buttons", 40, 50, 0x6750A4);
        }
        aroma_dirty_list_clear();
    }

    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 16);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16);

    AromaWindow* window = aroma_ui_create_window("Widget Example - Radio Button", 480, 320);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    radio1 = aroma_radiobutton_create((AromaNode*)window, "Small", 40, 90, 220, 36, 1);
    radio2 = aroma_radiobutton_create((AromaNode*)window, "Medium", 40, 130, 220, 36, 1);
    radio3 = aroma_radiobutton_create((AromaNode*)window, "Large", 40, 170, 220, 36, 1);

    if (font) {
        if (radio1) aroma_radiobutton_set_font(radio1, font);
        if (radio2) aroma_radiobutton_set_font(radio2, font);
        if (radio3) aroma_radiobutton_set_font(radio3, font);
    }

    if (radio2) aroma_radiobutton_set_selected(radio2, true);
    if (radio1) aroma_node_invalidate(radio1);
    if (radio2) aroma_node_invalidate(radio2);
    if (radio3) aroma_node_invalidate(radio3);

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
