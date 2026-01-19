#include "aroma.h"
#include <stdio.h>
#include <unistd.h>

static AromaFont* font = NULL;
static AromaButton* button = NULL;
static int clicks = 0;

static bool on_click(AromaButton* btn, void* user_data) {
    (void)btn; (void)user_data;
    clicks++;
    printf("Clicks: %d\n", clicks);
    aroma_ui_request_redraw(NULL);
    return true;
}

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;
    aroma_graphics_clear(window_id, 0xFFFBFE);

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count > 0) {
        if (button) aroma_button_draw((AromaNode*)button, window_id);
        if (font) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Clicks: %d", clicks);
            aroma_graphics_render_text(window_id, font, buf, 40, 160, 0x6750A4);
        }
        aroma_dirty_list_clear();
    }

    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 16);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16);

    AromaWindow* window = aroma_ui_create_window("Widget Example - Button", 360, 240);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    button = aroma_ui_create_button(window, "Click", 40, 90, 120, 40);
    if (button) {
        aroma_ui_on_button_click(button, on_click, NULL);
        aroma_node_invalidate((AromaNode*)button);
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
