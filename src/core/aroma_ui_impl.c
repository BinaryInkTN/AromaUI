#include "aroma_ui.h"
#include "core/aroma_common.h"
#include "core/aroma_node.h"
#include "core/aroma_event.h"
#include "core/aroma_style.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_drawlist.h"
#include "core/aroma_timer.h"
#include "core/aroma_time.h"
#include "widgets/aroma_window.h"
#include "widgets/aroma_container.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_ubuntu_font.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void aroma_android_set_app(struct android_app *state)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->set_android_app)
    {
        platform->set_android_app(state);
    }
}
#endif

#ifdef ESP32
#include <Arduino.h>
#define SLEEP_MS(ms) delay(ms)
#else
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#define INVALID_INDEX -1
#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600

bool g_frame_cleared = false;
bool g_ui_initialized = false;
struct AromaNode *g_main_window = NULL;
AromaWindowHandle g_windows[AROMA_MAX_WINDOWS] = {0};
int g_window_count = 0;
AromaNode *g_focused_node = NULL;
static bool g_immediate_mode = false;
static bool g_running = true;
static AromaDrawList *g_window_drawlists[AROMA_MAX_WINDOWS] = {0};
static AromaTheme g_default_theme;
static bool g_frame_active = false;

static void window_update_callback(size_t window_id, void *data);
static void render_dirty_window_internal(size_t window_id, uint32_t clear_color);
static int find_window_index_by_id(size_t window_id);
static void collect_draw_tasks(struct AromaNode *node, AromaDrawTask *tasks,
                               size_t *task_count, size_t max_tasks,
                               const AromaRect *clip);
static int draw_task_compare(const void *a, const void *b);

static inline bool is_valid_node_ptr(const AromaNode *node)
{
    if (!node)
        return false;
    if (((uintptr_t)node % _Alignof(AromaNode)) != 0)
        return false;
    return true;
}

static inline bool node_is_drawable_candidate(const AromaNode *node)
{
    if (!is_valid_node_ptr(node))
        return false;
    if (node->is_hidden)
        return false;

#ifdef __EMSCRIPTEN__
    if (node->node_id == 0 || node->node_id > 1000000ULL)
        return false;
#endif

    return true;
}

static inline bool append_draw_task(AromaNode *node, AromaDrawTask *tasks,
                                    size_t *task_count, size_t max_tasks)
{
    if (*task_count >= max_tasks)
        return false;

    AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(node);
    if (!draw_cb)
        return false;

    tasks[(*task_count)++] = (AromaDrawTask){
        .node = node,
        .draw_cb = draw_cb,
        .z_index = node->z_index};
    return true;
}

void aroma_ui_open_url_impl(const char *url)
{
    if (!url)
        return;

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->open_url)
    {
        platform->open_url(url);
    }
    else
    {
        LOG_WARNING("Open URL not supported on this platform");
    }
}

void aroma_ui_android_intent_impl(int action, const char *uri, const char *type,
                                  const AromaIntentExtra *extras, int extra_count)
{
#ifdef __ANDROID__
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->android_send_intent)
    {
        platform->android_send_intent(action, uri, type, (const void *)extras, extra_count);
    }
    else
    {
        LOG_ERROR("Android Intent not supported");
    }
#else
    (void)action;
    (void)uri;
    (void)type;
    (void)extras;
    (void)extra_count;
    LOG_WARNING("Android Intent called on non-Android platform");
#endif
}

bool aroma_ui_init_impl(void)
{
    if (g_ui_initialized)
    {
        LOG_WARNING("Aroma UI already initialized");
        return true;
    }

    __node_system_init();
    aroma_event_system_init();

    srand((unsigned int)time(NULL));

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->initialize && !platform->initialize())
    {
        LOG_CRITICAL("Failed to initialize platform backend");
        return false;
    }

    if (platform)
    {
        LOG_INFO("Platform backend initialized successfully");
    }

    g_default_theme = aroma_theme_create_default();
    aroma_theme_set_global(&g_default_theme);
    aroma_dirty_list_clear();

    const char *immediate_env = getenv("AROMA_UI_IMMEDIATE");
    g_immediate_mode = (immediate_env && immediate_env[0] == '1');

    for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
    {
        g_window_drawlists[i] = NULL;
    }

    if (platform && platform->set_window_update_callback)
    {
        platform->set_window_update_callback(window_update_callback, NULL);
    }

    g_running = true;
    g_ui_initialized = true;
    g_frame_active = false;
    LOG_INFO("Aroma UI initialized successfully (mode: %s)",
             g_immediate_mode ? "immediate" : "batched");
    return true;
}

