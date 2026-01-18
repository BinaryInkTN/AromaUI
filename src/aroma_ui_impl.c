#include "aroma_ui.h"
#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_style.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "widgets/aroma_window.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <stdlib.h>
#include <stdbool.h>

bool g_ui_initialized = false;
struct AromaNode* g_main_window = NULL;
AromaWindowHandle g_windows[AROMA_MAX_WINDOWS] = {0};
int g_window_count = 0;
AromaNode* g_focused_node = NULL;

bool aroma_ui_init_impl(void) {
    aroma_memory_system_init();
    __node_system_init();
    aroma_event_system_init();

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->initialize) {
        if (!platform->initialize()) {
            LOG_CRITICAL("Failed to initialize platform backend");
            return false;
        }
        LOG_INFO("Platform backend initialized successfully");
    }

    AromaTheme default_theme = aroma_theme_create_default();
    aroma_theme_set_global(&default_theme);
    aroma_dirty_list_clear();

    g_ui_initialized = true;
    LOG_INFO("Aroma UI initialized successfully");
    return true;
}

void aroma_ui_shutdown_impl(void) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->shutdown) {
        platform->shutdown();
        LOG_INFO("Platform backend shutdown");
    }

    aroma_event_system_shutdown();
    __node_system_destroy();

    g_focused_node = NULL;

    g_ui_initialized = false;
    g_main_window = NULL;
    LOG_INFO("Aroma UI shutdown complete");
}

bool aroma_ui_is_running_impl(void) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->run_event_loop) {
        return platform->run_event_loop();
    }
    return false;
}

void aroma_ui_render_impl(struct AromaWindow* window_data) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->request_window_update) {
        platform->request_window_update(window_data->window_id);
    }
}

void aroma_ui_render_all_windows_impl(void) {
    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count == 0) return;

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform) return;

    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].is_active && g_windows[i].window) {
            struct AromaWindow* window_data = (struct AromaWindow*)
                ((AromaNode*)g_windows[i].window)->node_widget_ptr;
            if (window_data && platform->request_window_update) {
                platform->request_window_update(window_data->window_id);
            }
        }
    }
}

AromaWindow* aroma_ui_create_window_impl(const char* title, int width, int height) {
    if (g_window_count >= AROMA_MAX_WINDOWS) {
        LOG_ERROR("Maximum number of windows (%d) reached", AROMA_MAX_WINDOWS);
        return NULL;
    }

    AromaNode* window = aroma_window_create(title, 0, 0, width, height);
    if (!window) {
        LOG_ERROR("Failed to create window");
        return NULL;
    }

    int idx = g_window_count;
    g_windows[idx].window = (AromaWindow*)window;
    g_windows[idx].root_node = window;

    struct AromaWindow* window_data = (struct AromaWindow*)window->node_widget_ptr;
    if (window_data) {
        g_windows[idx].window_id = window_data->window_id;
    }

    g_windows[idx].is_active = true;
    g_window_count++;

    aroma_event_set_root(window);
    if (!g_main_window) {
        g_main_window = window;
    }

    LOG_INFO("Window %d created: title='%s', size=%dx%d", idx, title, width, height);
    aroma_node_invalidate(window);

    return (AromaWindow*)window;
}

void aroma_ui_destroy_window_impl(AromaWindow* window) {
    if (!window) return;

    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].window == window) {

            __destroy_node(g_windows[i].root_node);

            if (g_focused_node) {
                g_focused_node = NULL;
            }

            for (int j = i; j < g_window_count - 1; j++) {
                g_windows[j] = g_windows[j + 1];
            }
            g_window_count--;

            if ((AromaNode*)window == g_main_window) {
                g_main_window = (g_window_count > 0) ? g_windows[0].root_node : NULL;
            }

            LOG_INFO("Window destroyed");
            return;
        }
    }
}

