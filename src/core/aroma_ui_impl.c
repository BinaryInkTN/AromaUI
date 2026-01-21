/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "aroma_ui.h"
#include "core/aroma_common.h"
#include "core/aroma_node.h"
#include "core/aroma_event.h"
#include "core/aroma_style.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_drawlist.h"
#include "widgets/aroma_window.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <stdlib.h>
#include <stdbool.h>

bool g_ui_initialized = false;
struct AromaNode* g_main_window = NULL;
AromaWindowHandle g_windows[AROMA_MAX_WINDOWS] = {0};
int g_window_count = 0;
AromaNode* g_focused_node = NULL;
static bool g_immediate_mode = false;
static AromaDrawList* g_window_drawlists[AROMA_MAX_WINDOWS] = {0};


static inline int __draw_task_compare(const void* a, const void* b) {
    const AromaDrawTask* ta = (const AromaDrawTask*)a;
    const AromaDrawTask* tb = (const AromaDrawTask*)b;
    return (ta->z_index > tb->z_index) - (ta->z_index < tb->z_index);
}

static inline int __find_window_index_by_id(size_t window_id) {
    for (int i = 0; i < g_window_count; ++i)
        if (g_windows[i].window_id == window_id)
            return i;
    return -1;
}

static inline bool __node_matches_window_id(AromaNode* node, size_t window_id) {
    if (!node) return false;
    AromaNode* root = node;
    while (root->parent_node) root = root->parent_node;
    for (int i = 0; i < g_window_count; ++i)
        if (g_windows[i].root_node == root && g_windows[i].window_id == window_id)
            return true;
    return false;
}

static void __collect_draw_tasks(AromaNode* node, AromaDrawTask* tasks, size_t* task_count, size_t max_tasks) {
    if (!node || node->is_hidden) return;
    AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(node);
    if (draw_cb && *task_count < max_tasks) {
        tasks[(*task_count)++] = (AromaDrawTask){ .node = node, .draw_cb = draw_cb, .z_index = node->z_index };
    }
    for (uint64_t i = 0; i < node->child_count; ++i)
        if (node->child_nodes[i])
            __collect_draw_tasks(node->child_nodes[i], tasks, task_count, max_tasks);
}

bool aroma_ui_init_impl(void) {
    __node_system_init();
    aroma_event_system_init();

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->initialize && !platform->initialize()) {
        LOG_CRITICAL("Failed to initialize platform backend");
        return false;
    }
    if (platform) LOG_INFO("Platform backend initialized successfully");

    AromaTheme default_theme = aroma_theme_create_default();
    aroma_theme_set_global(&default_theme);
    aroma_dirty_list_clear();

    if (getenv("AROMA_UI_IMMEDIATE") && getenv("AROMA_UI_IMMEDIATE")[0] == '1')
        aroma_ui_set_immediate_mode(true);

    g_ui_initialized = true;
    LOG_INFO("Aroma UI initialized successfully");
    return true;
}

void aroma_ui_set_immediate_mode(bool enabled) { g_immediate_mode = enabled; }
bool aroma_ui_is_immediate_mode(void) { return g_immediate_mode; }

void aroma_ui_request_redraw(void* user_data) {
    (void)user_data;
    if (g_main_window) aroma_node_invalidate(g_main_window);
}

bool aroma_ui_consume_redraw(void) {
    if (aroma_ui_is_immediate_mode()) return true;
    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    return dirty_count > 0;
}

void aroma_ui_shutdown_impl(void) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->shutdown) {
        platform->shutdown();
        LOG_INFO("Platform backend shutdown");
    }
    aroma_event_system_shutdown();
    __node_system_destroy();

    for (int i = 0; i < g_window_count; ++i) {
        if (g_window_drawlists[i]) {
            aroma_drawlist_destroy(g_window_drawlists[i]);
            g_window_drawlists[i] = NULL;
        }
    }
    g_focused_node = NULL;
    g_ui_initialized = false;
    g_main_window = NULL;
    LOG_INFO("Aroma UI shutdown complete");
}

bool aroma_ui_is_running_impl(void) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    return (platform && platform->run_event_loop) ? platform->run_event_loop() : false;
}

void aroma_ui_render_impl(struct AromaWindow* window_data) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->request_window_update)
        platform->request_window_update(window_data->window_id);
}