void aroma_ui_set_immediate_mode(bool enabled)
{
    g_immediate_mode = enabled;
}

bool aroma_ui_is_immediate_mode(void)
{
    return g_immediate_mode;
}

void aroma_ui_request_redraw(void *user_data)
{
    (void)user_data;
    if (g_main_window)
    {
        aroma_node_invalidate(g_main_window);
    }
}

bool aroma_ui_consume_redraw(void)
{
    if (g_immediate_mode)
        return true;
    return aroma_dirty_list_has_entries();
}

void aroma_ui_shutdown_impl(void)
{
    if (!g_ui_initialized)
        return;

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->shutdown)
    {
        platform->shutdown();
        LOG_INFO("Platform backend shutdown");
    }

    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_window_drawlists[i])
        {
            aroma_drawlist_destroy(g_window_drawlists[i]);
            g_window_drawlists[i] = NULL;
        }
    }

    memset(g_windows, 0, sizeof(g_windows));

    aroma_event_system_shutdown();
    __node_system_destroy();

    g_focused_node = NULL;
    g_main_window = NULL;
    g_window_count = 0;
    g_running = false;
    g_ui_initialized = false;
    g_frame_active = false;

    LOG_INFO("Aroma UI shutdown complete");
}

bool aroma_ui_is_running_impl(void)
{
    return g_running;
}

void aroma_ui_render_impl(struct AromaWindow *window_data)
{
    if (!window_data)
        return;

    aroma_ui_render_all_windows_impl();
}

void aroma_ui_render_all_windows_impl(void)
{

    if (!aroma_dirty_list_has_entries() && !g_immediate_mode)
        return;

    if (g_window_count > 0 && g_windows[0].is_active && g_windows[0].window)
    {
        window_update_callback(g_windows[0].window_id, NULL);
    }
}

void aroma_ui_process_events_impl(void)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();

    if (platform && platform->run_event_loop)
    {
        if (!platform->run_event_loop())
        {
            g_running = false;
        }
    }

    aroma_event_process_queue();
    aroma_timer_tick(aroma_time_now_ms());
}

AromaWindow *aroma_ui_create_window_impl(const char *title, int width, int height)
{
    if (g_window_count >= AROMA_MAX_WINDOWS)
    {
        LOG_ERROR("Maximum number of windows (%d) reached", AROMA_MAX_WINDOWS);
        return NULL;
    }

    struct AromaNode *window = aroma_window_create(title, 0, 0, width, height);
    if (!window)
    {
        LOG_ERROR("Failed to create window");
        return NULL;
    }

    int idx = g_window_count;
    struct AromaWindow *window_data = (struct AromaWindow *)window->node_widget_ptr;

    g_windows[idx].window = (AromaWindow *)window;
    g_windows[idx].root_node = window;
    g_windows[idx].window_id = window_data ? window_data->window_id : 0;
    g_windows[idx].is_active = true;

    g_window_drawlists[idx] = aroma_drawlist_create();
    if (!g_window_drawlists[idx])
    {
        LOG_ERROR("Failed to create drawlist for window");
        __destroy_node(window);
        return NULL;
    }

    g_window_count++;

    if (!g_main_window)
    {
        g_main_window = window;
    }

    aroma_event_set_root(window);

    LOG_INFO("Window %d created: title='%s', size=%dx%d", idx, title, width, height);

    aroma_node_invalidate(window);

    return (AromaWindow *)window;
}

void aroma_ui_destroy_window_impl(AromaWindow *window)
{
    if (!window)
        return;

    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_windows[i].window == window)
        {
            if (g_frame_active)
            {
                int active_idx = find_window_index_by_id(g_windows[i].window_id);
                if (active_idx == i)
                {
                    AromaDrawList *list = g_window_drawlists[i];
                    if (list)
                    {
                        aroma_drawlist_end();
                        aroma_drawlist_reset(list);
                    }
                    g_frame_active = false;
                }
            }

            aroma_window_destroy((AromaNode *)g_windows[i].root_node);

            if (g_window_drawlists[i])
            {
                aroma_drawlist_destroy(g_window_drawlists[i]);
                g_window_drawlists[i] = NULL;
            }

            int remaining = g_window_count - i - 1;
            if (remaining > 0)
            {
                memmove(&g_windows[i], &g_windows[i + 1],
                        remaining * sizeof(AromaWindowHandle));
                memmove(&g_window_drawlists[i], &g_window_drawlists[i + 1],
                        remaining * sizeof(AromaDrawList *));
            }

            g_window_count--;

            if ((struct AromaNode *)window == g_main_window)
            {
                g_main_window = (g_window_count > 0) ? g_windows[0].root_node : NULL;
            }

            g_focused_node = NULL;

            LOG_INFO("Window destroyed");
            return;
        }
    }
}

