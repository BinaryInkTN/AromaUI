#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include "widgets/aroma_snackbar.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "core/aroma_timer.h"
#include "core/aroma_time.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"


#define AROMA_SNACKBAR_TEXT_MAX 128

typedef struct AromaSnackbar {
    AromaRect rect;
    char message[AROMA_SNACKBAR_TEXT_MAX];
    char action_label[32];
    void (*action_callback)(void* user_data);
    void* user_data;
    int action_hit_width;
    int duration_ms;
    bool visible;
    bool pending_show;
    AromaFont* font;
    float corner_radius;
    float text_scale;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t action_color;
    bool use_theme_color;
    uint64_t show_time_ms;
    AromaTimer* dismiss_timer;
    AromaNode* self_node;
} AromaSnackbar;

static int g_win_w = 800;
static int g_win_h = 600;

static void __snackbar_request_redraw(void* user_data) {
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static bool __snackbar_handle_event(AromaEvent* event, void* user_data) {
    if (!event || !event->target_node) return false;

    AromaSnackbar* bar = (AromaSnackbar*)event->target_node->node_widget_ptr;
    if (!bar || !bar->visible) return false;
    if (event->event_type != EVENT_TYPE_MOUSE_RELEASE) return false;

    AromaRect* r = &bar->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);
    if (!in_bounds) return false;

    if (bar->action_label[0] && bar->action_callback) {
        int action_width = (bar->action_hit_width > 0) ? bar->action_hit_width : 72;
        if (event->data.mouse.x >= r->x + r->width - action_width) {
            bar->action_callback(bar->user_data);
            bar->visible = false;
            __snackbar_request_redraw(user_data);
            return true;
        }
    }

    return false;
}

static void __calculate_snackbar_size(AromaSnackbar* bar) {
    if (!bar || !bar->font) return;

    int msg_width = aroma_font_get_line_width(bar->font, bar->message);
    int action_width = bar->action_label[0] ? aroma_font_get_line_width(bar->font, bar->action_label) + 25 : 0;

    bar->rect.width = msg_width + action_width + 100; // 60px padding
    if (bar->rect.width > g_win_w - 10) bar->rect.width = g_win_w - 10; // max width

    bar->rect.height = 52; // Material snackbar height
    bar->rect.x = (g_win_w - bar->rect.width) / 2;
    bar->rect.y = g_win_h - bar->rect.height - 16; // bottom margin
}

AromaNode* aroma_snackbar_create(AromaNode* parent, const char* message, int duration_ms) {
    if (!parent || !message) return NULL;

    AromaSnackbar* bar = (AromaSnackbar*)aroma_widget_alloc(sizeof(AromaSnackbar));
    if (!bar) return NULL;
    memset(bar, 0, sizeof(AromaSnackbar));

    bar->duration_ms = duration_ms;
    bar->visible = false;
    bar->pending_show = false;
    bar->corner_radius = 8.0f;
    bar->text_scale = 1.0f;

    strncpy(bar->message, message, AROMA_SNACKBAR_TEXT_MAX - 1);

    AromaTheme theme = aroma_theme_get_global();
    bar->bg_color = 0x333333;
    bar->text_color = 0xFFFFFF;
    bar->action_color = theme.colors.primary;
    bar->use_theme_color = true;

    // fetch current window size
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size) {
        platform->get_window_size(0, &g_win_w, &g_win_h);
    }

    bar->show_time_ms = 0;
    bar->dismiss_timer = NULL;
    bar->self_node = NULL;

    // size/position will be finalized after font is set

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, bar);
    if (!node) {
        aroma_widget_free(bar);
        return NULL;
    }
    bar->self_node = node;
    aroma_node_set_z_index(node, INT_MAX);
    aroma_node_set_draw_cb(node, aroma_snackbar_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE,
                          __snackbar_handle_event, aroma_ui_request_redraw, 80);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_snackbar_set_action(AromaNode* snackbar_node, const char* action_text,
                               void (*callback)(void* user_data), void* user_data) {
    if (!snackbar_node || !snackbar_node->node_widget_ptr || !action_text) return;

    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    strncpy(bar->action_label, action_text, sizeof(bar->action_label) - 1);
    bar->action_callback = callback;
    bar->user_data = user_data;

    bar->action_hit_width = aroma_font_get_line_width(bar->font, action_text) + 25;

    __calculate_snackbar_size(bar);
    
}

