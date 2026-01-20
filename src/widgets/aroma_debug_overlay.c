#include "widgets/aroma_debug_overlay.h"
#include "core/aroma_common.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>

struct AromaDebugOverlay {
    AromaRect rect;
    bool visible;
    AromaFont* font;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t border_color;
    struct timespec last_time;
    int frame_count;
    float fps;
};

static float __time_diff_sec(struct timespec a, struct timespec b)
{
    float sec = (float)(a.tv_sec - b.tv_sec);
    float nsec = (float)(a.tv_nsec - b.tv_nsec) / 1000000000.0f;
    return sec + nsec;
}

AromaNode* aroma_debug_overlay_create(AromaNode* parent, int x, int y)
{
    if (!parent) return NULL;
    AromaDebugOverlay* overlay = (AromaDebugOverlay*)aroma_widget_alloc(sizeof(AromaDebugOverlay));
    if (!overlay) return NULL;

    memset(overlay, 0, sizeof(AromaDebugOverlay));
    overlay->rect.x = x;
    overlay->rect.y = y;
    overlay->rect.width = 170;
    overlay->rect.height = 170;
    overlay->visible = true;
    overlay->frame_count = 0;
    overlay->fps = 0.0f;
    clock_gettime(CLOCK_MONOTONIC, &overlay->last_time);

    AromaTheme theme = aroma_theme_get_global();
    overlay->bg_color = aroma_color_blend(theme.colors.surface, theme.colors.text_primary, 0.06f);
    overlay->text_color = theme.colors.text_primary;
    overlay->border_color = theme.colors.border;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, overlay);
    if (!node) {
        aroma_widget_free(overlay);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_debug_overlay_draw);

    return node;
}

void aroma_debug_overlay_set_font(AromaNode* overlay_node, AromaFont* font)
{
    if (!overlay_node || !overlay_node->node_widget_ptr) return;
    AromaDebugOverlay* overlay = (AromaDebugOverlay*)overlay_node->node_widget_ptr;
    overlay->font = font;
}

void aroma_debug_overlay_set_visible(AromaNode* overlay_node, bool visible)
{
    if (!overlay_node || !overlay_node->node_widget_ptr) return;
    AromaDebugOverlay* overlay = (AromaDebugOverlay*)overlay_node->node_widget_ptr;
    overlay->visible = visible;
    aroma_node_invalidate(overlay_node);
}

void aroma_debug_overlay_draw(AromaNode* overlay_node, size_t window_id)
{
    if (!overlay_node || !overlay_node->node_widget_ptr) return;
    AromaDebugOverlay* overlay = (AromaDebugOverlay*)overlay_node->node_widget_ptr;
    if (!overlay->visible) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);

    overlay->frame_count++;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float elapsed = __time_diff_sec(now, overlay->last_time);
    if (elapsed >= 0.5f) {
        overlay->fps = overlay->frame_count / elapsed;
        overlay->frame_count = 0;
        overlay->last_time = now;
    }

    gfx->fill_rectangle(window_id, overlay->rect.x, overlay->rect.y,
                        overlay->rect.width, overlay->rect.height,
                        overlay->bg_color, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, overlay->rect.x, overlay->rect.y,
                               overlay->rect.width, overlay->rect.height,
                               overlay->border_color, 1, true, 10.0f);

    if (overlay->font && gfx->render_text) {
        char line1[64], line2[64], line3[64], line4[64], line5[64], line6[64], line7[64], line8[64];
        snprintf(line1, sizeof(line1), "AromaUI v%s", AROMA_VERSION_STRING);

        const char* gfx_backend = "?";
        switch (aroma_backend_abi.get_graphics_backend_type()) {
            case 0: gfx_backend = "GLES3"; break;
            case 1: gfx_backend = "Software"; break;
            case 2: gfx_backend = "TFT_ESPI"; break;
            case 3: gfx_backend = "STM_SPI"; break;
        }
        snprintf(line2, sizeof(line2), "GFX: %s", gfx_backend);

        const char* plat_backend = "?";
        extern AromaBackendABI aroma_backend_abi;
        AromaPlatformInterface* plat = aroma_backend_abi.get_platform_interface();
 
        plat_backend = "GLPS";
        snprintf(line3, sizeof(line3), "PLAT: %s", plat_backend);


        size_t node_count = 0;
        {
            AromaNode* root = overlay_node;
            while (root && root->parent_node) root = root->parent_node;
            void count_nodes(AromaNode* node) {
                if (!node) return;
                node_count++;
                for (uint64_t i = 0; i < node->child_count; ++i) {
                    count_nodes(node->child_nodes[i]);
                }
            }
            count_nodes(root);
        }
        snprintf(line4, sizeof(line4), "fps: %.1f", overlay->fps);
        snprintf(line5, sizeof(line5), "dirty: %zu", dirty_count);
        snprintf(line6, sizeof(line6), "nodes: %zu", node_count);

        extern AromaMemorySystem global_memory_system;
        size_t total_alloc = global_memory_system.widget_pools[0].total_allocated;
        size_t total_free = global_memory_system.widget_pools[0].total_freed;
        snprintf(line7, sizeof(line7), "mem: %zu/%zu", total_alloc, total_free);

        extern AromaNode* g_focused_node;
        snprintf(line8, sizeof(line8), "focus: %llu", g_focused_node ? (unsigned long long)g_focused_node->node_id : 0ULL);

        int ascender = aroma_font_get_ascender(overlay->font);
        int y1 = overlay->rect.y + 10 + ascender;
        int y2 = y1 + ascender + 6;
        int y3 = y2 + ascender + 6;
        int y4 = y3 + ascender + 6;
        int y5 = y4 + ascender + 6;
        int y6 = y5 + ascender + 6;
        int y7 = y6 + ascender + 6;
        int y8 = y7 + ascender + 6;
        overlay->rect.height = (y8 - overlay->rect.y) + 10;
        gfx->render_text(window_id, overlay->font, line1, overlay->rect.x + 10, y1, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line2, overlay->rect.x + 10, y2, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line3, overlay->rect.x + 10, y3, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line4, overlay->rect.x + 10, y4, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line5, overlay->rect.x + 10, y5, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line6, overlay->rect.x + 10, y6, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line7, overlay->rect.x + 10, y7, overlay->text_color);
        gfx->render_text(window_id, overlay->font, line8, overlay->rect.x + 10, y8, overlay->text_color);
    }
}

void aroma_debug_overlay_destroy(AromaNode* overlay_node)
{
    if (!overlay_node) return;
    if (overlay_node->node_widget_ptr) {
        aroma_widget_free(overlay_node->node_widget_ptr);
        overlay_node->node_widget_ptr = NULL;
    }
}