AromaDrawList *aroma_ui_begin_frame(size_t window_id)
{
    (void)window_id;

    if (g_frame_active)
    {
        LOG_ERROR("aroma_ui_begin_frame: frame already active");
        return NULL;
    }

    g_frame_cleared = false;

    if (g_window_count == 0)
    {
        LOG_ERROR("aroma_ui_begin_frame: no windows created");
        return NULL;
    }

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current)
    {
        platform->make_context_current(g_windows[0].window_id);
    }

    if (!g_window_drawlists[0])
    {
        g_window_drawlists[0] = aroma_drawlist_create();
        if (!g_window_drawlists[0])
        {
            LOG_ERROR("Failed to create draw list for window 0");
            return NULL;
        }
    }

    AromaDrawList *list = g_window_drawlists[0];
    aroma_drawlist_reset(list);
    aroma_drawlist_begin(list);

    g_frame_active = true;

    return list;
}

void aroma_ui_end_frame(size_t window_id)
{
    (void)window_id;

    if (!g_frame_active)
    {
        LOG_ERROR("aroma_ui_end_frame: no active frame");
        return;
    }

    if (g_window_count == 0)
    {
        LOG_ERROR("aroma_ui_end_frame: no windows created");
        g_frame_active = false;
        return;
    }

    AromaDrawList *list = g_window_drawlists[0];
    if (!list)
    {
        g_frame_active = false;
        return;
    }

    aroma_drawlist_end();

#ifdef ESP32
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->call_flush_function_ptr)
    {
        platform->call_flush_function_ptr(aroma_drawlist_smart_flush, list);
        aroma_drawlist_reset(list);
    }
#else
    aroma_drawlist_flush(list, g_windows[0].window_id);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->graphics_flush)
    {
        gfx->graphics_flush();
    }

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->swap_buffers)
    {
        platform->swap_buffers(g_windows[0].window_id);
    }
#endif

    g_frame_active = false;
}

