#include "aroma_platform_interface.h"
#include <GLPS/glps_window_manager.h>
#include "aroma_logger.h"
#include "aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "aroma_event.h"
#include "aroma_node.h"

static glps_WindowManager *wm = NULL;
static size_t primary_window_id = 0;
static double last_mouse_x = 0.0;
static double last_mouse_y = 0.0;
static uint64_t last_hovered_node_id = 0;

#define MOUSE_MOVE_MIN_DISTANCE 80  

static double last_queued_mouse_x = 0.0;
static double last_queued_mouse_y = 0.0;
static uint32_t pending_mouse_move_event = 0;  

static bool queue_mouse_event(AromaEventType type, double mouse_x, double mouse_y, uint8_t button)
{
    AromaNode* root = aroma_event_get_root();
    if (!root) {
        return false;
    }

    AromaNode* target = aroma_event_hit_test(root, (int)mouse_x, (int)mouse_y);
    uint64_t target_id = target ? target->node_id : root->node_id;
    bool target_is_button = target && target->node_type == NODE_TYPE_WIDGET;

    AromaEvent* event = aroma_event_create_mouse(type, target_id, (int)mouse_x, (int)mouse_y, button);
    if (!event) {
        return false;
    }

    event->data.mouse.delta_x = (int)(mouse_x - last_mouse_x);
    event->data.mouse.delta_y = (int)(mouse_y - last_mouse_y);

    bool queued = aroma_event_queue(event);

    if (type == EVENT_TYPE_MOUSE_MOVE && queued) {
        if (last_hovered_node_id != 0 && last_hovered_node_id != target_id && !target_is_button) {

            AromaEvent* exit_event = aroma_event_create_mouse(EVENT_TYPE_MOUSE_MOVE, last_hovered_node_id,
                (int)mouse_x, (int)mouse_y, button);
            if (exit_event) {
                exit_event->data.mouse.delta_x = event->data.mouse.delta_x;
                exit_event->data.mouse.delta_y = event->data.mouse.delta_y;
                aroma_event_queue(exit_event);
            }
            last_hovered_node_id = 0;
        } else if (target_is_button) {
            last_hovered_node_id = target_id;
        }
    }

    return queued;
}

static bool queue_key_event(AromaEventType type, uint32_t key_value)
{
    AromaNode* root = aroma_event_get_root();
    if (!root) {
        return false;
    }

    AromaEvent* event = aroma_event_create_key(type, root->node_id, key_value, 0);
    if (!event) {
        return false;
    }

    return aroma_event_queue(event);
}

static void glps_mouse_move_callback(size_t window_id, double mouse_x, double mouse_y, void *data)
{
    (void)window_id;
    (void)data;

    double dx = mouse_x - last_queued_mouse_x;
    double dy = mouse_y - last_queued_mouse_y;
    double distance = (dx * dx) + (dy * dy);  

    if (distance >= (MOUSE_MOVE_MIN_DISTANCE * MOUSE_MOVE_MIN_DISTANCE)) {

        last_queued_mouse_x = mouse_x;
        last_queued_mouse_y = mouse_y;
    }

    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;
}

static void glps_mouse_click_callback(size_t window_id, bool state, void *data)
{
    (void)window_id;
    (void)data;

    AromaEventType type = state ? EVENT_TYPE_MOUSE_CLICK : EVENT_TYPE_MOUSE_RELEASE;
    queue_mouse_event(type, last_mouse_x, last_mouse_y, 0);
}

static void glps_keyboard_callback(size_t window_id, bool state, const char *value,
                                   unsigned long keycode, void *data)
{
    (void)window_id;
    (void)keycode;  

    (void)data;

    uint32_t key_value = 0;
    if (value && value[0] != '\0') {

        key_value = (unsigned char)value[0];
    } else {

        key_value = (uint32_t)keycode;
    }

    AromaEventType type = state ? EVENT_TYPE_KEY_PRESS : EVENT_TYPE_KEY_RELEASE;
    queue_key_event(type, key_value);
}

int initialize()
{
    wm = glps_wm_init();
    if (!wm)
    {
        LOG_CRITICAL("Failed to initialize GLPS' window manager");
        return 0;
    }

    glps_wm_set_mouse_move_callback(wm, glps_mouse_move_callback, NULL);
    glps_wm_set_mouse_click_callback(wm, glps_mouse_click_callback, NULL);
    glps_wm_set_keyboard_callback(wm, glps_keyboard_callback, NULL);

    return 1;
}

size_t create_window(const char* title, int x, int y, int width, int height)
{
    size_t window_id = glps_wm_window_create(wm, title, x, y, width, height);

    if (window_id == 0)
    {
        aroma_backend_abi.get_graphics_interface()->setup_shared_window_resources();
    }

    aroma_backend_abi.get_graphics_interface()->setup_separate_window_resources(window_id);

    if (primary_window_id == 0) {
        primary_window_id = window_id;
    }

    return window_id;
}

void make_context_current(size_t window_id)
{
    glps_wm_set_window_ctx_curr(wm, window_id);
}

void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    glps_wm_window_get_dimensions(wm, window_id, window_width, window_height);
}

void set_window_update_callback(void (*callback)(size_t window_id, void *data), void* data)
{
    glps_wm_window_set_frame_update_callback(wm, callback, data);
}

void request_window_update(size_t window_id)
{
    glps_wm_window_update(wm, window_id);
}

bool run_event_loop()
{
    if (!wm)
    {
        LOG_ERROR("Window manager not initialized. Cannot run event loop.");
        return false;
    }
    if (primary_window_id != 0) {
        glps_wm_window_update(wm, primary_window_id);
    }
    return !glps_wm_should_close(wm);
}

void swap_buffers(size_t window_id)
{
    glps_wm_swap_buffers(wm, window_id);
}

void shutdown()
{
  if (!wm)
    {
        LOG_ERROR("Window manager not initialized. Cannot shutdown.");
        return;
    }
    glps_wm_destroy(wm);
    wm = NULL;
}

AromaPlatformInterface aroma_platform_glps = {
    .initialize = initialize,
    .create_window = create_window,
    .make_context_current = make_context_current,
    .get_window_size = get_window_size,
    .set_window_update_callback = set_window_update_callback,
    .request_window_update = request_window_update,
    .run_event_loop = run_event_loop,
    .swap_buffers = swap_buffers,
    .shutdown = shutdown
};

