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

#include "widgets/aroma_debug_overlay.h"
#include "core/aroma_common.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <string.h>
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>

struct AromaDebugOverlay
{
    AromaRect rect;
    bool visible;
    AromaFont *font;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t border_color;
    struct timespec last_time;
    int frame_count;
    float fps;
    float corner_radius;
    bool dragging;
    int drag_start_x;
    int drag_start_y;
    int _initial_x;
    int _initial_y;
    float actual_x;
    float actual_y;
    float velocity_x;
    float velocity_y;
    struct timespec last_drag_time;
    int last_drag_x;
    int last_drag_y;
    bool animating;
};

static float __time_diff_sec(struct timespec a, struct timespec b)
{
    float sec = (float)(a.tv_sec - b.tv_sec);
    float nsec = (float)(a.tv_nsec - b.tv_nsec) / 1000000000.0f;
    return sec + nsec;
}

    #include "aroma_event.h"


#include "aroma_event.h"


static bool debug_overlay_event_handler(AromaEvent *event, void *user_data)
{
    if (!event || !user_data) return false;
    AromaNode *node = (AromaNode *)user_data;
    if (!node || !node->node_widget_ptr) return false;
    AromaDebugOverlay *overlay = (AromaDebugOverlay *)node->node_widget_ptr;

    if (!overlay->visible) return false;

    if (event->event_type == EVENT_TYPE_MOUSE_CLICK) {
        int x = event->data.mouse.x;
        int y = event->data.mouse.y;
        if (x >= overlay->rect.x && x <= overlay->rect.x + overlay->rect.width &&
            y >= overlay->rect.y && y <= overlay->rect.y + overlay->rect.height) {
            overlay->dragging = true;
            overlay->animating = false;
            overlay->drag_start_x = x;
            overlay->drag_start_y = y;
            overlay->_initial_x = overlay->rect.x;
            overlay->_initial_y = overlay->rect.y;
            
            clock_gettime(CLOCK_MONOTONIC, &overlay->last_drag_time);
            overlay->last_drag_x = x;
            overlay->last_drag_y = y;
            overlay->velocity_x = 0;
            overlay->velocity_y = 0;
            return true;
        }
    } else if (event->event_type == EVENT_TYPE_MOUSE_RELEASE) {
        if (overlay->dragging) {
            overlay->dragging = false;
            overlay->animating = true;
            overlay->actual_x = (float)overlay->rect.x;
            overlay->actual_y = (float)overlay->rect.y;
            aroma_node_invalidate(node);
            return true;
        }
    } else if (event->event_type == EVENT_TYPE_MOUSE_MOVE) {
        if (overlay->dragging) {
            int dx = event->data.mouse.x - overlay->drag_start_x;
            int dy = event->data.mouse.y - overlay->drag_start_y;
            
            int tx = overlay->_initial_x + dx;
            int ty = overlay->_initial_y + dy;

            AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
            int win_w = 800, win_h = 480;
            if (platform && platform->get_window_size && event->target_node) {
                platform->get_window_size(0, &win_w, &win_h);
            } else if (platform && platform->get_window_size) {
                platform->get_window_size(0, &win_w, &win_h);
            }
            
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx + overlay->rect.width > win_w) tx = win_w - overlay->rect.width;
            if (ty + overlay->rect.height > win_h) ty = win_h - overlay->rect.height;

            overlay->rect.x = tx;
            overlay->rect.y = ty;
            
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            float dt = __time_diff_sec(now, overlay->last_drag_time);
            
            if (dt > 0.005f) {
                float vx = (event->data.mouse.x - overlay->last_drag_x) / dt;
                float vy = (event->data.mouse.y - overlay->last_drag_y) / dt;
                
                if (overlay->velocity_x == 0 && overlay->velocity_y == 0) {
                    overlay->velocity_x = vx;
                    overlay->velocity_y = vy;
                } else {
                    overlay->velocity_x = overlay->velocity_x * 0.5f + vx * 0.5f;
                    overlay->velocity_y = overlay->velocity_y * 0.5f + vy * 0.5f;
                }

                overlay->last_drag_time = now;
                overlay->last_drag_x = event->data.mouse.x;
                overlay->last_drag_y = event->data.mouse.y;
            }
            
            aroma_node_invalidate(node);
            return true;
        }
    }

    return false;
}