void aroma_snackbar_set_font(AromaNode* snackbar_node, AromaFont* font) {
    if (!snackbar_node || !snackbar_node->node_widget_ptr || !font) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    bar->font = font;

    __calculate_snackbar_size(bar);
    
}

static void __snackbar_dismiss_cb(void* user_data) {
    AromaSnackbar* bar = (AromaSnackbar*)user_data;
    if (!bar) return;
    bar->visible = false;
    bar->dismiss_timer = NULL;
    if (bar->self_node) {
        aroma_node_set_hidden(bar->self_node, true); // <--- fix: hide snackbar node itself
        aroma_node_invalidate(bar->self_node);
    }
    aroma_ui_request_redraw(NULL);
}

void aroma_snackbar_show(AromaNode* snackbar_node) {
    if (!snackbar_node || !snackbar_node->node_widget_ptr) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    bar->visible = true;
    bar->pending_show = true;
    bar->show_time_ms = aroma_time_now_ms();

    /* Cancel any previous dismiss timer and start a new one */
    if (bar->dismiss_timer) {
        aroma_timer_cancel(bar->dismiss_timer);
        bar->dismiss_timer = NULL;
    }
    if (bar->duration_ms > 0) {
        bar->dismiss_timer = aroma_timer_create(
            (uint32_t)bar->duration_ms, false, __snackbar_dismiss_cb, bar);
    }

    aroma_node_invalidate(snackbar_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_snackbar_draw(AromaNode* snackbar_node, size_t window_id) {
    if (!snackbar_node || !snackbar_node->node_widget_ptr) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    if (!bar->visible || aroma_node_is_hidden(snackbar_node)) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    /* Re-fetch window size and recompute position every frame.
       The layout engine may have delta-shifted rect.x/y away from
       the intended position; recalculating here corrects that. */
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size) {
        platform->get_window_size(window_id, &g_win_w, &g_win_h);
    }
    __calculate_snackbar_size(bar);

    if (bar->use_theme_color) {
        AromaTheme theme = aroma_theme_get_global();
        bar->bg_color = theme.colors.surface;
        bar->text_color = theme.colors.text_primary;
        bar->action_color = theme.colors.primary;
    }

    gfx->fill_rectangle(window_id, bar->rect.x, bar->rect.y,
                        bar->rect.width, bar->rect.height,
                        bar->bg_color, true, bar->corner_radius);

    if (bar->font && gfx->render_text) {
        int line_h = aroma_font_get_line_height(bar->font);
        int text_y = bar->rect.y + (bar->rect.height - line_h) / 2;
        gfx->render_text(window_id, bar->font, bar->message,
                         bar->rect.x + 16, text_y,
                         bar->text_color, bar->text_scale);

        if (bar->action_label[0]) {
            gfx->render_text(window_id, bar->font, bar->action_label,
                             bar->rect.x + bar->rect.width - bar->action_hit_width - 8,
                             text_y,
                             bar->action_color, bar->text_scale);
        }
    }
}

void aroma_snackbar_destroy(AromaNode* snackbar_node) {
    if (!snackbar_node) return;
    if (snackbar_node->node_widget_ptr) {
        AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
        if (bar->dismiss_timer) {
            aroma_timer_cancel(bar->dismiss_timer);
            bar->dismiss_timer = NULL;
        }
        aroma_widget_free(snackbar_node->node_widget_ptr);
        snackbar_node->node_widget_ptr = NULL;
    }
    __destroy_node(snackbar_node);
}