static void render_dirty_window_internal(size_t window_id, uint32_t clear_color)
{
    size_t dirty_count = 0;
    AromaNode **dirty_nodes = aroma_dirty_list_get(&dirty_count);

    if (dirty_count == 0 && !g_immediate_mode)
        return;

    if (clear_color != AROMA_CLEAR_NONE && !g_frame_cleared)
    {
        aroma_graphics_clear(window_id, clear_color);
        g_frame_cleared = true;
    }

    AromaDrawTask tasks[AROMA_MAX_DIRTY_NODES];
    size_t task_count = 0;

    int backend_type = aroma_backend_abi.get_graphics_backend_type
                           ? aroma_backend_abi.get_graphics_backend_type()
                           : -1;

    bool use_scene_traversal = false;
#ifdef __EMSCRIPTEN__
    use_scene_traversal = (backend_type == GRAPHICS_BACKEND_GLES3);
#else
    use_scene_traversal = (backend_type == GRAPHICS_BACKEND_GLES3 ||
                           backend_type == GRAPHICS_BACKEND_VULKAN);
#endif

    if (use_scene_traversal)
    {
        const AromaRect *clip = NULL;
        AromaRect dirty_clip = {0};

        if (backend_type == GRAPHICS_BACKEND_VULKAN)
        {
            AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
            int dx, dy, dw, dh;
            if (gfx && gfx->get_pending_dirty_rect &&
                gfx->get_pending_dirty_rect(&dx, &dy, &dw, &dh))
            {
                int screen_w = 0, screen_h = 0;
                AromaPlatformInterface *plat = aroma_backend_abi.get_platform_interface();
                if (plat && plat->get_window_size)
                    plat->get_window_size(window_id, &screen_w, &screen_h);

                if (screen_w > 0 && screen_h > 0 &&
                    (dw * dh) < (screen_w * screen_h) / 2)
                {
                    const int margin = 4;
                    dirty_clip = (AromaRect){
                        .x = dx - margin,
                        .y = dy - margin,
                        .width = dw + margin * 2,
                        .height = dh + margin * 2,
                    };
                    clip = &dirty_clip;
                }
            }
        }

        if (g_window_count > 0 && g_windows[0].root_node)
        {
            collect_draw_tasks(g_windows[0].root_node, tasks, &task_count,
                               AROMA_MAX_DIRTY_NODES, clip);
        }
    }
    else
    {
        for (size_t i = 0; i < dirty_count && task_count < AROMA_MAX_DIRTY_NODES; ++i)
        {
            struct AromaNode *node = dirty_nodes[i];

            if (!node_is_drawable_candidate(node))
                continue;

            append_draw_task(node, tasks, &task_count, AROMA_MAX_DIRTY_NODES);
        }
    }

    if (task_count > 1)
    {
        qsort(tasks, task_count, sizeof(AromaDrawTask), draw_task_compare);
    }

#ifdef __EMSCRIPTEN__
    LOG_INFO("render_dirty: window=%zu dirty_count=%zu task_count=%zu",
             window_id, dirty_count, task_count);
#endif

    AromaTheme theme = aroma_theme_get_global();
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();

    for (size_t i = 0; i < task_count; ++i)
    {
        AromaNode *n = tasks[i].node;

        if (!is_valid_node_ptr(n))
            continue;

#ifdef __EMSCRIPTEN__
        if (n->node_id == 0 || n->node_id > 1000000ULL)
            continue;
#endif

        if (n->child_count > AROMA_MAX_CHILD_NODES)
        {
            LOG_ERROR("render_dirty: node %llu has excessive child_count=%llu, capping",
                      (unsigned long long)n->node_id,
                      (unsigned long long)n->child_count);
            n->child_count = AROMA_MAX_CHILD_NODES;
        }
        if (n->node_type < 0 || n->node_type > NODE_TYPE_WIDGET)
        {
            LOG_ERROR("render_dirty: node %llu has invalid node_type=%d, skipping",
                      (unsigned long long)n->node_id, (int)n->node_type);
            continue;
        }

#ifdef __EMSCRIPTEN__
        LOG_INFO("render_dirty: draw task i=%zu node_id=%llu widget_ptr=%p child_count=%llu",
                 i,
                 (unsigned long long)n->node_id,
                 (void *)n->node_widget_ptr,
                 (unsigned long long)n->child_count);
#endif

        tasks[i].draw_cb(n, window_id);

#ifdef __EMSCRIPTEN__
        LOG_INFO("render_dirty: done task i=%zu", i);
#endif

#ifndef __EMSCRIPTEN__
        if (gfx && gfx->fill_rectangle && n->node_widget_ptr)
        {
            float effective_opacity = 1.0f;
            AromaNode *curr = n;
            while (curr)
            {
                effective_opacity *= curr->opacity;
                curr = curr->parent_node;
            }

            if (effective_opacity < 0.99f)
            {
                AromaRect *rect = aroma_node_get_rect(n);
                if (rect)
                {
                    uint8_t r, g, b;
                    aroma_color_extract_rgb(theme.colors.background, &r, &g, &b);
                    float alpha_f = 1.0f - effective_opacity;
                    if (alpha_f < 0.0f)
                        alpha_f = 0.0f;
                    if (alpha_f > 1.0f)
                        alpha_f = 1.0f;
                    uint8_t a = (uint8_t)(alpha_f * 255.0f);

                    uint32_t overlay_color = aroma_color_rgba(r, g, b, a);
                    gfx->fill_rectangle(window_id,
                                        rect->x, rect->y, rect->width, rect->height,
                                        overlay_color, false, 0.0f);
                }
            }
        }
#endif
    }
}

static int draw_task_compare(const void *a, const void *b)
{
    const AromaDrawTask *ta = (const AromaDrawTask *)a;
    const AromaDrawTask *tb = (const AromaDrawTask *)b;
    return (ta->z_index > tb->z_index) - (ta->z_index < tb->z_index);
}

static int find_window_index_by_id(size_t window_id)
{
    if (g_window_count > AROMA_MAX_WINDOWS)
    {
        LOG_ERROR("find_window_index_by_id: invalid g_window_count=%d", g_window_count);
        return INVALID_INDEX;
    }

    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_windows[i].window_id == window_id)
            return i;
    }

    LOG_WARNING("find_window_index_by_id: window_id=%zu not found", window_id);
    return INVALID_INDEX;
}

