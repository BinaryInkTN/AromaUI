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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

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
#define MAX_SPLASH_WAIT_ATTEMPTS 100
#define SPLASH_WAIT_INTERVAL_MS 20
#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600

bool g_frame_cleared = false;
bool g_ui_initialized = false;
struct AromaNode *g_main_window = NULL;
AromaWindowHandle g_windows[AROMA_MAX_WINDOWS] = {0};
int g_window_count = 0;
AromaNode *g_focused_node = NULL;
static bool g_immediate_mode = false;
static AromaDrawList *g_window_drawlists[AROMA_MAX_WINDOWS] = {0};
static bool g_splash_enabled = true;
static AromaTheme g_default_theme;

static void window_update_callback(size_t window_id, void *data);
static int find_window_index_by_id(size_t window_id);
static int find_window_index_by_node(struct AromaNode *node);
static void collect_draw_tasks(struct AromaNode *node, AromaDrawTask *tasks,
                               size_t *task_count, size_t max_tasks);
static void show_splash_screen(size_t window_id, int width, int height);
static int draw_task_compare(const void *a, const void *b);

void aroma_splash(bool enabled)
{
    g_splash_enabled = enabled;
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

    g_ui_initialized = true;
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
        aroma_ui_render_all_windows_impl();
    }
}

bool aroma_ui_consume_redraw(void)
{
    if (g_immediate_mode)
        return true;

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    return dirty_count > 0;
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
    g_ui_initialized = false;

    LOG_INFO("Aroma UI shutdown complete");
}

bool aroma_ui_is_running_impl(void)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    return (platform && platform->run_event_loop) ? platform->run_event_loop() : false;
}

void aroma_ui_render_impl(struct AromaWindow *window_data)
{
    if (!window_data)
        return;

    LOG_INFO("Rendering window ID %zu", window_data->window_id);
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->request_window_update)
    {
        platform->request_window_update(window_data->window_id);
    }
}

void aroma_ui_render_all_windows_impl(void)
{
    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);

    if (dirty_count == 0 && !g_immediate_mode)
        return;

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (!platform || !platform->request_window_update)
        return;

    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_windows[i].is_active && g_windows[i].window)
        {
            platform->request_window_update(g_windows[i].window_id);
        }
    }
}

void aroma_ui_process_events_impl(void)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();

    if (platform && platform->run_event_loop)
    {
        platform->run_event_loop();
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

    if (g_window_count == 1 && g_splash_enabled)
    {
        AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();

        int w = 0, h = 0;
        for (int attempts = 0; attempts < MAX_SPLASH_WAIT_ATTEMPTS; attempts++)
        {
            aroma_ui_process_events_impl();

            if (platform && platform->get_window_size)
            {
                platform->get_window_size(g_windows[idx].window_id, &w, &h);
            }

            if (w > 0 && h > 0)
            {
                break;
            }

            SLEEP_MS(SPLASH_WAIT_INTERVAL_MS);
        }

        if (platform && platform->make_context_current)
        {
            platform->make_context_current(g_windows[idx].window_id);
        }

        if (w <= 0 || h <= 0)
        {
            LOG_WARNING("Timed out waiting for window surface. Using defaults.");
            w = (width > 0) ? width : DEFAULT_WINDOW_WIDTH;
            h = (height > 0) ? height : DEFAULT_WINDOW_HEIGHT;
        }

        aroma_node_invalidate(window);
        show_splash_screen(g_windows[idx].window_id, w, h);
    }

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

            __destroy_node(g_windows[i].root_node);

            if (g_window_drawlists[i])
            {
                aroma_drawlist_destroy(g_window_drawlists[i]);
                g_window_drawlists[i] = NULL;
            }

            int remaining = g_window_count - i - 1;
            if (remaining > 0)
            {
                memmove(&g_windows[i], &g_windows[i + 1], remaining * sizeof(AromaWindowHandle));
                memmove(&g_window_drawlists[i], &g_window_drawlists[i + 1],
                        remaining * sizeof(AromaDrawList *));
            }

            g_window_count--;

            if ((struct AromaNode *)window == g_main_window)
            {
                g_main_window = (g_window_count > 0) ? g_windows[0].root_node : NULL;
            }

            if (g_focused_node)
            {
                g_focused_node = NULL;
            }

            LOG_INFO("Window destroyed");
            return;
        }
    }
}

