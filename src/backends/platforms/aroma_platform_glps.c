#include "aroma_backend_interface.h"
#include <GLPS/glps_window_manager.h>
#include "aroma_logger.h"


/**
 * @file aroma_backend_interface.c
 * @brief Implementation of the Aroma Backend Interface.
 *
 * This file provides the implementation of the AromaPlatformInterface structure,
 * which defines the interface for different backend implementations in the Aroma UI framework.
 */

static glps_WindowManager *wm = NULL;
static size_t window_id = 0;

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

uint16_t create_window(const char* title, int x, int y, int width, int height)
{
    return glps_wm_window_create(wm, title, x, y, width, height);
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
    .run_event_loop = run_event_loop,
    .shutdown = shutdown
};
