#include "aroma.h"
#include <unistd.h>

static AromaNode* divider_h = NULL;
static AromaNode* divider_v = NULL;

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;
    aroma_ui_render_dirty_window(window_id, 0xFFFBFE);
    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;

    AromaWindow* window = aroma_ui_create_window("Widget Example - Divider", 320, 200);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);

    divider_h = aroma_divider_create((AromaNode*)window, 20, 60, 280, DIVIDER_ORIENTATION_HORIZONTAL);
    divider_v = aroma_divider_create((AromaNode*)window, 160, 80, 80, DIVIDER_ORIENTATION_VERTICAL);
    if (divider_h) aroma_node_invalidate(divider_h);
    if (divider_v) aroma_node_invalidate(divider_v);

    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(4000);
    }

    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}