AromaDrawList *aroma_ui_begin_frame(size_t window_id)
{
    g_frame_cleared = false;

    int idx = find_window_index_by_id(window_id);
    if (idx < 0)
        return NULL;

    if (!g_window_drawlists[idx])
    {
        g_window_drawlists[idx] = aroma_drawlist_create();
        if (!g_window_drawlists[idx])
        {
            LOG_ERROR("Failed to create draw list for window %zu", window_id);
            return NULL;
        }
    }

    AromaDrawList *list = g_window_drawlists[idx];
    aroma_drawlist_reset(list);
    aroma_drawlist_begin(list);

    return list;
}

void aroma_ui_end_frame(size_t window_id)
{
    int idx = find_window_index_by_id(window_id);
    if (idx < 0)
        return;

    AromaDrawList *list = g_window_drawlists[idx];
    if (!list)
        return;

    aroma_drawlist_end();

#ifdef ESP32
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->call_flush_function_ptr)
    {
        platform->call_flush_function_ptr(aroma_drawlist_smart_flush, list);
        aroma_drawlist_reset(list);
    }
#else
    aroma_drawlist_flush(list, window_id);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->graphics_flush)
    {
        gfx->graphics_flush();
    }

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->swap_buffers)
    {
        platform->swap_buffers(window_id);
    }

#endif
}

void aroma_ui_render_dirty_window(size_t window_id, uint32_t clear_color)
{
    size_t dirty_count = 0;
    AromaNode **dirty_nodes = aroma_dirty_list_get(&dirty_count);

    if (dirty_count == 0 && !g_immediate_mode)
        return;

    bool frame_active = aroma_drawlist_is_active();
    if (!frame_active)
    {
        if (!aroma_ui_begin_frame(window_id))
            return;
    }

    if (clear_color != AROMA_CLEAR_NONE)
    {
        aroma_graphics_clear(window_id, clear_color);
    }

    AromaDrawTask tasks[AROMA_MAX_DIRTY_NODES];
    size_t task_count = 0;

    int backend_type = aroma_backend_abi.get_graphics_backend_type ? aroma_backend_abi.get_graphics_backend_type() : -1;

    if (backend_type == GRAPHICS_BACKEND_GLES3)
    {

        for (int i = 0; i < g_window_count; ++i)
        {
            if (g_windows[i].window_id == window_id && g_windows[i].root_node)
            {
                collect_draw_tasks(g_windows[i].root_node, tasks, &task_count, AROMA_MAX_DIRTY_NODES);
                break;
            }
        }
    }
    else
    {

        for (size_t i = 0; i < dirty_count && task_count < AROMA_MAX_DIRTY_NODES; ++i)
        {
            struct AromaNode *node = dirty_nodes[i];
            if (!node || node->is_hidden)
                continue;

            AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(node);
            if (!draw_cb)
                continue;

            tasks[task_count++] = (AromaDrawTask){
                .node = node,
                .draw_cb = draw_cb,
                .z_index = node->z_index};
        }
    }

    if (task_count > 1)
    {
        qsort(tasks, task_count, sizeof(AromaDrawTask), draw_task_compare);
    }

    for (size_t i = 0; i < task_count; ++i)
    {
        tasks[i].draw_cb(tasks[i].node, window_id);
    }

    if (!frame_active)
    {
        aroma_ui_end_frame(window_id);
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
    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_windows[i].window_id == window_id)
        {
            return i;
        }
    }
    return INVALID_INDEX;
}

