#include "aroma.h"
#include <unistd.h>

static AromaFont* font = NULL;
static AromaNode* listview = NULL;

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;
    aroma_ui_render_dirty_window(window_id, 0xFFFBFE);
    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 14);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 14);

    AromaWindow* window = aroma_ui_create_window("Widget Example - ListView", 480, 320);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    listview = aroma_listview_create((AromaNode*)window, 40, 60, 360, 200);
    if (listview) {
        aroma_listview_set_font(listview, font);
        aroma_listview_add_item(listview, "Inbox", "5 new messages", NULL);
        aroma_listview_add_item(listview, "Starred", "2 items", NULL);
        aroma_listview_add_item(listview, "Archive", "64 items", NULL);
        aroma_node_invalidate(listview);
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