AromaNode *aroma_debug_overlay_create(AromaNode *parent, int x, int y, int width)
{
    if (!parent)
        return NULL;
    AromaDebugOverlay *overlay = (AromaDebugOverlay *)aroma_widget_alloc(sizeof(AromaDebugOverlay));
    if (!overlay)
        return NULL;

    memset(overlay, 0, sizeof(AromaDebugOverlay));
    overlay->rect.x = x;
    overlay->rect.y = y;
    overlay->rect.width = width;
    overlay->rect.height = 300;
    overlay->visible = true;
    overlay->frame_count = 0;
    overlay->fps = 0.0f;
    clock_gettime(CLOCK_MONOTONIC, &overlay->last_time);

    AromaTheme theme = aroma_theme_get_global();
    overlay->text_color = theme.colors.text_primary;
    
    uint8_t r, g, b;
    aroma_color_extract_rgb(theme.colors.surface, &r, &g, &b);
    overlay->bg_color = aroma_color_rgba(r, g, b, 150); // Frosted glass
    overlay->border_color = aroma_color_rgba(255, 255, 255, 80); // Glossy thin edge
    overlay->corner_radius = 16.0f;
    overlay->border_color = theme.colors.border;
    overlay->corner_radius = 10.0f;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, overlay);
    if (!node)
    {
        aroma_widget_free(overlay);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_debug_overlay_draw);

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, debug_overlay_event_handler, node, 100);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, debug_overlay_event_handler, node, 100);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, debug_overlay_event_handler, node, 100);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_debug_overlay_set_font(AromaNode *overlay_node, AromaFont *font)
{
    if (!overlay_node || !overlay_node->node_widget_ptr)
        return;
    AromaDebugOverlay *overlay = (AromaDebugOverlay *)overlay_node->node_widget_ptr;
    overlay->font = font;
}

void aroma_debug_overlay_set_visible(AromaNode *overlay_node, bool visible)
{
    if (!overlay_node || !overlay_node->node_widget_ptr)
        return;
    AromaDebugOverlay *overlay = (AromaDebugOverlay *)overlay_node->node_widget_ptr;
    overlay->visible = visible;
    aroma_node_invalidate(overlay_node);
}

static void count_nodes(AromaNode *node, size_t *count)
{
    if (!node)
        return;
    (*count)++;
    for (uint64_t i = 0; i < node->child_count; ++i)
    {
        count_nodes(node->child_nodes[i], count);
    }
}

