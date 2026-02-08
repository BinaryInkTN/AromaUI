
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

#ifdef __ANDROID__
struct android_app;
void aroma_android_set_app(struct android_app* state) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->set_android_app) {
        platform->set_android_app(state);
    }
}
#endif

#ifdef ESP32
#include <Arduino.h>
#endif
bool g_frame_cleared = false;
bool g_ui_initialized = false;
struct AromaNode* g_main_window = NULL;
AromaWindowHandle g_windows[AROMA_MAX_WINDOWS] = {0};
int g_window_count = 0;
AromaNode* g_focused_node = NULL;
static bool g_immediate_mode = false;
static AromaDrawList* g_window_drawlists[AROMA_MAX_WINDOWS] = {0};
static bool g_splash_enabled = true;

void aroma_splash(bool enabled) {
    g_splash_enabled = enabled;
}

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
    LOG_CRITICAL("Rendering window ID %zu", window_data->window_id);
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

static void __window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) {
      
        return;
    }

    aroma_ui_begin_frame(window_id);
    AromaTheme theme = aroma_theme_get_global();
    aroma_ui_render_dirty_window(window_id, theme.colors.background);
    aroma_dropdown_render_overlays(window_id);

    aroma_ui_end_frame(window_id);
    #ifndef ESP32
    aroma_graphics_swap_buffers(window_id);
    #endif
   
}

#include "aroma_ubuntu_font.h"
#include <unistd.h>

static void __show_splash_screen(size_t window_id, int width, int height) {
    if (!g_splash_enabled) return;

    LOG_INFO("Showing splash screen...");
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    int splash_font_size = 128;
    AromaFont* font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, splash_font_size);
    if (!font) {
        LOG_WARNING("Could not load font for splash screen");
        return;
    }

    aroma_ui_prepare_font_for_window(window_id, font);
    
    AromaTheme theme = aroma_theme_get_global();
    
    if (gfx->clear) {
        gfx->clear(window_id, theme.colors.background); 
    }

    const char* text = "AromaUI";
    float scale = 1.0f;
    float text_width = 0; 
    
    text_width = aroma_font_get_line_width(font, text) * scale;
    int line_height = aroma_font_get_line_height(font) * scale;
    int x = (width - (int)text_width) / 2;
    int y = (height - line_height) / 2; 
    LOG_INFO("Drawing splash: '%s' at (%d, %d) on %dx%d window", text, x, y, width, height);

    if (gfx->render_text) {
        gfx->render_text(window_id, font, text, x, y, theme.colors.primary, scale);
    }

    #ifndef ESP32
    aroma_graphics_swap_buffers(window_id);
    #endif

    #ifndef ESP32
    sleep(5); 
    #else
    delay(5000);
    #endif

    aroma_font_destroy(font);
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

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();

    if (g_window_count == 1 && g_splash_enabled) {
        
        int attempts = 0;
        int max_attempts = 100; 
        int w = 0, h = 0;
        
        while (attempts < max_attempts) {
            aroma_ui_process_events_impl();
            
            if (platform && platform->get_window_size) {
                platform->get_window_size(g_windows[idx].window_id, &w, &h);
            }
            
            if (w > 0 && h > 0) {
                break;
            }
            
            #ifndef ESP32
            usleep(20000); 
            #endif
            attempts++;
        }
        
        if (platform && platform->make_context_current) {
            platform->make_context_current(g_windows[idx].window_id);
        }
        
        if (w <= 0 || h <= 0) {
             LOG_WARNING("Timed out waiting for window surface. Splash might fail.");
             w = (width > 0) ? width : 800;
             h = (height > 0) ? height : 600;
        }

        __show_splash_screen(g_windows[idx].window_id, w, h);
    }
    
    aroma_node_invalidate(window);

    if (platform && platform->set_window_update_callback) {
        platform->set_window_update_callback(__window_update_callback, NULL);
    }
     
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

void aroma_ui_process_events_impl(void) {
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->run_event_loop) {
        if (!platform->run_event_loop()) {
            // Signal main loop to stop? 
            // Usually run_event_loop returns false if quit requested.
            // aroma_ui_shutdown(); ?
            // For now just continue, aroma_ui_is_running checks initialized flag.
        }
    }
    aroma_event_process_queue();
}

AromaDrawList* aroma_ui_begin_frame(size_t window_id) {
       g_frame_cleared = false;
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

#ifndef ESP32
    aroma_drawlist_flush(list, window_id);
#else
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->call_flush_function_ptr) {
        platform->call_flush_function_ptr(aroma_drawlist_smart_flush, list);
        aroma_drawlist_reset(list); 
    }
#endif
}

void aroma_ui_render_dirty_window(size_t window_id, uint32_t clear_color) {
    size_t dirty_count = 0;
    AromaNode** dirty_nodes = aroma_dirty_list_get(&dirty_count);
    #ifdef ESP32
        aroma_dirty_list_clear();
    #endif
    if (dirty_count == 0 && !aroma_ui_is_immediate_mode()) return;

    #ifndef ESP32
    bool frame_active = aroma_drawlist_is_active();
    #else
    bool frame_active = false;
    #endif
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
    
    #ifndef ESP32
    aroma_dirty_list_clear();
    #endif
    

}
