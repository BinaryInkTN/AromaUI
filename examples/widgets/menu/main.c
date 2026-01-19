#include "aroma.h"
#include <unistd.h>

static AromaFont* font = NULL;
static AromaNode* menu = NULL;

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;
    aroma_graphics_clear(window_id, 0xFFFBFE);

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count > 0) {
        if (menu) aroma_menu_draw(menu, window_id);
        aroma_dirty_list_clear();
    }

    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 14);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 14);

    AromaWindow* window = aroma_ui_create_window("Widget Example - Menu", 360, 260);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    menu = aroma_menu_create((AromaNode*)window, 40, 60);
    if (menu) {
        aroma_menu_set_font(menu, font);
        aroma_menu_add_item(menu, "New", NULL, NULL);
        aroma_menu_add_item(menu, "Open", NULL, NULL);
        aroma_menu_add_separator(menu);
        aroma_menu_add_item(menu, "Settings", NULL, NULL);
        aroma_menu_add_item(menu, "Exit", NULL, NULL);
        aroma_menu_show(menu);
        aroma_node_invalidate(menu);
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
