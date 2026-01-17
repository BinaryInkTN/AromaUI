#include "aroma_platform_interface.h"
#include <GLPS/glps_window_manager.h>
#include "aroma_logger.h"
#include "aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"

/**
 * @file aroma_backend_interface.c
 * @brief Implementation of the Aroma Backend Interface.
 *
 * This file provides the implementation of the AromaPlatformInterface structure,
 * which defines the interface for different backend implementations in the Aroma UI framework.
 */

static glps_WindowManager *wm = NULL;

int initialize()
{
    wm = glps_wm_init();
    if (!wm)
    {
        LOG_CRITICAL("Failed to initialize GLPS' window manager");
        return 0;
    }

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
