#include "aroma_backend_interface.h"
#include <GLPS/glps_window_manager.h>
#include "aroma_logger.h"

/**
 * @file aroma_backend_interface.c
 * @brief Implementation of the Aroma Backend Interface.
 *
 * This file provides the implementation of the AromaBackendInterface structure,
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

void create_window(const char* title, int x, int y, int width, int height)
{
    size_t window_id = glps_wm_window_create(wm, title, x, y, width, height);
}

void run_event_loop()
{
    if (!wm)
    {
        LOG_ERROR("Window manager not initialized. Cannot run event loop.");
        return;
    }

    while (!glps_wm_should_close(wm))
    {
       // glps_wm_window_update(wm, 0);
    }

}

void shutdown()
{
    if (wm)
    {
        glps_wm_destroy(wm);
    }
}

AromaBackendInterface aroma_glps_opengl_backend = {
    .initialize = initialize,
    .create_window = create_window,
    .run_event_loop = run_event_loop,
    .shutdown = shutdown
};