void aroma_ui_render_all_windows_impl(void) {
    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count == 0 && !aroma_ui_is_immediate_mode()) return;

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform) return;

    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].is_active && g_windows[i].window) {
            struct AromaWindow* window_data = (struct AromaWindow*)((AromaNode*)g_windows[i].window)->node_widget_ptr;
            if (window_data && platform->request_window_update)
                platform->request_window_update(window_data->window_id);
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
    if (window_data) g_windows[idx].window_id = window_data->window_id;
    g_windows[idx].is_active = true;
    g_window_count++;
    g_window_drawlists[idx] = aroma_drawlist_create();
    aroma_event_set_root(window);
    if (!g_main_window) g_main_window = window;
    LOG_INFO("Window %d created: title='%s', size=%dx%d", idx, title, width, height);
    aroma_node_invalidate(window);
    return (AromaWindow*)window;
}

void aroma_ui_destroy_window_impl(AromaWindow* window) {
    if (!window) return;
    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].window == window) {
            __destroy_node(g_windows[i].root_node);
            if (g_window_drawlists[i]) {
                aroma_drawlist_destroy(g_window_drawlists[i]);
                g_window_drawlists[i] = NULL;
            }
            g_focused_node = NULL;
            for (int j = i; j < g_window_count - 1; ++j) {
                g_windows[j] = g_windows[j + 1];
                g_window_drawlists[j] = g_window_drawlists[j + 1];
            }
            --g_window_count;
            if ((AromaNode*)window == g_main_window)
                g_main_window = (g_window_count > 0) ? g_windows[0].root_node : NULL;
            LOG_INFO("Window destroyed");
            return;
        }
    }
}

AromaDrawList* aroma_ui_begin_frame(size_t window_id) {
    int idx = __find_window_index_by_id(window_id);
    if (idx < 0) return NULL;
    AromaDrawList* list = g_window_drawlists[idx];
    if (!list) g_window_drawlists[idx] = list = aroma_drawlist_create();
    aroma_drawlist_reset(list);
    aroma_drawlist_begin(list);
    return list;
}

void aroma_ui_end_frame(size_t window_id) {
    int idx = __find_window_index_by_id(window_id);
    if (idx < 0) return;
    AromaDrawList* list = g_window_drawlists[idx];
    if (!list) return;
    aroma_drawlist_end();
    aroma_drawlist_flush(list, window_id);
}

void aroma_ui_render_dirty_window(size_t window_id, uint32_t clear_color) {
    size_t dirty_count = 0;
    AromaNode** dirty_nodes = aroma_dirty_list_get(&dirty_count);
    if (dirty_count == 0 && !aroma_ui_is_immediate_mode()) return;

    bool frame_active = aroma_drawlist_is_active();
    if (!frame_active) {
        AromaDrawList* list = aroma_ui_begin_frame(window_id);
        if (!list) return;
    }
    if (clear_color != AROMA_CLEAR_NONE)
        aroma_graphics_clear(window_id, clear_color);

    AromaDrawTask tasks[AROMA_MAX_DIRTY_NODES];
    size_t task_count = 0;

    int backend_type = aroma_backend_abi.get_graphics_backend_type ?
        aroma_backend_abi.get_graphics_backend_type() : -1;

    if (backend_type == GRAPHICS_BACKEND_GLES3) {
        for (int i = 0; i < g_window_count; ++i) {
            if (g_windows[i].window_id == window_id && g_windows[i].root_node) {
                __collect_draw_tasks(g_windows[i].root_node, tasks, &task_count, AROMA_MAX_DIRTY_NODES);
                break;
            }
        }
    } else {
        for (size_t i = 0; i < dirty_count && task_count < AROMA_MAX_DIRTY_NODES; ++i) {
            AromaNode* node = dirty_nodes[i];
            if (!node || node->is_hidden) continue;
            if (!__node_matches_window_id(node, window_id)) continue;
            AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(node);
            if (!draw_cb) continue;
            tasks[task_count++] = (AromaDrawTask){ .node = node, .draw_cb = draw_cb, .z_index = node->z_index };
        }
    }
    if (task_count > 1)
        qsort(tasks, task_count, sizeof(AromaDrawTask), __draw_task_compare);

    for (size_t i = 0; i < task_count; ++i)
        tasks[i].draw_cb(tasks[i].node, window_id);

    if (!frame_active)
        aroma_ui_end_frame(window_id);
    aroma_dirty_list_clear();
}