static int find_window_index_by_node(struct AromaNode *node)
{
    if (!node)
        return INVALID_INDEX;

    struct AromaNode *root = node;
    while (root->parent_node)
    {
        root = root->parent_node;
    }

    for (int i = 0; i < g_window_count; ++i)
    {
        if (g_windows[i].root_node == root)
        {
            return i;
        }
    }

    return INVALID_INDEX;
}

static void collect_draw_tasks(struct AromaNode *node, AromaDrawTask *tasks,
                               size_t *task_count, size_t max_tasks)
{
    if (!node || node->is_hidden || *task_count >= max_tasks)
        return;

    AromaNodeDrawFn draw_cb = aroma_node_get_draw_cb(node);
    if (draw_cb)
    {
        tasks[(*task_count)++] = (AromaDrawTask){
            .node = node,
            .draw_cb = draw_cb,
            .z_index = node->z_index};

        if (aroma_container_is_scrollable(node))
        {
            return;
        }
    }

    for (uint64_t i = 0; i < node->child_count; ++i)
    {
        if (node->child_nodes[i])
        {
            collect_draw_tasks(node->child_nodes[i], tasks, task_count, max_tasks);
        }
    }
}

static void window_update_callback(size_t window_id, void *data)
{
    (void)data;

    if (!aroma_ui_consume_redraw())
    {
        return;
    }

    AromaTheme theme = aroma_theme_get_global();

    aroma_ui_begin_frame(window_id);
    aroma_ui_render_dirty_window(window_id, theme.colors.background);
    aroma_dropdown_render_overlays(window_id);

    int width, height;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size)
    {
        platform->get_window_size(window_id, &width, &height);
    }

    aroma_node_update_layout(g_windows[window_id].root_node, 0, 0, width, height);

    aroma_ui_end_frame(window_id);

    aroma_dirty_list_clear();
}

static void show_splash_screen(size_t window_id, int width, int height)
{
    if (!g_splash_enabled)
        return;

    LOG_INFO("Showing splash screen...");

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
    {
        LOG_WARNING("Graphics interface not available for splash screen");
        return;
    }

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (!platform)
    {
        LOG_WARNING("Platform interface not available for splash screen");
        return;
    }

    int splash_font_size = platform->android_sp_to_px ? platform->android_sp_to_px(46) : 46;
    AromaFont *font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, splash_font_size);

    if (!font)
    {
        LOG_WARNING("Could not load font for splash screen");
        return;
    }

    aroma_ui_prepare_font_for_window(window_id, font);

    AromaTheme theme = aroma_theme_get_global();

    if (gfx->clear)
    {
        gfx->clear(window_id, theme.colors.background);
    }

    const char *title = "AromaUI";
    const char *slogan = "Modern UI for Everywhere";

    float title_scale = 1.0f;
    float slogan_scale = 0.3f;

    float title_width = aroma_font_get_line_width(font, title) * title_scale;
    int title_height = aroma_font_get_line_height(font) * title_scale;

    float slogan_width = aroma_font_get_line_width(font, slogan) * slogan_scale;
    int slogan_height = aroma_font_get_line_height(font) * slogan_scale;

    int gap = 20;
    int total_height = title_height + gap + slogan_height;

    int start_y = (height - total_height) / 2;
    int title_x = (width - (int)title_width) / 2;
    int title_y = start_y;

    int slogan_x = (width - (int)slogan_width) / 2;
    int slogan_y = title_y + title_height + gap;

    if (gfx->render_text)
    {
        gfx->render_text(window_id, font, title, title_x, title_y,
                         theme.colors.primary, title_scale);
        gfx->render_text(window_id, font, slogan, slogan_x, slogan_y,
                         theme.colors.text_secondary, slogan_scale);
    }

#ifndef ESP32
    aroma_graphics_swap_buffers(window_id);
#endif

    SLEEP_MS(5000);

    aroma_font_destroy(font);
}