#include "widgets/aroma_loading.h"
#include "core/aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include <math.h>

typedef struct  __attribute__((packed, aligned(1))) __attribute__((packed, aligned(1))) {
    AromaRect rect;
    int thickness;
    uint32_t color;
    float start_angle;
    AromaTimer* timer;
    AromaNode* node;
} AromaLoading;

static void loading_draw(AromaNode* node, size_t window_id) {
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node)) return;

    AromaLoading* loading = (AromaLoading*)node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle) return;

    int r = loading->rect.width / 2;
    int cx = loading->rect.x + r;
    int cy = loading->rect.y + loading->rect.height / 2;
    int orbit_r = r - loading->thickness;
    if (orbit_r < 1) orbit_r = 1;

    for (int i = 0; i < 4; i++) {
        float angle = loading->start_angle - (i * 20.0f);
        float angle_rad = angle * 3.14159265f / 180.0f;
        int dot_size = (loading->thickness * 2) - i * (loading->thickness / 2);
        if (dot_size < 2) dot_size = 2;
        
        int dx = cx + (int)(orbit_r * cosf(angle_rad)) - dot_size / 2;
        int dy = cy + (int)(orbit_r * sinf(angle_rad)) - dot_size / 2;
        
        uint32_t a = (loading->color >> 24) & 0xFF;
        if (a == 0) a = 0xFF; // Fallback if color doesn't have alpha set properly
        a = a / (i + 1);
        uint32_t c = (a << 24) | (loading->color & 0x00FFFFFF);
        
        gfx->fill_rectangle(window_id, dx, dy, dot_size, dot_size, c, true, dot_size / 2.0f);
    }
}

static void loading_timer_cb(void* user_data) {
    AromaLoading* loading = (AromaLoading*)user_data;
    if (!loading) return;

    if (loading->node) {
        AromaNode* curr = loading->node;
        bool is_visible = true;
        while(curr) {
            if (curr->is_hidden) {
                is_visible = false;
                break;
            }
            curr = curr->parent_node;
        }
        if (!is_visible) return;
    }

    loading->start_angle += 10.0f;
    if (loading->start_angle >= 360.0f) {
        loading->start_angle -= 360.0f;
    }
    
    if (loading->node) {
        aroma_node_invalidate(loading->node);
    }
}

AromaNode* aroma_loading_create(AromaNode* parent, int x, int y, int radius, int thickness, uint32_t color) {
    AromaLoading* loading = (AromaLoading*)aroma_widget_alloc(sizeof(AromaLoading));
    if (!loading) return NULL;

    loading->rect.x = x;
    loading->rect.y = y;
    loading->rect.width = radius * 2;
    loading->rect.height = radius * 2;
    loading->thickness = thickness;
    loading->color = color;
    loading->start_angle = 0.0f;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, loading);
    if (!node) {
        aroma_widget_free(loading);
        return NULL;
    }

    loading->node = node;
    aroma_node_set_draw_cb(node, loading_draw);
    loading->timer = aroma_timer_create(30, true, loading_timer_cb, loading);

    return node;
}