static void collect_draw_tasks(struct AromaNode *node, AromaDrawTask *tasks,
                               size_t *task_count, size_t max_tasks,
                               const AromaRect *clip)
{
    if (!node_is_drawable_candidate(node))
        return;
    if (*task_count >= max_tasks)
        return;

    if (node->child_count > AROMA_MAX_CHILD_NODES)
    {
        LOG_ERROR("collect_draw_tasks: node %llu corrupt child_count %llu",
                  (unsigned long long)node->node_id,
                  (unsigned long long)node->child_count);
        return;
    }

    if (clip)
    {
        const AromaRect *r = aroma_node_get_rect(node);
        if (r && r->width > 0 && r->height > 0)
        {
            bool outside = (r->x >= clip->x + clip->width) ||
                           (r->y >= clip->y + clip->height) ||
                           (r->x + r->width <= clip->x) ||
                           (r->y + r->height <= clip->y);
            if (outside)
                return;
        }
    }

    append_draw_task(node, tasks, task_count, max_tasks);

    if (aroma_container_is_scrollable(node) && !aroma_card_is_card(node->parent_node))
    {
        return;
    }

    for (uint64_t i = 0; i < node->child_count; ++i)
    {
        if (node->child_nodes[i])
        {
            collect_draw_tasks(node->child_nodes[i], tasks, task_count, max_tasks, clip);
        }
    }
}
static void window_update_callback(size_t window_id, void *data)
{
    (void)data;
    (void)window_id;

    static bool s_in_update = false;
    if (s_in_update)
    {
        LOG_WARNING("window_update_callback: recursive call prevented");
        return;
    }
    s_in_update = true;

    aroma_frame_advance();

    size_t actual_window_id = (g_window_count > 0) ? g_windows[0].window_id : 0;

    int width = 0, height = 0;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size)
    {
        platform->get_window_size(actual_window_id, &width, &height);
    }

    if (g_window_count > 0 && g_windows[0].root_node)
    {
        aroma_node_update_layout(g_windows[0].root_node, 0, 0, width, height);
    }
    else
    {
        LOG_ERROR("window_update_callback: no window 0 available");
        s_in_update = false;
        return;
    }

    AromaTheme theme = aroma_theme_get_global();

    {
        size_t dirty_count = 0;
        AromaNode **dirty_nodes = aroma_dirty_list_get(&dirty_count);
        if (dirty_count > 0)
        {
            int min_x = INT32_MAX, min_y = INT32_MAX;
            int max_x = INT32_MIN, max_y = INT32_MIN;
            for (size_t i = 0; i < dirty_count; i++)
            {
                AromaNode *node = dirty_nodes[i];
                if (!is_valid_node_ptr(node))
                    continue;
                AromaRect *r = aroma_node_get_rect(node);
                if (!r || r->width <= 0 || r->height <= 0)
                    continue;
                if (r->x < min_x)
                    min_x = r->x;
                if (r->y < min_y)
                    min_y = r->y;
                if (r->x + r->width > max_x)
                    max_x = r->x + r->width;
                if (r->y + r->height > max_y)
                    max_y = r->y + r->height;
            }
            if (max_x > min_x && max_y > min_y)
            {
                AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
                if (gfx && gfx->notify_dirty_region)
                {
                    gfx->notify_dirty_region(min_x, min_y,
                                             max_x - min_x, max_y - min_y);
                }
            }
        }
    }

    AromaDrawList *drawlist = aroma_ui_begin_frame(0);
    if (!drawlist)
    {
        LOG_ERROR("window_update_callback: Failed to begin frame");
        s_in_update = false;
        return;
    }

    aroma_graphics_clear(actual_window_id, theme.colors.background);
    g_frame_cleared = true;

    render_dirty_window_internal(actual_window_id, AROMA_CLEAR_NONE);
    aroma_dropdown_render_overlays(actual_window_id);

    aroma_ui_end_frame(0);

    aroma_dirty_list_clear();
    s_in_update = false;
}

void aroma_ui_set_offscreen_mode(bool offscreen)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->set_offscreen_mode)
    {
        platform->set_offscreen_mode(offscreen);
    }
}

void aroma_ui_set_use_surfaceless(bool use_surfaceless)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->set_use_surfaceless)
    {
        platform->set_use_surfaceless(use_surfaceless);
    }
}

void aroma_ui_read_pixels(AromaWindow *window, void *buffer, int width, int height)
{
    if (!window)
        return;

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->read_pixels)
    {
        size_t win_id = (g_window_count > 0) ? g_windows[0].window_id : 0;
        platform->read_pixels(win_id, buffer, width, height);
    }
}