void aroma_debug_overlay_draw(AromaNode *overlay_node, size_t window_id)
{
    if (!overlay_node || !overlay_node->node_widget_ptr)
        return;
    AromaDebugOverlay *overlay = (AromaDebugOverlay *)overlay_node->node_widget_ptr;
    if (!overlay->visible)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);

    if (overlay->animating) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        float dt = __time_diff_sec(now, overlay->last_time);
        if (dt > 0.05f) dt = 0.016f;
        else if (dt < 0.001f) dt = 0.001f;

        overlay->actual_x += overlay->velocity_x * dt;
        overlay->actual_y += overlay->velocity_y * dt;
        
        float friction = 4.0f; 
        overlay->velocity_x -= overlay->velocity_x * friction * dt;
        overlay->velocity_y -= overlay->velocity_y * friction * dt;

        overlay->rect.x = (int)overlay->actual_x;
        overlay->rect.y = (int)overlay->actual_y;

        AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
        int win_w = 800, win_h = 480;
        if (platform && platform->get_window_size) {
            platform->get_window_size(window_id, &win_w, &win_h);
        }

        bool bounced = false;
        if (overlay->actual_x < 0) { 
            overlay->actual_x = 0; 
            overlay->velocity_x *= -0.4f; 
            bounced = true;
        } else if (overlay->actual_x + overlay->rect.width > win_w) { 
            overlay->actual_x = win_w - overlay->rect.width; 
            overlay->velocity_x *= -0.4f; 
            bounced = true;
        }

        if (overlay->actual_y < 0) { 
            overlay->actual_y = 0; 
            overlay->velocity_y *= -0.4f; 
            bounced = true;
        } else if (overlay->actual_y + overlay->rect.height > win_h) { 
            overlay->actual_y = win_h - overlay->rect.height; 
            overlay->velocity_y *= -0.4f; 
            bounced = true;
        }

        overlay->rect.x = (int)overlay->actual_x;
        overlay->rect.y = (int)overlay->actual_y;

        if ((overlay->velocity_x > -15.0f && overlay->velocity_x < 15.0f) && 
            (overlay->velocity_y > -15.0f && overlay->velocity_y < 15.0f) && !bounced) {
            overlay->animating = false;
        } else {
            aroma_node_invalidate(overlay_node); 
        }
    }


    overlay->frame_count++;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float elapsed = __time_diff_sec(now, overlay->last_time);
    if (elapsed >= 0.5f)
    {
        overlay->fps = overlay->frame_count / elapsed;
        overlay->frame_count = 0;
        overlay->last_time = now;
    }

    gfx->fill_rectangle(window_id, overlay->rect.x, overlay->rect.y,
                        overlay->rect.width, overlay->rect.height,
                        overlay->bg_color, true, overlay->corner_radius);
    gfx->draw_hollow_rectangle(window_id, overlay->rect.x, overlay->rect.y,
                               overlay->rect.width, overlay->rect.height,
                               overlay->border_color, 1, true, overlay->corner_radius);

    if (overlay->font && gfx->render_text)
    {
        char line1[64], line2[64], line3[64], line4[64], line5[64], line6[64], line7[64], line8[64], line9[64], line10[64];
        snprintf(line1, sizeof(line1), "AromaUI v0.0.1");

        const char *gfx_backend = "?";
        switch (aroma_backend_abi.get_graphics_backend_type())
        {
        case GRAPHICS_BACKEND_GLES3:
            gfx_backend = "GLES3";
            break;
        case GRAPHICS_BACKEND_VULKAN:
            gfx_backend = "Vulkan";
            break;
        case GRAPHICS_BACKEND_SOFTWARE:
            gfx_backend = "Software";
            break;
        case GRAPHICS_BACKEND_TFT_ESPI:
            gfx_backend = "TFT_ESPI";
            break;
        case GRAPHICS_BACKEND_STM_SPI:
            gfx_backend = "STM_SPI";
            break;
        }
        snprintf(line2, sizeof(line2), "GFX: %s", gfx_backend);

        const char *plat_backend = "?";
        extern AromaBackendABI aroma_backend_abi;
        AromaPlatformInterface *plat = aroma_backend_abi.get_platform_interface();

        switch (aroma_backend_abi.get_platform_backend_type())
        {
        case PLATFORM_BACKEND_GLPS:
            plat_backend = "GLPS";
            break;
        case PLATFORM_BACKEND_ANDROID:
            plat_backend = "Android";
            break;
        case PLATFORM_BACKEND_TFT_ESPI:
            plat_backend = "TFT_ESPI";
            break;
        }

        snprintf(line3, sizeof(line3), "PLAT: %s", plat_backend);

        size_t node_count = 0;
        {
            AromaNode *root = overlay_node;
            while (root && root->parent_node)
                root = root->parent_node;
            count_nodes(root, &node_count);
        }
        snprintf(line4, sizeof(line4), "fps: %.1f", overlay->fps);
        snprintf(line5, sizeof(line5), "dirty: %zu", dirty_count);
        snprintf(line6, sizeof(line6), "nodes: %zu", node_count);

        extern AromaMemorySystem global_memory_system;
        size_t total_alloc = global_memory_system.widget_pools[0].total_allocated;
        size_t total_free = global_memory_system.widget_pools[0].total_freed;
        snprintf(line7, sizeof(line7), "mem: %zu/%zu", total_alloc, total_free);

        extern AromaNode *g_focused_node;
        snprintf(line8, sizeof(line8), "focus: %llu", g_focused_node ? (unsigned long long)g_focused_node->node_id : 0ULL);
#ifdef __builtin_expect
        extern AromaGLPSContext platform_ctx;
#endif
        int disp_w = 0, disp_h = 0;
        AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
        if (platform) { platform->get_window_size(window_id, &disp_w, &disp_h); }
        snprintf(line9, sizeof(line9), "Res: %dx%d", disp_w, disp_h);

        AromaNode* ev_root = aroma_event_get_root();
        snprintf(line10, sizeof(line10), "Root: %llu", ev_root ? (unsigned long long)ev_root->node_id : 0ULL);


        int line_height = aroma_font_get_line_height(overlay->font);
        int y1 = overlay->rect.y + 10;
        int y2 = y1 + line_height + 6;
        int y3 = y2 + line_height + 6;
        int y4 = y3 + line_height + 6;
        int y5 = y4 + line_height + 6;
        int y6 = y5 + line_height + 6;
        int y7 = y6 + line_height + 6;
        int y8 = y7 + line_height + 6;
        int y9 = y8 + line_height + 6;
        int y10 = y9 + line_height + 6;
        overlay->rect.height = (y10 - overlay->rect.y) + line_height + 10;
        gfx->render_text(window_id, overlay->font, line1, overlay->rect.x + 10, y1, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line2, overlay->rect.x + 10, y2, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line3, overlay->rect.x + 10, y3, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line4, overlay->rect.x + 10, y4, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line5, overlay->rect.x + 10, y5, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line6, overlay->rect.x + 10, y6, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line7, overlay->rect.x + 10, y7, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line8, overlay->rect.x + 10, y8, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line9, overlay->rect.x + 10, y9, overlay->text_color, 1.0f);
        gfx->render_text(window_id, overlay->font, line10, overlay->rect.x + 10, y10, overlay->text_color, 1.0f);
    }
}

void aroma_debug_overlay_destroy(AromaNode *overlay_node)
{
    if (!overlay_node)
        return;
    if (overlay_node->node_widget_ptr)
    {
        aroma_widget_free(overlay_node->node_widget_ptr);
        overlay_node->node_widget_ptr = NULL;
    }
}
